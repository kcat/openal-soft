/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by authors.
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

#define COBJMACROS
#define _WIN32_WINNT 0x0500
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cguid.h>
#include <mmreg.h>
#ifndef _WAVEFORMATEXTENSIBLE_
#include <ks.h>
#include <ksmedia.h>
#endif

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"


DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

#define MONO SPEAKER_FRONT_CENTER
#define STEREO (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)
#define QUAD (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X5DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X5DOT1SIDE (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X6DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_CENTER|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)


static IMMDeviceEnumerator *Enumerator = NULL;


typedef struct {
    IMMDevice *mmdev;
    IAudioClient *client;

    volatile int killNow;
    ALvoid *thread;
} MMDevApiData;


static const ALCchar mmDevice[] = "WASAPI Default";


static ALCboolean MakeExtensible(WAVEFORMATEXTENSIBLE *out, const WAVEFORMATEX *in)
{
    memset(out, 0, sizeof(*out));
    if(in->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        *out = *(WAVEFORMATEXTENSIBLE*)in;
    else if(in->wFormatTag == WAVE_FORMAT_PCM)
    {
        out->Format = *in;
        out->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        out->Format.cbSize = sizeof(*out) - sizeof(*in);
        if(out->Format.nChannels == 1)
            out->dwChannelMask = MONO;
        else if(out->Format.nChannels == 2)
            out->dwChannelMask = STEREO;
        else
            ERROR("Unhandled PCM channel count: %d\n", out->Format.nChannels);
        out->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }
    else if(in->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        out->Format = *in;
        out->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        out->Format.cbSize = sizeof(*out) - sizeof(*in);
        if(out->Format.nChannels == 1)
            out->dwChannelMask = MONO;
        else if(out->Format.nChannels == 2)
            out->dwChannelMask = STEREO;
        else
            ERROR("Unhandled IEEE float channel count: %d\n", out->Format.nChannels);
        out->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    else
    {
        ERROR("Unhandled format tag: 0x%04x\n", in->wFormatTag);
        return ALC_FALSE;
    }
    return ALC_TRUE;
}


static void *MMDevApiLoad(void)
{
    if(!Enumerator)
    {
        void *mme = NULL;
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if(FAILED(hr))
        {
            WARN("Failed to initialize apartment-threaded COM: 0x%08lx\n", hr);
            hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            if(FAILED(hr))
                WARN("Failed to initialize multi-threaded COM: 0x%08lx\n", hr);
        }
        if(SUCCEEDED(hr))
        {
            hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, &mme);
            if(SUCCEEDED(hr))
                Enumerator = mme;
            else
            {
                WARN("Failed to create IMMDeviceEnumerator instance: 0x%08lx\n", hr);
                CoUninitialize();
            }
        }
    }
    return Enumerator;
}


static ALuint MMDevApiProc(ALvoid *ptr)
{
    ALCdevice *device = ptr;
    MMDevApiData *data = device->ExtraData;
    union {
        IAudioRenderClient *iface;
        void *ptr;
    } render;
    UINT32 written, len;
    BYTE *buffer;
    HRESULT hr;

    hr = IAudioClient_GetService(data->client, &IID_IAudioRenderClient, &render.ptr);
    if(FAILED(hr))
    {
        ERROR("Failed to get AudioRenderClient service: 0x%08lx\n", hr);
        aluHandleDisconnect(device);
        return 0;
    }

    SetRTPriority();

    while(!data->killNow)
    {
        hr = IAudioClient_GetCurrentPadding(data->client, &written);
        if(FAILED(hr))
        {
            ERROR("Failed to get padding: 0x%08lx\n", hr);
            aluHandleDisconnect(device);
            break;
        }

        len = device->UpdateSize*device->NumUpdates - written;
        if(len < device->UpdateSize)
        {
            Sleep(10);
            continue;
        }
        len -= len%device->UpdateSize;

        hr = IAudioRenderClient_GetBuffer(render.iface, len, &buffer);
        if(SUCCEEDED(hr))
        {
            aluMixData(device, buffer, len);
            hr = IAudioRenderClient_ReleaseBuffer(render.iface, len, 0);
        }
        if(FAILED(hr))
        {
            ERROR("Failed to buffer data: 0x%08lx\n", hr);
            aluHandleDisconnect(device);
            break;
        }
    }

    IAudioRenderClient_Release(render.iface);
    return 0;
}


static ALCboolean MMDevApiOpenPlayback(ALCdevice *device, const ALCchar *deviceName)
{
    MMDevApiData *data = NULL;
    void *client = NULL;
    HRESULT hr;

    if(!MMDevApiLoad())
        return ALC_FALSE;

    if(!deviceName)
        deviceName = mmDevice;
    else if(strcmp(deviceName, mmDevice) != 0)
        return ALC_FALSE;

    //Initialise requested device
    data = calloc(1, sizeof(MMDevApiData));
    if(!data)
    {
        alcSetError(device, ALC_OUT_OF_MEMORY);
        return ALC_FALSE;
    }

    //MMDevApi Init code
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, eRender, eMultimedia, &data->mmdev);
    if(SUCCEEDED(hr))
        hr = IMMDevice_Activate(data->mmdev, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, &client);

    if(FAILED(hr))
    {
        if(data->mmdev)
            IMMDevice_Release(data->mmdev);
        data->mmdev = NULL;
        free(data);

        ERROR("Device init failed: 0x%08lx\n", hr);
        return ALC_FALSE;
    }
    data->client = client;

    device->szDeviceName = strdup(deviceName);
    device->ExtraData = data;
    return ALC_TRUE;
}

