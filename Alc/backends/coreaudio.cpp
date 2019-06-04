/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "backends/coreaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "alu.h"
#include "ringbuffer.h"
#include "converter.h"
#include "backends/base.h"

#include <unistd.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>


namespace {

static const ALCchar ca_device[] = "CoreAudio Default";


struct CoreAudioPlayback final : public BackendBase {
    CoreAudioPlayback(ALCdevice *device) noexcept : BackendBase{device} { }
    ~CoreAudioPlayback() override;

    static OSStatus MixerProcC(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
        const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames,
        AudioBufferList *ioData);
    OSStatus MixerProc(AudioUnitRenderActionFlags *ioActionFlags,
        const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames,
        AudioBufferList *ioData);

    ALCenum open(const ALCchar *name) override;
    ALCboolean reset() override;
    ALCboolean start() override;
    void stop() override;

    AudioUnit mAudioUnit;

    ALuint mFrameSize{0u};
    AudioStreamBasicDescription mFormat{}; // This is the OpenAL format as a CoreAudio ASBD

    DEF_NEWDEL(CoreAudioPlayback)
};

CoreAudioPlayback::~CoreAudioPlayback()
{
    AudioUnitUninitialize(mAudioUnit);
    AudioComponentInstanceDispose(mAudioUnit);
}


OSStatus CoreAudioPlayback::MixerProcC(void *inRefCon,
    AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp,
    UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
    return static_cast<CoreAudioPlayback*>(inRefCon)->MixerProc(ioActionFlags, inTimeStamp,
        inBusNumber, inNumberFrames, ioData);
}

OSStatus CoreAudioPlayback::MixerProc(AudioUnitRenderActionFlags* UNUSED(ioActionFlags),
    const AudioTimeStamp* UNUSED(inTimeStamp), UInt32 UNUSED(inBusNumber),
    UInt32 UNUSED(inNumberFrames), AudioBufferList *ioData)
{
    lock();
    aluMixData(mDevice, ioData->mBuffers[0].mData, ioData->mBuffers[0].mDataByteSize/mFrameSize);
    unlock();
    return noErr;
}


ALCenum CoreAudioPlayback::open(const ALCchar *name)
{
    if(!name)
        name = ca_device;
    else if(strcmp(name, ca_device) != 0)
        return ALC_INVALID_VALUE;

    /* open the default output unit */
    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IOS
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp{AudioComponentFindNext(NULL, &desc)};
    if(comp == nullptr)
    {
        ERR("AudioComponentFindNext failed\n");
        return ALC_INVALID_VALUE;
    }

    OSStatus err{AudioComponentInstanceNew(comp, &mAudioUnit)};
    if(err != noErr)
    {
        ERR("AudioComponentInstanceNew failed\n");
        return ALC_INVALID_VALUE;
    }

    /* init and start the default audio unit... */
    err = AudioUnitInitialize(mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioUnitInitialize failed\n");
        AudioComponentInstanceDispose(mAudioUnit);
        return ALC_INVALID_VALUE;
    }

    mDevice->DeviceName = name;
    return ALC_NO_ERROR;
}

ALCboolean CoreAudioPlayback::reset()
{
    OSStatus err{AudioUnitUninitialize(mAudioUnit)};
    if(err != noErr)
        ERR("-- AudioUnitUninitialize failed.\n");

    /* retrieve default output unit's properties (output side) */
    AudioStreamBasicDescription streamFormat{};
    auto size = static_cast<UInt32>(sizeof(AudioStreamBasicDescription));
    err = AudioUnitGetProperty(mAudioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
        0, &streamFormat, &size);
    if(err != noErr || size != sizeof(AudioStreamBasicDescription))
    {
        ERR("AudioUnitGetProperty failed\n");
        return ALC_FALSE;
    }

#if 0
    TRACE("Output streamFormat of default output unit -\n");
    TRACE("  streamFormat.mFramesPerPacket = %d\n", streamFormat.mFramesPerPacket);
    TRACE("  streamFormat.mChannelsPerFrame = %d\n", streamFormat.mChannelsPerFrame);
    TRACE("  streamFormat.mBitsPerChannel = %d\n", streamFormat.mBitsPerChannel);
    TRACE("  streamFormat.mBytesPerPacket = %d\n", streamFormat.mBytesPerPacket);
    TRACE("  streamFormat.mBytesPerFrame = %d\n", streamFormat.mBytesPerFrame);
    TRACE("  streamFormat.mSampleRate = %5.0f\n", streamFormat.mSampleRate);
#endif

    /* set default output unit's input side to match output side */
    err = AudioUnitSetProperty(mAudioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
        0, &streamFormat, size);
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_FALSE;
    }

