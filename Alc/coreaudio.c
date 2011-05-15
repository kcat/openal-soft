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
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"

#include <CoreServices/CoreServices.h>
#include <unistd.h>
#include <AudioUnit/AudioUnit.h>

/* toggle verbose tty output among CoreAudio code */
#define CA_VERBOSE 1

typedef struct {
    AudioUnit OutputUnit;
    ALuint FrameSize;
} ca_data;

static const ALCchar ca_device[] = "CoreAudio Default";

static int ca_callback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp,
                       UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
    ALCdevice *device = (ALCdevice*)inRefCon;
    ca_data *data = (ca_data*)device->ExtraData;

    aluMixData(device, ioData->mBuffers[0].mData,
               ioData->mBuffers[0].mDataByteSize / data->FrameSize);

    return noErr;
}

static ALCboolean ca_open_playback(ALCdevice *device, const ALCchar *deviceName)
{
    ComponentDescription desc;
    Component comp;
    ca_data *data;
    OSStatus err;

    if(!deviceName)
        deviceName = ca_device;
    else if(strcmp(deviceName, ca_device) != 0)
        return ALC_FALSE;

    /* open the default output unit */
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    comp = FindNextComponent(NULL, &desc);
    if(comp == NULL)
    {
        AL_PRINT("FindNextComponent failed\n");
        return ALC_FALSE;
    }

    data = calloc(1, sizeof(*data));
    device->ExtraData = data;

    err = OpenAComponent(comp, &data->OutputUnit);
    if(err != noErr)
    {
        AL_PRINT("OpenAComponent failed\n");
        free(data);
        device->ExtraData = NULL;
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void ca_close_playback(ALCdevice *device)
{
    ca_data *data = (ca_data*)device->ExtraData;

    CloseComponent(data->OutputUnit);

    free(data);
    device->ExtraData = NULL;
}

static ALCboolean ca_reset_playback(ALCdevice *device)
{
    ca_data *data = (ca_data*)device->ExtraData;
    AudioStreamBasicDescription streamFormat;
    AURenderCallbackStruct input;
    OSStatus err;
    UInt32 size;

    /* init and start the default audio unit... */
    err = AudioUnitInitialize(data->OutputUnit);
    if(err != noErr)
    {
        AL_PRINT("AudioUnitInitialize failed\n");
        return ALC_FALSE;
    }

    err = AudioOutputUnitStart(data->OutputUnit);
    if(err != noErr)
    {
        AL_PRINT("AudioOutputUnitStart failed\n");
        return ALC_FALSE;
    }

    /* retrieve default output unit's properties (output side) */
    size = sizeof(AudioStreamBasicDescription);
    err = AudioUnitGetProperty(data->OutputUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &streamFormat, &size);
    if(err != noErr || size != sizeof(AudioStreamBasicDescription))
    {
        AL_PRINT("AudioUnitGetProperty failed\n");
        return ALC_FALSE;
    }

#if 0
    AL_PRINT("Output streamFormat of default output unit -\n");
    AL_PRINT("  streamFormat.mFramesPerPacket = %d\n", streamFormat.mFramesPerPacket);
    AL_PRINT("  streamFormat.mChannelsPerFrame = %d\n", streamFormat.mChannelsPerFrame);
    AL_PRINT("  streamFormat.mBitsPerChannel = %d\n", streamFormat.mBitsPerChannel);
    AL_PRINT("  streamFormat.mBytesPerPacket = %d\n", streamFormat.mBytesPerPacket);
    AL_PRINT("  streamFormat.mBytesPerFrame = %d\n", streamFormat.mBytesPerFrame);
    AL_PRINT("  streamFormat.mSampleRate = %5.0f\n", streamFormat.mSampleRate);
#endif

    /* set default output unit's input side to match output side */
    err = AudioUnitSetProperty(data->OutputUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &streamFormat, size);
    if(err != noErr)
    {
        AL_PRINT("AudioUnitSetProperty failed\n");
        return ALC_FALSE;
    }

    if(device->Frequency != streamFormat.mSampleRate)
    {
        if((device->Flags&DEVICE_FREQUENCY_REQUEST))
            AL_PRINT("CoreAudio does not support changing sample rates (wanted %dhz, got %dhz)\n", device->Frequency, streamFormat.mSampleRate);
        device->Flags &= ~DEVICE_FREQUENCY_REQUEST;

        device->UpdateSize = (ALuint)((ALuint64)device->UpdateSize *
                                      streamFormat.mSampleRate /
                                      device->Frequency);
        device->Frequency = streamFormat.mSampleRate;
    }

    /* FIXME: How to tell what channels are what in the output device, and how
     * to specify what we're giving?  eg, 6.0 vs 5.1 */
    switch(streamFormat.mChannelsPerFrame)
    {
        case 1:
            if((device->Flags&DEVICE_CHANNELS_REQUEST) &&
               device->FmtChans != DevFmtMono)
            {
                AL_PRINT("Failed to set %s, got Mono instead\n", DevFmtChannelsString(device->FmtChans));
                device->Flags &= ~DEVICE_CHANNELS_REQUEST;
            }
            device->FmtChans = DevFmtMono;
            break;
        case 2:
            if((device->Flags&DEVICE_CHANNELS_REQUEST) &&
               device->FmtChans != DevFmtStereo)
            {
                AL_PRINT("Failed to set %s, got Stereo instead\n", DevFmtChannelsString(device->FmtChans));
                device->Flags &= ~DEVICE_CHANNELS_REQUEST;
            }
            device->FmtChans = DevFmtStereo;
            break;
        case 4:
            if((device->Flags&DEVICE_CHANNELS_REQUEST) &&
               device->FmtChans != DevFmtQuad)
            {
                AL_PRINT("Failed to set %s, got Quad instead\n", DevFmtChannelsString(device->FmtChans));
                device->Flags &= ~DEVICE_CHANNELS_REQUEST;
            }
            device->FmtChans = DevFmtQuad;
            break;
        case 6:
            if((device->Flags&DEVICE_CHANNELS_REQUEST) &&
               device->FmtChans != DevFmtX51)
            {
                AL_PRINT("Failed to set %s, got 5.1 Surround instead\n", DevFmtChannelsString(device->FmtChans));
                device->Flags &= ~DEVICE_CHANNELS_REQUEST;
            }
            device->FmtChans = DevFmtX51;
            break;
        case 7:
            if((device->Flags&DEVICE_CHANNELS_REQUEST) &&
               device->FmtChans != DevFmtX61)
            {
                AL_PRINT("Failed to set %s, got 6.1 Surround instead\n", DevFmtChannelsString(device->FmtChans));
                device->Flags &= ~DEVICE_CHANNELS_REQUEST;
            }
            device->FmtChans = DevFmtX61;
            break;
        case 8:
            if((device->Flags&DEVICE_CHANNELS_REQUEST) &&
               device->FmtChans != DevFmtX71)
            {
                AL_PRINT("Failed to set %s, got 7.1 Surround instead\n", DevFmtChannelsString(device->FmtChans));
                device->Flags &= ~DEVICE_CHANNELS_REQUEST;
            }
            device->FmtChans = DevFmtX71;
            break;
        default:
            AL_PRINT("Unhandled channel count (%d), using Stereo\n", streamFormat.mChannelsPerFrame);
            device->Flags &= ~DEVICE_CHANNELS_REQUEST;
            device->FmtChans = DevFmtStereo;
            streamFormat.mChannelsPerFrame = 2;
            break;
    }
    SetDefaultWFXChannelOrder(device);

    /* use channel count and sample rate from the default output unit's current
     * parameters, but reset everything else */
    streamFormat.mFramesPerPacket = 1;
    switch(device->FmtType)
    {
        case DevFmtUByte:
            device->FmtType = DevFmtByte;
            /* fall-through */
        case DevFmtByte:
            streamFormat.mBitsPerChannel = 8;
            streamFormat.mBytesPerPacket = streamFormat.mChannelsPerFrame;
            streamFormat.mBytesPerFrame = streamFormat.mChannelsPerFrame;
            break;
        case DevFmtUShort:
        case DevFmtFloat:
            device->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            streamFormat.mBitsPerChannel = 16;
            streamFormat.mBytesPerPacket = 2 * streamFormat.mChannelsPerFrame;
            streamFormat.mBytesPerFrame = 2 * streamFormat.mChannelsPerFrame;
            break;
    }
    streamFormat.mFormatID = kAudioFormatLinearPCM;
    streamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                                kAudioFormatFlagsNativeEndian |
                                kLinearPCMFormatFlagIsPacked;

    err = AudioUnitSetProperty(data->OutputUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &streamFormat, sizeof(AudioStreamBasicDescription));
    if(err != noErr)
    {
        AL_PRINT("AudioUnitSetProperty failed\n");
        return ALC_FALSE;
    }

    /* setup callback */
    data->FrameSize = FrameSizeFromDevFmt(device->FmtChans, device->FmtType);
    input.inputProc = ca_callback;
    input.inputProcRefCon = device;

    err = AudioUnitSetProperty(data->OutputUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &input, sizeof(AURenderCallbackStruct));
    if(err != noErr)
    {
        AL_PRINT("AudioUnitSetProperty failed\n");
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void ca_stop_playback(ALCdevice *device)
{
    ca_data *data = (ca_data*)device->ExtraData;
    OSStatus err;

    AudioOutputUnitStop(data->OutputUnit);
    err = AudioUnitUninitialize(data->OutputUnit);
    if(err != noErr)
        AL_PRINT("-- AudioUnitUninitialize failed.\n");
}

static ALCboolean ca_open_capture(ALCdevice *device, const ALCchar *deviceName)
{
    return ALC_FALSE;
    (void)device;
    (void)deviceName;
}

static const BackendFuncs ca_funcs = {
    ca_open_playback,
    ca_close_playback,
    ca_reset_playback,
    ca_stop_playback,
    ca_open_capture,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

void alc_ca_init(BackendFuncs *func_list)
{
    *func_list = ca_funcs;
}

void alc_ca_deinit(void)
{
}

void alc_ca_probe(int type)
{
    if(type == DEVICE_PROBE)
        AppendDeviceList(ca_device);
    else if(type == ALL_DEVICE_PROBE)
        AppendAllDeviceList(ca_device);
}