static void MMDevApiClosePlayback(ALCdevice *device)
{
    MMDevApiData *data = device->ExtraData;

    IAudioClient_Release(data->client);
    data->client = NULL;

    IMMDevice_Release(data->mmdev);
    data->mmdev = NULL;

    free(data);
    device->ExtraData = NULL;
}

static ALCboolean MMDevApiResetPlayback(ALCdevice *device)
{
    MMDevApiData *data = device->ExtraData;
    WAVEFORMATEXTENSIBLE OutputType;
    WAVEFORMATEX *wfx = NULL;
    REFERENCE_TIME def_per;
    UINT32 buffer_len;
    HRESULT hr;

    hr = IAudioClient_GetMixFormat(data->client, &wfx);
    if(FAILED(hr))
    {
        ERROR("Failed to get mix format: 0x%08lx\n", hr);
        return ALC_FALSE;
    }

    if(!MakeExtensible(&OutputType, wfx))
    {
        CoTaskMemFree(wfx);
        return ALC_FALSE;
    }
    CoTaskMemFree(wfx);
    wfx = NULL;

    if(!(device->Flags&DEVICE_FREQUENCY_REQUEST))
        device->Frequency = OutputType.Format.nSamplesPerSec;
    if(!(device->Flags&DEVICE_CHANNELS_REQUEST))
    {
        if(OutputType.Format.nChannels == 1 && OutputType.dwChannelMask == MONO)
            device->FmtChans = DevFmtMono;
        else if(OutputType.Format.nChannels == 2 && OutputType.dwChannelMask == STEREO)
            device->FmtChans = DevFmtStereo;
        else if(OutputType.Format.nChannels == 4 && OutputType.dwChannelMask == QUAD)
            device->FmtChans = DevFmtQuad;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1)
            device->FmtChans = DevFmtX51;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1SIDE)
            device->FmtChans = DevFmtX51Side;
        else if(OutputType.Format.nChannels == 7 && OutputType.dwChannelMask == X6DOT1)
            device->FmtChans = DevFmtX61;
        else if(OutputType.Format.nChannels == 8 && OutputType.dwChannelMask == X7DOT1)
            device->FmtChans = DevFmtX71;
        else
            ERROR("Unhandled channel config: %d -- 0x%08x\n", OutputType.Format.nChannels, OutputType.dwChannelMask);
    }

    switch(device->FmtChans)
    {
        case DevFmtMono:
            OutputType.Format.nChannels = 1;
            OutputType.dwChannelMask = MONO;
            break;
        case DevFmtStereo:
            OutputType.Format.nChannels = 2;
            OutputType.dwChannelMask = STEREO;
            break;
        case DevFmtQuad:
            OutputType.Format.nChannels = 4;
            OutputType.dwChannelMask = QUAD;
            break;
        case DevFmtX51:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1;
            break;
        case DevFmtX51Side:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1SIDE;
            break;
        case DevFmtX61:
            OutputType.Format.nChannels = 7;
            OutputType.dwChannelMask = X6DOT1;
            break;
        case DevFmtX71:
            OutputType.Format.nChannels = 8;
            OutputType.dwChannelMask = X7DOT1;
            break;
    }
    switch(device->FmtType)
    {
        case DevFmtByte:
            device->FmtType = DevFmtUByte;
            /* fall-through */
        case DevFmtUByte:
            OutputType.Format.wBitsPerSample = 8;
            OutputType.Samples.wValidBitsPerSample = 8;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtUShort:
            device->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            OutputType.Format.wBitsPerSample = 16;
            OutputType.Samples.wValidBitsPerSample = 16;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtFloat:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.Samples.wValidBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            break;
    }
    OutputType.Format.nSamplesPerSec = device->Frequency;

    OutputType.Format.nBlockAlign = OutputType.Format.nChannels *
                                    OutputType.Format.wBitsPerSample / 8;
    OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec *
                                        OutputType.Format.nBlockAlign;

    hr = IAudioClient_IsFormatSupported(data->client, AUDCLNT_SHAREMODE_SHARED, &OutputType.Format, &wfx);
    if(FAILED(hr))
    {
        ERROR("Failed to check format support: 0x%08lx\n", hr);
        hr = IAudioClient_GetMixFormat(data->client, &wfx);
    }
    if(FAILED(hr))
    {
        ERROR("Failed to find a supported format: 0x%08lx\n", hr);
        return ALC_FALSE;
    }

    if(wfx != NULL)
    {
        if(!MakeExtensible(&OutputType, wfx))
        {
            CoTaskMemFree(wfx);
            return ALC_FALSE;
        }
        CoTaskMemFree(wfx);
        wfx = NULL;

        if(device->Frequency != OutputType.Format.nSamplesPerSec)
        {
            if((device->Flags&DEVICE_FREQUENCY_REQUEST))
                ERROR("Failed to set %dhz, got %dhz instead\n", device->Frequency, OutputType.Format.nSamplesPerSec);
            device->Flags &= ~DEVICE_FREQUENCY_REQUEST;
            device->Frequency = OutputType.Format.nSamplesPerSec;
        }

        if(!((device->FmtChans == DevFmtMono && OutputType.Format.nChannels == 1 && OutputType.dwChannelMask == MONO) ||
             (device->FmtChans == DevFmtStereo && OutputType.Format.nChannels == 2 && OutputType.dwChannelMask == STEREO) ||
             (device->FmtChans == DevFmtQuad && OutputType.Format.nChannels == 4 && OutputType.dwChannelMask == QUAD) ||
             (device->FmtChans == DevFmtX51 && OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1) ||
             (device->FmtChans == DevFmtX51Side && OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1SIDE) ||
             (device->FmtChans == DevFmtX61 && OutputType.Format.nChannels == 7 && OutputType.dwChannelMask == X6DOT1) ||
             (device->FmtChans == DevFmtX71 && OutputType.Format.nChannels == 8 && OutputType.dwChannelMask == X7DOT1)))
        {
            if((device->Flags&DEVICE_CHANNELS_REQUEST))
                ERROR("Failed to set %s, got %d channels (0x%08x) instead\n", DevFmtChannelsString(device->FmtChans), OutputType.Format.nChannels, OutputType.dwChannelMask);
            device->Flags &= ~DEVICE_CHANNELS_REQUEST;

            if(OutputType.Format.nChannels == 1 && OutputType.dwChannelMask == MONO)
                device->FmtChans = DevFmtMono;
            else if(OutputType.Format.nChannels == 2 && OutputType.dwChannelMask == STEREO)
                device->FmtChans = DevFmtStereo;
            else if(OutputType.Format.nChannels == 4 && OutputType.dwChannelMask == QUAD)
                device->FmtChans = DevFmtQuad;
            else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1)
                device->FmtChans = DevFmtX51;
            else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1SIDE)
                device->FmtChans = DevFmtX51Side;
            else if(OutputType.Format.nChannels == 7 && OutputType.dwChannelMask == X6DOT1)
                device->FmtChans = DevFmtX61;
            else if(OutputType.Format.nChannels == 8 && OutputType.dwChannelMask == X7DOT1)
                device->FmtChans = DevFmtX71;
            else
            {
                ERROR("Unhandled extensible channels: %d -- 0x%08x\n", OutputType.Format.nChannels, OutputType.dwChannelMask);
                device->FmtChans = DevFmtStereo;
                OutputType.Format.nChannels = 2;
                OutputType.dwChannelMask = STEREO;
            }
        }

        if(IsEqualGUID(&OutputType.SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
        {
            if(OutputType.Samples.wValidBitsPerSample == 0)
                OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
            if(OutputType.Samples.wValidBitsPerSample != OutputType.Format.wBitsPerSample ||
               !((device->FmtType == DevFmtUByte && OutputType.Format.wBitsPerSample == 8) ||
                 (device->FmtType == DevFmtShort && OutputType.Format.wBitsPerSample == 16)))
            {
                ERROR("Failed to set %s samples, got %d/%d-bit instead\n", DevFmtTypeString(device->FmtType), OutputType.Samples.wValidBitsPerSample, OutputType.Format.wBitsPerSample);
                if(OutputType.Format.wBitsPerSample == 8)
                    device->FmtType = DevFmtUByte;
                else if(OutputType.Format.wBitsPerSample == 16)
                    device->FmtType = DevFmtShort;
                else
                {
                    device->FmtType = DevFmtShort;
                    OutputType.Format.wBitsPerSample = 16;
                }
                OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
            }
        }
        else if(IsEqualGUID(&OutputType.SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            if(OutputType.Samples.wValidBitsPerSample == 0)
                OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
            if(OutputType.Samples.wValidBitsPerSample != OutputType.Format.wBitsPerSample ||
               !((device->FmtType == DevFmtFloat && OutputType.Format.wBitsPerSample == 32)))
            {
                ERROR("Failed to set %s samples, got %d/%d-bit instead\n", DevFmtTypeString(device->FmtType), OutputType.Samples.wValidBitsPerSample, OutputType.Format.wBitsPerSample);
                if(OutputType.Format.wBitsPerSample != 32)
                {
                    device->FmtType = DevFmtFloat;
                    OutputType.Format.wBitsPerSample = 32;
                }
                OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
            }
        }
        else
        {
            ERROR("Unhandled format sub-type\n");
            device->FmtType = DevFmtShort;
            OutputType.Format.wBitsPerSample = 16;
            OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        }
    }

    SetDefaultWFXChannelOrder(device);


    hr = IAudioClient_Initialize(data->client, AUDCLNT_SHAREMODE_SHARED, 0,
                                 (ALuint64)device->UpdateSize * 10000000 /
                                 device->Frequency * device->NumUpdates,
                                 0, &OutputType.Format, NULL);
    if(FAILED(hr))
    {
        ERROR("Failed to initialize audio client: 0x%08lx\n", hr);
        return ALC_FALSE;
    }

    hr = IAudioClient_GetDevicePeriod(data->client, &def_per, NULL);
    if(SUCCEEDED(hr))
        hr = IAudioClient_GetBufferSize(data->client, &buffer_len);
    if(FAILED(hr))
    {
        ERROR("Failed to get audio buffer info: 0x%08lx\n", hr);
        return ALC_FALSE;
    }

    device->NumUpdates = (ALuint)((REFERENCE_TIME)buffer_len * 10000000 /
                                  device->Frequency / def_per);
    if(device->NumUpdates <= 1)
    {
        device->NumUpdates = 1;
        ERROR("Audio client returned default_period > buffer_len/2; expect break up\n");
    }
    device->UpdateSize = buffer_len / device->NumUpdates;

    hr = IAudioClient_Start(data->client);
    if(FAILED(hr))
    {
        ERROR("Failed to start audio client: 0x%08lx\n", hr);
        return ALC_FALSE;
    }

    data->thread = StartThread(MMDevApiProc, device);
    if(!data->thread)
    {
        IAudioClient_Stop(data->client);
        ERROR("Failed to start thread\n");
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void MMDevApiStopPlayback(ALCdevice *device)
{
    MMDevApiData *data = device->ExtraData;

    if(!data->thread)
        return;

    data->killNow = 1;
    StopThread(data->thread);
    data->thread = NULL;

    data->killNow = 0;

    IAudioClient_Stop(data->client);
}


static ALCboolean MMDevApiOpenCapture(ALCdevice *device, const ALCchar *deviceName)
{
    (void)device;
    (void)deviceName;
    return ALC_FALSE;
}


static const BackendFuncs MMDevApiFuncs = {
    MMDevApiOpenPlayback,
    MMDevApiClosePlayback,
    MMDevApiResetPlayback,
    MMDevApiStopPlayback,
    MMDevApiOpenCapture,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


void alcMMDevApiInit(BackendFuncs *FuncList)
{
    *FuncList = MMDevApiFuncs;
}

void alcMMDevApiDeinit(void)
{
    if(Enumerator)
    {
        IMMDeviceEnumerator_Release(Enumerator);
        Enumerator = NULL;
        CoUninitialize();
    }
}

void alcMMDevApiProbe(enum DevProbe type)
{
    if(!MMDevApiLoad()) return;

    switch(type)
    {
        case DEVICE_PROBE:
            AppendDeviceList(mmDevice);
            break;
        case ALL_DEVICE_PROBE:
            AppendAllDeviceList(mmDevice);
            break;
        case CAPTURE_DEVICE_PROBE:
            break;
    }
}