    if(mDevice->Frequency != streamFormat.mSampleRate)
    {
        mDevice->BufferSize = static_cast<ALuint>(uint64_t{mDevice->BufferSize} *
            streamFormat.mSampleRate / mDevice->Frequency);
        mDevice->Frequency = streamFormat.mSampleRate;
    }

    /* FIXME: How to tell what channels are what in the output device, and how
     * to specify what we're giving?  eg, 6.0 vs 5.1 */
    switch(streamFormat.mChannelsPerFrame)
    {
        case 1:
            mDevice->FmtChans = DevFmtMono;
            break;
        case 2:
            mDevice->FmtChans = DevFmtStereo;
            break;
        case 4:
            mDevice->FmtChans = DevFmtQuad;
            break;
        case 6:
            mDevice->FmtChans = DevFmtX51;
            break;
        case 7:
            mDevice->FmtChans = DevFmtX61;
            break;
        case 8:
            mDevice->FmtChans = DevFmtX71;
            break;
        default:
            ERR("Unhandled channel count (%d), using Stereo\n", streamFormat.mChannelsPerFrame);
            mDevice->FmtChans = DevFmtStereo;
            streamFormat.mChannelsPerFrame = 2;
            break;
    }
    SetDefaultWFXChannelOrder(mDevice);

    /* use channel count and sample rate from the default output unit's current
     * parameters, but reset everything else */
    streamFormat.mFramesPerPacket = 1;
    streamFormat.mFormatFlags = 0;
    switch(mDevice->FmtType)
    {
        case DevFmtUByte:
            mDevice->FmtType = DevFmtByte;
            /* fall-through */
        case DevFmtByte:
            streamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
            streamFormat.mBitsPerChannel = 8;
            break;
        case DevFmtUShort:
            mDevice->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            streamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
            streamFormat.mBitsPerChannel = 16;
            break;
        case DevFmtUInt:
            mDevice->FmtType = DevFmtInt;
            /* fall-through */
        case DevFmtInt:
            streamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
            streamFormat.mBitsPerChannel = 32;
            break;
        case DevFmtFloat:
            streamFormat.mFormatFlags = kLinearPCMFormatFlagIsFloat;
            streamFormat.mBitsPerChannel = 32;
            break;
    }
    streamFormat.mBytesPerFrame = streamFormat.mChannelsPerFrame *
                                  streamFormat.mBitsPerChannel / 8;
    streamFormat.mBytesPerPacket = streamFormat.mBytesPerFrame;
    streamFormat.mFormatID = kAudioFormatLinearPCM;
    streamFormat.mFormatFlags |= kAudioFormatFlagsNativeEndian |
                                 kLinearPCMFormatFlagIsPacked;

