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

#include <unistd.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>


static const ALCchar ca_device[] = "CoreAudio Default";


struct ALCcoreAudioPlayback final : public ALCbackend {
    AudioUnit mAudioUnit;

    ALuint mFrameSize{0u};
    AudioStreamBasicDescription mFormat{}; // This is the OpenAL format as a CoreAudio ASBD

    ALCcoreAudioPlayback(ALCdevice *device) noexcept : ALCbackend{device} { }
};

static void ALCcoreAudioPlayback_Construct(ALCcoreAudioPlayback *self, ALCdevice *device);
static void ALCcoreAudioPlayback_Destruct(ALCcoreAudioPlayback *self);
static ALCenum ALCcoreAudioPlayback_open(ALCcoreAudioPlayback *self, const ALCchar *name);
static ALCboolean ALCcoreAudioPlayback_reset(ALCcoreAudioPlayback *self);
static ALCboolean ALCcoreAudioPlayback_start(ALCcoreAudioPlayback *self);
static void ALCcoreAudioPlayback_stop(ALCcoreAudioPlayback *self);
static DECLARE_FORWARD2(ALCcoreAudioPlayback, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
static DECLARE_FORWARD(ALCcoreAudioPlayback, ALCbackend, ALCuint, availableSamples)
static DECLARE_FORWARD(ALCcoreAudioPlayback, ALCbackend, ClockLatency, getClockLatency)
static DECLARE_FORWARD(ALCcoreAudioPlayback, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCcoreAudioPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCcoreAudioPlayback)

DEFINE_ALCBACKEND_VTABLE(ALCcoreAudioPlayback);


static void ALCcoreAudioPlayback_Construct(ALCcoreAudioPlayback *self, ALCdevice *device)
{
    new (self) ALCcoreAudioPlayback{device};
    SET_VTABLE2(ALCcoreAudioPlayback, ALCbackend, self);
}

static void ALCcoreAudioPlayback_Destruct(ALCcoreAudioPlayback *self)
{
    AudioUnitUninitialize(self->mAudioUnit);
    AudioComponentInstanceDispose(self->mAudioUnit);

    self->~ALCcoreAudioPlayback();
}


static OSStatus ALCcoreAudioPlayback_MixerProc(void *inRefCon,
  AudioUnitRenderActionFlags* UNUSED(ioActionFlags), const AudioTimeStamp* UNUSED(inTimeStamp),
  UInt32 UNUSED(inBusNumber), UInt32 UNUSED(inNumberFrames), AudioBufferList *ioData)
{
    ALCcoreAudioPlayback *self = static_cast<ALCcoreAudioPlayback*>(inRefCon);
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;

    ALCcoreAudioPlayback_lock(self);
    aluMixData(device, ioData->mBuffers[0].mData,
               ioData->mBuffers[0].mDataByteSize / self->mFrameSize);
    ALCcoreAudioPlayback_unlock(self);

    return noErr;
}


static ALCenum ALCcoreAudioPlayback_open(ALCcoreAudioPlayback *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    AudioComponentDescription desc;
    AudioComponent comp;
    OSStatus err;

    if(!name)
        name = ca_device;
    else if(strcmp(name, ca_device) != 0)
        return ALC_INVALID_VALUE;

    /* open the default output unit */
    desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IOS
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    comp = AudioComponentFindNext(NULL, &desc);
    if(comp == NULL)
    {
        ERR("AudioComponentFindNext failed\n");
        return ALC_INVALID_VALUE;
    }

    err = AudioComponentInstanceNew(comp, &self->mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioComponentInstanceNew failed\n");
        return ALC_INVALID_VALUE;
    }

    /* init and start the default audio unit... */
    err = AudioUnitInitialize(self->mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioUnitInitialize failed\n");
        AudioComponentInstanceDispose(self->mAudioUnit);
        return ALC_INVALID_VALUE;
    }

    device->DeviceName = name;
    return ALC_NO_ERROR;
}

static ALCboolean ALCcoreAudioPlayback_reset(ALCcoreAudioPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    AudioStreamBasicDescription streamFormat;
    AURenderCallbackStruct input;
    OSStatus err;
    UInt32 size;

    err = AudioUnitUninitialize(self->mAudioUnit);
    if(err != noErr)
        ERR("-- AudioUnitUninitialize failed.\n");

    /* retrieve default output unit's properties (output side) */
    size = sizeof(AudioStreamBasicDescription);
    err = AudioUnitGetProperty(self->mAudioUnit, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Output, 0, &streamFormat, &size);
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
    err = AudioUnitSetProperty(self->mAudioUnit, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, 0, &streamFormat, size);
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_FALSE;
    }

    if(device->Frequency != streamFormat.mSampleRate)
    {
        device->NumUpdates = (ALuint)((ALuint64)device->NumUpdates *
                                      streamFormat.mSampleRate /
                                      device->Frequency);
        device->Frequency = streamFormat.mSampleRate;
    }

    /* FIXME: How to tell what channels are what in the output device, and how
     * to specify what we're giving?  eg, 6.0 vs 5.1 */
    switch(streamFormat.mChannelsPerFrame)
    {
        case 1:
            device->FmtChans = DevFmtMono;
            break;
        case 2:
            device->FmtChans = DevFmtStereo;
            break;
        case 4:
            device->FmtChans = DevFmtQuad;
            break;
        case 6:
            device->FmtChans = DevFmtX51;
            break;
        case 7:
            device->FmtChans = DevFmtX61;
            break;
        case 8:
            device->FmtChans = DevFmtX71;
            break;
        default:
            ERR("Unhandled channel count (%d), using Stereo\n", streamFormat.mChannelsPerFrame);
            device->FmtChans = DevFmtStereo;
            streamFormat.mChannelsPerFrame = 2;
            break;
    }
    SetDefaultWFXChannelOrder(device);

    /* use channel count and sample rate from the default output unit's current
     * parameters, but reset everything else */
    streamFormat.mFramesPerPacket = 1;
    streamFormat.mFormatFlags = 0;
    switch(device->FmtType)
    {
        case DevFmtUByte:
            device->FmtType = DevFmtByte;
            /* fall-through */
        case DevFmtByte:
            streamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
            streamFormat.mBitsPerChannel = 8;
            break;
        case DevFmtUShort:
            device->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            streamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
            streamFormat.mBitsPerChannel = 16;
            break;
        case DevFmtUInt:
            device->FmtType = DevFmtInt;
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

    err = AudioUnitSetProperty(self->mAudioUnit, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, 0, &streamFormat, sizeof(AudioStreamBasicDescription));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_FALSE;
    }

    /* setup callback */
    self->mFrameSize = device->frameSizeFromFmt();
    input.inputProc = ALCcoreAudioPlayback_MixerProc;
    input.inputProcRefCon = self;

    err = AudioUnitSetProperty(self->mAudioUnit, kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input, 0, &input, sizeof(AURenderCallbackStruct));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_FALSE;
    }

    /* init the default audio unit... */
    err = AudioUnitInitialize(self->mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioUnitInitialize failed\n");
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static ALCboolean ALCcoreAudioPlayback_start(ALCcoreAudioPlayback *self)
{
    OSStatus err = AudioOutputUnitStart(self->mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioOutputUnitStart failed\n");
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void ALCcoreAudioPlayback_stop(ALCcoreAudioPlayback *self)
{
    OSStatus err = AudioOutputUnitStop(self->mAudioUnit);
    if(err != noErr)
        ERR("AudioOutputUnitStop failed\n");
}


struct ALCcoreAudioCapture final : public ALCbackend {
    AudioUnit mAudioUnit{0};

    ALuint mFrameSize{0u};
    AudioStreamBasicDescription mFormat{};  // This is the OpenAL format as a CoreAudio ASBD

    SampleConverterPtr mConverter;

    RingBufferPtr mRing{nullptr};

    ALCcoreAudioCapture(ALCdevice *device) noexcept : ALCbackend{device} { }
};

static void ALCcoreAudioCapture_Construct(ALCcoreAudioCapture *self, ALCdevice *device);
static void ALCcoreAudioCapture_Destruct(ALCcoreAudioCapture *self);
static ALCenum ALCcoreAudioCapture_open(ALCcoreAudioCapture *self, const ALCchar *name);
static DECLARE_FORWARD(ALCcoreAudioCapture, ALCbackend, ALCboolean, reset)
static ALCboolean ALCcoreAudioCapture_start(ALCcoreAudioCapture *self);
static void ALCcoreAudioCapture_stop(ALCcoreAudioCapture *self);
static ALCenum ALCcoreAudioCapture_captureSamples(ALCcoreAudioCapture *self, ALCvoid *buffer, ALCuint samples);
static ALCuint ALCcoreAudioCapture_availableSamples(ALCcoreAudioCapture *self);
static DECLARE_FORWARD(ALCcoreAudioCapture, ALCbackend, ClockLatency, getClockLatency)
static DECLARE_FORWARD(ALCcoreAudioCapture, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCcoreAudioCapture, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCcoreAudioCapture)

DEFINE_ALCBACKEND_VTABLE(ALCcoreAudioCapture);


static void ALCcoreAudioCapture_Construct(ALCcoreAudioCapture *self, ALCdevice *device)
{
    new (self) ALCcoreAudioCapture{device};
    SET_VTABLE2(ALCcoreAudioCapture, ALCbackend, self);
}

static void ALCcoreAudioCapture_Destruct(ALCcoreAudioCapture *self)
{
    if(self->mAudioUnit)
        AudioComponentInstanceDispose(self->mAudioUnit);
    self->mAudioUnit = 0;

    self->~ALCcoreAudioCapture();
}


static OSStatus ALCcoreAudioCapture_RecordProc(void *inRefCon,
  AudioUnitRenderActionFlags* UNUSED(ioActionFlags),
  const AudioTimeStamp *inTimeStamp, UInt32 UNUSED(inBusNumber),
  UInt32 inNumberFrames, AudioBufferList* UNUSED(ioData))
{
    auto self = static_cast<ALCcoreAudioCapture*>(inRefCon);
    RingBuffer *ring{self->mRing.get()};
    AudioUnitRenderActionFlags flags = 0;
    union {
        ALbyte _[sizeof(AudioBufferList) + sizeof(AudioBuffer)];
        AudioBufferList list;
    } audiobuf = { { 0 } };
    OSStatus err;

    auto rec_vec = ring->getWriteVector();

    // Fill the ringbuffer's first segment with data from the input device
    size_t total_read{minz(rec_vec.first.len, inNumberFrames)};
    audiobuf.list.mNumberBuffers = 1;
    audiobuf.list.mBuffers[0].mNumberChannels = self->mFormat.mChannelsPerFrame;
    audiobuf.list.mBuffers[0].mData = rec_vec.first.buf;
    audiobuf.list.mBuffers[0].mDataByteSize = total_read * self->mFormat.mBytesPerFrame;
    err = AudioUnitRender(self->mAudioUnit, &flags, inTimeStamp, 1, inNumberFrames,
        &audiobuf.list);
    if(err == noErr && inNumberFrames > rec_vec.first.len && rec_vec.second.len > 0)
    {
        /* If there's still more to get and there's space in the ringbuffer's
         * second segment, fill that with data too.
         */
        const size_t remlen{inNumberFrames - rec_vec.first.len};
        const size_t toread{minz(rec_vec.second.len, remlen)};
        total_read += toread;

        audiobuf.list.mNumberBuffers = 1;
        audiobuf.list.mBuffers[0].mNumberChannels = self->mFormat.mChannelsPerFrame;
        audiobuf.list.mBuffers[0].mData = rec_vec.second.buf;
        audiobuf.list.mBuffers[0].mDataByteSize = toread * self->mFormat.mBytesPerFrame;
        err = AudioUnitRender(self->mAudioUnit, &flags, inTimeStamp, 1, inNumberFrames,
            &audiobuf.list);
    }
    if(err != noErr)
    {
        ERR("AudioUnitRender error: %d\n", err);
        return err;
    }

    ring->writeAdvance(total_read);
    return noErr;
}


static ALCenum ALCcoreAudioCapture_open(ALCcoreAudioCapture *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
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
    err = AudioComponentInstanceNew(comp, &self->mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioComponentInstanceNew failed\n");
        return ALC_INVALID_VALUE;
    }

    // Turn off AudioUnit output
    enableIO = 0;
    err = AudioUnitSetProperty(self->mAudioUnit, kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Output, 0, &enableIO, sizeof(ALuint));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_INVALID_VALUE;
    }

    // Turn on AudioUnit input
    enableIO = 1;
    err = AudioUnitSetProperty(self->mAudioUnit, kAudioOutputUnitProperty_EnableIO,
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
        err = AudioUnitSetProperty(self->mAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
            kAudioUnitScope_Global, 0, &inputDevice, sizeof(AudioDeviceID));
        if(err != noErr)
        {
            ERR("AudioUnitSetProperty failed\n");
            return ALC_INVALID_VALUE;
        }
    }
#endif

    // set capture callback
    input.inputProc = ALCcoreAudioCapture_RecordProc;
    input.inputProcRefCon = self;

    err = AudioUnitSetProperty(self->mAudioUnit, kAudioOutputUnitProperty_SetInputCallback,
        kAudioUnitScope_Global, 0, &input, sizeof(AURenderCallbackStruct));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_INVALID_VALUE;
    }

    // Initialize the device
    err = AudioUnitInitialize(self->mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioUnitInitialize failed\n");
        return ALC_INVALID_VALUE;
    }

    // Get the hardware format
    propertySize = sizeof(AudioStreamBasicDescription);
    err = AudioUnitGetProperty(self->mAudioUnit, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, 1, &hardwareFormat, &propertySize);
    if(err != noErr || propertySize != sizeof(AudioStreamBasicDescription))
    {
        ERR("AudioUnitGetProperty failed\n");
        return ALC_INVALID_VALUE;
    }

    // Set up the requested format description
    switch(device->FmtType)
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
            ERR("%s samples not supported\n", DevFmtTypeString(device->FmtType));
            return ALC_INVALID_VALUE;
    }

    switch(device->FmtChans)
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
            ERR("%s not supported\n", DevFmtChannelsString(device->FmtChans));
            return ALC_INVALID_VALUE;
    }

    requestedFormat.mBytesPerFrame = requestedFormat.mChannelsPerFrame * requestedFormat.mBitsPerChannel / 8;
    requestedFormat.mBytesPerPacket = requestedFormat.mBytesPerFrame;
    requestedFormat.mSampleRate = device->Frequency;
    requestedFormat.mFormatID = kAudioFormatLinearPCM;
    requestedFormat.mReserved = 0;
    requestedFormat.mFramesPerPacket = 1;

    // save requested format description for later use
    self->mFormat = requestedFormat;
    self->mFrameSize = device->frameSizeFromFmt();

    // Use intermediate format for sample rate conversion (outputFormat)
    // Set sample rate to the same as hardware for resampling later
    outputFormat = requestedFormat;
    outputFormat.mSampleRate = hardwareFormat.mSampleRate;

    // The output format should be the requested format, but using the hardware sample rate
    // This is because the AudioUnit will automatically scale other properties, except for sample rate
    err = AudioUnitSetProperty(self->mAudioUnit, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Output, 1, (void *)&outputFormat, sizeof(outputFormat));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed\n");
        return ALC_INVALID_VALUE;
    }

    // Set the AudioUnit output format frame count
    ALuint64 FrameCount64{device->UpdateSize};
    FrameCount64 = (FrameCount64*outputFormat.mSampleRate + device->Frequency-1) /
        device->Frequency;
    FrameCount64 += MAX_RESAMPLE_PADDING*2;
    if(FrameCount64 > std::numeric_limits<uint32_t>::max()/2)
    {
        ERR("FrameCount too large\n");
        return ALC_INVALID_VALUE;
    }

    outputFrameCount = static_cast<uint32_t>(FrameCount64);
    err = AudioUnitSetProperty(self->mAudioUnit, kAudioUnitProperty_MaximumFramesPerSlice,
        kAudioUnitScope_Output, 0, &outputFrameCount, sizeof(outputFrameCount));
    if(err != noErr)
    {
        ERR("AudioUnitSetProperty failed: %d\n", err);
        return ALC_INVALID_VALUE;
    }

    // Set up sample converter if needed
    if(outputFormat.mSampleRate != device->Frequency)
        self->mConverter = CreateSampleConverter(device->FmtType, device->FmtType,
            self->mFormat.mChannelsPerFrame, hardwareFormat.mSampleRate, device->Frequency,
            BSinc24Resampler);

    self->mRing = CreateRingBuffer(outputFrameCount, self->mFrameSize, false);
    if(!self->mRing) return ALC_INVALID_VALUE;

    device->DeviceName = name;
    return ALC_NO_ERROR;
}


