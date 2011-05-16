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


static void *MMDevApiLoad(void)
{
    if(!Enumerator)
    {
        void *mme = NULL;
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if(SUCCEEDED(hr))
        {
            hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, &mme);
            if(FAILED(hr))
                CoUninitialize();
            else
                Enumerator = mme;
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
        AL_PRINT("Failed to get AudioRenderClient service: 0x%08lx\n", hr);
        aluHandleDisconnect(device);
        return 0;
    }

    SetRTPriority();

    while(!data->killNow)
    {
        hr = IAudioClient_GetCurrentPadding(data->client, &written);
        if(FAILED(hr))
        {
            AL_PRINT("Failed to get padding: 0x%08lx\n", hr);
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
            AL_PRINT("Failed to buffer data: 0x%08lx\n", hr);
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

        AL_PRINT("Device init failed: 0x%08lx\n", hr);
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
    HRESULT hr;

    hr = IAudioClient_GetMixFormat(data->client, &wfx);
    if(FAILED(hr))
    {
        AL_PRINT("Failed to get mix format: 0x%08lx\n", hr);
        return ALC_FALSE;
    }

    if(!(device->Flags&DEVICE_FREQUENCY_REQUEST))
        device->Frequency = wfx->nSamplesPerSec;
    if(!(device->Flags&DEVICE_CHANNELS_REQUEST))
    {
        if(wfx->wFormatTag == WAVE_FORMAT_PCM)
        {
            if(wfx->nChannels == 1)
                device->FmtChans = DevFmtMono;
            else if(wfx->nChannels == 2)
                device->FmtChans = DevFmtStereo;
            else
            {
                AL_PRINT("Unhandled PCM channels: %d\n", wfx->nChannels);
                device->FmtChans = DevFmtStereo;
            }
        }
        else if(wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            WAVEFORMATEXTENSIBLE *wfe = (WAVEFORMATEXTENSIBLE*)wfx;

            if(wfe->Format.nChannels == 1 && wfe->dwChannelMask == MONO)
                device->FmtChans = DevFmtMono;
            else if(wfe->Format.nChannels == 2 && wfe->dwChannelMask == STEREO)
                device->FmtChans = DevFmtStereo;
            else if(wfe->Format.nChannels == 4 && wfe->dwChannelMask == QUAD)
                device->FmtChans = DevFmtQuad;
            else if(wfe->Format.nChannels == 6 && wfe->dwChannelMask == X5DOT1)
                device->FmtChans = DevFmtX51;
            else if(wfe->Format.nChannels == 7 && wfe->dwChannelMask == X6DOT1)
                device->FmtChans = DevFmtX61;
            else if(wfe->Format.nChannels == 8 && wfe->dwChannelMask == X7DOT1)
                device->FmtChans = DevFmtX71;
            else
            {
                AL_PRINT("Unhandled extensible channels: %d -- 0x%08lx\n", wfe->Format.nChannels, wfe->dwChannelMask);
                device->FmtChans = DevFmtStereo;
            }
        }
    }

    if(wfx->wFormatTag == WAVE_FORMAT_PCM)
    {
        if(wfx->wBitsPerSample == 8)
            device->FmtType = DevFmtUByte;
        else if(wfx->wBitsPerSample == 16)
            device->FmtType = DevFmtShort;
    }
    else if(wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        WAVEFORMATEXTENSIBLE *wfe = (WAVEFORMATEXTENSIBLE*)wfx;

        if(IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
        {
            if(wfx->wBitsPerSample == 8)
                device->FmtType = DevFmtUByte;
            else if(wfx->wBitsPerSample == 16)
                device->FmtType = DevFmtShort;
        }
        else if(IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            device->FmtType = DevFmtFloat;
    }

    CoTaskMemFree(wfx);
    wfx = NULL;

    SetDefaultWFXChannelOrder(device);

    memset(&OutputType, 0, sizeof(OutputType));
    OutputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;

    switch(device->FmtType)
    {
        case DevFmtByte:
            device->FmtType = DevFmtUByte;
            /* fall-through */
        case DevFmtUByte:
            OutputType.Format.wBitsPerSample = 8;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtUShort:
            device->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            OutputType.Format.wBitsPerSample = 16;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtFloat:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            break;
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
        case DevFmtX61:
            OutputType.Format.nChannels = 7;
            OutputType.dwChannelMask = X6DOT1;
            break;
        case DevFmtX71:
            OutputType.Format.nChannels = 8;
            OutputType.dwChannelMask = X7DOT1;
            break;
    }

    OutputType.Format.nBlockAlign = OutputType.Format.nChannels*OutputType.Format.wBitsPerSample/8;
    OutputType.Format.nSamplesPerSec = device->Frequency;
    OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec*OutputType.Format.nBlockAlign;
    OutputType.Format.cbSize = sizeof(OutputType) - sizeof(OutputType.Format);


    hr = IAudioClient_Initialize(data->client, AUDCLNT_SHAREMODE_SHARED, 0,
                                 (ALuint64)device->UpdateSize * 10000000 /
                                 device->Frequency * device->NumUpdates,
                                 0, &OutputType.Format, NULL);
    if(FAILED(hr))
    {
        AL_PRINT("Failed to initialize audio client: 0x%08lx\n", hr);
        return ALC_FALSE;
    }


    hr = IAudioClient_Start(data->client);
    if(FAILED(hr))
    {
        AL_PRINT("Failed to start audio client\n");
        return ALC_FALSE;
    }

    data->thread = StartThread(MMDevApiProc, device);
    if(!data->thread)
    {
        IAudioClient_Stop(data->client);
        AL_PRINT("Failed to start thread\n");
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

void alcMMDevApiProbe(int type)
{
    if(!MMDevApiLoad()) return;

    if(type == DEVICE_PROBE)
        AppendDeviceList(mmDevice);
    else if(type == ALL_DEVICE_PROBE)
        AppendAllDeviceList(mmDevice);
}