    err = AudioUnitSetProperty(mAudioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
        0, &streamFormat, sizeof(AudioStreamBasicDescription));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_FALSE;
    }

    /* setup callback */
    mFrameSize = mDevice->frameSizeFromFmt();
    AURenderCallbackStruct input{};
    input.inputProc = CoreAudioPlayback::MixerProcC;
    input.inputProcRefCon = this;

    err = AudioUnitSetProperty(mAudioUnit, kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input, 0, &input, sizeof(AURenderCallbackStruct));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_FALSE;
    }

    /* init the default audio unit... */
    err = AudioUnitInitialize(mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioUnitInitialize failed\n");
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

ALCboolean CoreAudioPlayback::start()
{
    OSStatus err{AudioOutputUnitStart(mAudioUnit)};
    if(err != noErr)
    {
        ERR("AudioOutputUnitStart failed\n");
        return ALC_FALSE;
    }
    return ALC_TRUE;
}

void CoreAudioPlayback::stop()
{
    OSStatus err{AudioOutputUnitStop(mAudioUnit)};
    if(err != noErr)
        ERR("AudioOutputUnitStop failed\n");
}


struct CoreAudioCapture final : public BackendBase {
    CoreAudioCapture(ALCdevice *device) noexcept : BackendBase{device} { }
    ~CoreAudioCapture() override;

    static OSStatus RecordProcC(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
        const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames,
        AudioBufferList *ioData);
    OSStatus RecordProc(AudioUnitRenderActionFlags *ioActionFlags,
        const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
        UInt32 inNumberFrames, AudioBufferList *ioData);

    ALCenum open(const ALCchar *name) override;
    ALCboolean start() override;
    void stop() override;
    ALCenum captureSamples(void *buffer, ALCuint samples) override;
    ALCuint availableSamples() override;

    AudioUnit mAudioUnit{0};

    ALuint mFrameSize{0u};
    AudioStreamBasicDescription mFormat{};  // This is the OpenAL format as a CoreAudio ASBD

    SampleConverterPtr mConverter;

    RingBufferPtr mRing{nullptr};

    DEF_NEWDEL(CoreAudioCapture)
};

CoreAudioCapture::~CoreAudioCapture()
{
    if(mAudioUnit)
        AudioComponentInstanceDispose(mAudioUnit);
    mAudioUnit = 0;
}


OSStatus CoreAudioCapture::RecordProcC(void *inRefCon,
    AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp,
    UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
    return static_cast<CoreAudioCapture*>(inRefCon)->RecordProc(ioActionFlags, inTimeStamp,
        inBusNumber, inNumberFrames, ioData);
}

OSStatus CoreAudioCapture::RecordProc(AudioUnitRenderActionFlags* UNUSED(ioActionFlags),
    const AudioTimeStamp *inTimeStamp, UInt32 UNUSED(inBusNumber), UInt32 inNumberFrames,
    AudioBufferList* UNUSED(ioData))
{
    AudioUnitRenderActionFlags flags = 0;
    union {
        ALbyte _[sizeof(AudioBufferList) + sizeof(AudioBuffer)*2];
        AudioBufferList list;
    } audiobuf = { { 0 } };

    auto rec_vec = mRing->getWriteVector();
    inNumberFrames = minz(inNumberFrames, rec_vec.first.len+rec_vec.second.len);

    // Fill the ringbuffer's two segments with data from the input device
    if(rec_vec.first.len >= inNumberFrames)
    {
        audiobuf.list.mNumberBuffers = 1;
        audiobuf.list.mBuffers[0].mNumberChannels = mFormat.mChannelsPerFrame;
        audiobuf.list.mBuffers[0].mData = rec_vec.first.buf;
        audiobuf.list.mBuffers[0].mDataByteSize = inNumberFrames * mFormat.mBytesPerFrame;
    }
    else
    {
        const size_t remaining{inNumberFrames-rec_vec.first.len};
        audiobuf.list.mNumberBuffers = 2;
        audiobuf.list.mBuffers[0].mNumberChannels = mFormat.mChannelsPerFrame;
        audiobuf.list.mBuffers[0].mData = rec_vec.first.buf;
        audiobuf.list.mBuffers[0].mDataByteSize = rec_vec.first.len * mFormat.mBytesPerFrame;
        audiobuf.list.mBuffers[1].mNumberChannels = mFormat.mChannelsPerFrame;
        audiobuf.list.mBuffers[1].mData = rec_vec.second.buf;
        audiobuf.list.mBuffers[1].mDataByteSize = remaining * mFormat.mBytesPerFrame;
    }
    OSStatus err{AudioUnitRender(mAudioUnit, &flags, inTimeStamp, audiobuf.list.mNumberBuffers,
        inNumberFrames, &audiobuf.list)};
    if(err != noErr)
    {
        ERR("AudioUnitRender error: %d\n", err);
        return err;
    }

    mRing->writeAdvance(inNumberFrames);
    return noErr;
}


ALCenum CoreAudioCapture::open(const ALCchar *name)
{
    AudioStreamBasicDescription requestedFormat;  // The application requested format
    AudioStreamBasicDescription hardwareFormat;   // The hardware format
    AudioStreamBasicDescription outputFormat;     // The AudioUnit output format
    AURenderCallbackStruct input;
    AudioComponentDescription desc;
    UInt32 outputFrameCount;
    UInt32 propertySize;
    AudioObjectPropertyAddress propertyAddress;
    UInt32 enableIO;
    AudioComponent comp;
    OSStatus err;

    if(!name)
        name = ca_device;
    else if(strcmp(name, ca_device) != 0)
        return ALC_INVALID_VALUE;

    desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IOS
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
    desc.componentSubType = kAudioUnitSubType_HALOutput;
#endif
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    // Search for component with given description
    comp = AudioComponentFindNext(NULL, &desc);
    if(comp == NULL)
    {
        ERR("AudioComponentFindNext failed\n");
        return ALC_INVALID_VALUE;
    }

    // Open the component
    err = AudioComponentInstanceNew(comp, &mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioComponentInstanceNew failed\n");
        return ALC_INVALID_VALUE;
    }

    // Turn off AudioUnit output
    enableIO = 0;
    err = AudioUnitSetProperty(mAudioUnit, kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Output, 0, &enableIO, sizeof(ALuint));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_INVALID_VALUE;
    }

    // Turn on AudioUnit input
    enableIO = 1;
    err = AudioUnitSetProperty(mAudioUnit, kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Input, 1, &enableIO, sizeof(ALuint));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_INVALID_VALUE;
    }