static ALCboolean ALCcoreAudioCapture_start(ALCcoreAudioCapture *self)
{
    OSStatus err = AudioOutputUnitStart(self->mAudioUnit);
    if(err != noErr)
    {
        ERR("AudioOutputUnitStart failed\n");
        return ALC_FALSE;
    }
    return ALC_TRUE;
}

static void ALCcoreAudioCapture_stop(ALCcoreAudioCapture *self)
{
    OSStatus err = AudioOutputUnitStop(self->mAudioUnit);
    if(err != noErr)
        ERR("AudioOutputUnitStop failed\n");
}

static ALCenum ALCcoreAudioCapture_captureSamples(ALCcoreAudioCapture *self, ALCvoid *buffer, ALCuint samples)
{
    RingBuffer *ring{self->mRing.get()};

    if(!self->mConverter)
    {
        ring->read(buffer, samples);
        return ALC_NO_ERROR;
    }

    auto rec_vec = ring->getReadVector();
    const void *src0{rec_vec.first.buf};
    auto src0len = static_cast<ALsizei>(rec_vec.first.len);
    auto got = static_cast<ALuint>(SampleConverterInput(self->mConverter.get(), &src0, &src0len,
        buffer, samples));
    size_t total_read{rec_vec.first.len - src0len};
    if(got < samples && !src0len && rec_vec.second.len > 0)
    {
        const void *src1{rec_vec.second.buf};
        auto src1len = static_cast<ALsizei>(rec_vec.second.len);
        got += static_cast<ALuint>(SampleConverterInput(self->mConverter.get(), &src1, &src1len,
            static_cast<char*>(buffer)+got, samples-got));
        total_read += rec_vec.second.len - src1len;
    }

    ring->readAdvance(total_read);
    return ALC_NO_ERROR;
}

static ALCuint ALCcoreAudioCapture_availableSamples(ALCcoreAudioCapture *self)
{
    RingBuffer *ring{self->mRing.get()};

    if(!self->mConverter) return ring->readSpace();
    return SampleConverterAvailableOut(self->mConverter.get(), ring->readSpace());
}


BackendFactory &CoreAudioBackendFactory::getFactory()
{
    static CoreAudioBackendFactory factory{};
    return factory;
}

bool CoreAudioBackendFactory::init() { return true; }

bool CoreAudioBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback || ALCbackend_Capture); }

void CoreAudioBackendFactory::probe(DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
        case CAPTURE_DEVICE_PROBE:
            /* Includes null char. */
            outnames->append(ca_device, sizeof(ca_device));
            break;
    }
}

ALCbackend *CoreAudioBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCcoreAudioPlayback *backend;
        NEW_OBJ(backend, ALCcoreAudioPlayback)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }
    if(type == ALCbackend_Capture)
    {
        ALCcoreAudioCapture *backend;
        NEW_OBJ(backend, ALCcoreAudioCapture)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    return nullptr;
}