#if !TARGET_OS_IOS
    {
        // Get the default input device
        AudioDeviceID inputDevice = kAudioDeviceUnknown;

        propertySize = sizeof(AudioDeviceID);
        propertyAddress.mSelector = kAudioHardwarePropertyDefaultInputDevice;
        propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
        propertyAddress.mElement = kAudioObjectPropertyElementMaster;

        err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &propertySize, &inputDevice);
        if(err != noErr)
        {
            ERR("AudioObjectGetPropertyData failed\n");
            return ALC_INVALID_VALUE;
        }
        if(inputDevice == kAudioDeviceUnknown)
        {
            ERR("No input device found\n");
            return ALC_INVALID_VALUE;
        }

        // Track the input device
        err = AudioUnitSetProperty(mAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
            kAudioUnitScope_Global, 0, &inputDevice, sizeof(AudioDeviceID));
        if(err != noErr)
        {
            ERR("AudioUnitSetProperty failed\n");
            return ALC_INVALID_VALUE;
        }
    }
#endif

    // set capture callback
    input.inputProc = CoreAudioCapture::RecordProcC;
    input.inputProcRefCon = this;

    err = AudioUnitSetProperty(mAudioUnit, kAudioOutputUnitProperty_SetInputCallback,
        kAudioUnitScope_Global, 0, &input, sizeof(AURenderCallbackStruct));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_INVALID_VALUE;
    }

    // Initialize the device
    err = AudioUnitInitialize(mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioUnitInitialize failed\n");
        return ALC_INVALID_VALUE;
    }

    // Get the hardware format
    propertySize = sizeof(AudioStreamBasicDescription);
    err = AudioUnitGetProperty(mAudioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
        1, &hardwareFormat, &propertySize);
    if(err != noErr || propertySize != sizeof(AudioStreamBasicDescription))
    {
        ERR("AudioUnitGetProperty failed\n");
        return ALC_INVALID_VALUE;
    }

    // Set up the requested format description
    switch(mDevice->FmtType)
    {
        case DevFmtUByte:
            requestedFormat.mBitsPerChannel = 8;
            requestedFormat.mFormatFlags = kAudioFormatFlagIsPacked;
            break;
        case DevFmtShort:
            requestedFormat.mBitsPerChannel = 16;
            requestedFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
            break;
        case DevFmtInt:
            requestedFormat.mBitsPerChannel = 32;
            requestedFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
            break;
        case DevFmtFloat:
            requestedFormat.mBitsPerChannel = 32;
            requestedFormat.mFormatFlags = kAudioFormatFlagIsPacked;
            break;
        case DevFmtByte:
        case DevFmtUShort:
        case DevFmtUInt:
            ERR("%s samples not supported\n", DevFmtTypeString(mDevice->FmtType));
            return ALC_INVALID_VALUE;
    }

    switch(mDevice->FmtChans)
    {
        case DevFmtMono:
            requestedFormat.mChannelsPerFrame = 1;
            break;
        case DevFmtStereo:
            requestedFormat.mChannelsPerFrame = 2;
            break;

        case DevFmtQuad:
        case DevFmtX51:
        case DevFmtX51Rear:
        case DevFmtX61:
        case DevFmtX71:
        case DevFmtAmbi3D:
            ERR("%s not supported\n", DevFmtChannelsString(mDevice->FmtChans));
            return ALC_INVALID_VALUE;
    }

    requestedFormat.mBytesPerFrame = requestedFormat.mChannelsPerFrame * requestedFormat.mBitsPerChannel / 8;
    requestedFormat.mBytesPerPacket = requestedFormat.mBytesPerFrame;
    requestedFormat.mSampleRate = mDevice->Frequency;
    requestedFormat.mFormatID = kAudioFormatLinearPCM;
    requestedFormat.mReserved = 0;
    requestedFormat.mFramesPerPacket = 1;

    // save requested format description for later use
    mFormat = requestedFormat;
    mFrameSize = mDevice->frameSizeFromFmt();

    // Use intermediate format for sample rate conversion (outputFormat)
    // Set sample rate to the same as hardware for resampling later
    outputFormat = requestedFormat;
    outputFormat.mSampleRate = hardwareFormat.mSampleRate;

    // The output format should be the requested format, but using the hardware sample rate
    // This is because the AudioUnit will automatically scale other properties, except for sample rate
    err = AudioUnitSetProperty(mAudioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
        1, (void*)&outputFormat, sizeof(outputFormat));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_INVALID_VALUE;
    }

    // Set the AudioUnit output format frame count
    uint64_t FrameCount64{mDevice->UpdateSize};
    FrameCount64 = (FrameCount64*outputFormat.mSampleRate + mDevice->Frequency-1) /
        mDevice->Frequency;
    FrameCount64 += MAX_RESAMPLE_PADDING*2;
    if(FrameCount64 > std::numeric_limits<uint32_t>::max()/2)
    {
        ERR("FrameCount too large\n");
        return ALC_INVALID_VALUE;
    }

    outputFrameCount = static_cast<uint32_t>(FrameCount64);
    err = AudioUnitSetProperty(mAudioUnit, kAudioUnitProperty_MaximumFramesPerSlice,
        kAudioUnitScope_Output, 0, &outputFrameCount, sizeof(outputFrameCount));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed: %d\n", err);
        return ALC_INVALID_VALUE;
    }

    // Set up sample converter if needed
    if(outputFormat.mSampleRate != mDevice->Frequency)
        mConverter = CreateSampleConverter(mDevice->FmtType, mDevice->FmtType,
            mFormat.mChannelsPerFrame, hardwareFormat.mSampleRate, mDevice->Frequency,
            BSinc24Resampler);

    mRing = CreateRingBuffer(outputFrameCount, mFrameSize, false);
    if(!mRing) return ALC_INVALID_VALUE;

    mDevice->DeviceName = name;
    return ALC_NO_ERROR;
}


ALCboolean CoreAudioCapture::start()
{
    OSStatus err{AudioOutputUnitStart(mAudioUnit)};
    if(err != noErr)
    {
        ERR("AudioOutputUnitStart failed\n");
        return ALC_FALSE;
    }
    return ALC_TRUE;
}

void CoreAudioCapture::stop()
{
    OSStatus err{AudioOutputUnitStop(mAudioUnit)};
    if(err != noErr)
        ERR("AudioOutputUnitStop failed\n");
}

ALCenum CoreAudioCapture::captureSamples(void *buffer, ALCuint samples)
{
    if(!mConverter)
    {
        mRing->read(buffer, samples);
        return ALC_NO_ERROR;
    }

    auto rec_vec = mRing->getReadVector();
    const void *src0{rec_vec.first.buf};
    auto src0len = static_cast<ALsizei>(rec_vec.first.len);
    auto got = static_cast<ALuint>(mConverter->convert(&src0, &src0len, buffer, samples));
    size_t total_read{rec_vec.first.len - src0len};
    if(got < samples && !src0len && rec_vec.second.len > 0)
    {
        const void *src1{rec_vec.second.buf};
        auto src1len = static_cast<ALsizei>(rec_vec.second.len);
        got += static_cast<ALuint>(mConverter->convert(&src1, &src1len,
            static_cast<char*>(buffer)+got, samples-got));
        total_read += rec_vec.second.len - src1len;
    }

    mRing->readAdvance(total_read);
    return ALC_NO_ERROR;
}

ALCuint CoreAudioCapture::availableSamples()
{
    if(!mConverter) return mRing->readSpace();
    return mConverter->availableOut(mRing->readSpace());
}

} // namespace

BackendFactory &CoreAudioBackendFactory::getFactory()
{
    static CoreAudioBackendFactory factory{};
    return factory;
}

bool CoreAudioBackendFactory::init() { return true; }

bool CoreAudioBackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback || type == BackendType::Capture; }

void CoreAudioBackendFactory::probe(DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case DevProbe::Playback:
        case DevProbe::Capture:
            /* Includes null char. */
            outnames->append(ca_device, sizeof(ca_device));
            break;
    }
}

BackendPtr CoreAudioBackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new CoreAudioPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new CoreAudioCapture{device}};
    return nullptr;
}
