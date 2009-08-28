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

#define _WIN32_WINNT 0x0500
#define INITGUID
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <dsound.h>
#include <mmreg.h>
#ifndef _WAVEFORMATEXTENSIBLE_
#include <ks.h>
#include <ksmedia.h>
#endif

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"

#ifndef DSSPEAKER_5POINT1
#define DSSPEAKER_5POINT1       6
#endif
#ifndef DSSPEAKER_7POINT1
#define DSSPEAKER_7POINT1       7
#endif

DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

static void *ds_handle;
static HRESULT (WINAPI *pDirectSoundCreate)(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter);
static HRESULT (WINAPI *pDirectSoundEnumerateA)(LPDSENUMCALLBACKA pDSEnumCallback, LPVOID pContext);

// Since DSound doesn't report the fragment size, emulate it
static int num_frags;

typedef struct {
    // DirectSound Playback Device
    LPDIRECTSOUND          lpDS;
    LPDIRECTSOUNDBUFFER    DSpbuffer;
    LPDIRECTSOUNDBUFFER    DSsbuffer;

    volatile int killNow;
    ALvoid *thread;
} DSoundData;


typedef struct {
    ALCchar *name;
    GUID guid;
} DevMap;

static const ALCchar dsDevice[] = "DirectSound Software";
static DevMap *DeviceList;
static ALuint NumDevices;


static ALuint DSoundProc(ALvoid *ptr)
{
    ALCdevice *pDevice = (ALCdevice*)ptr;
    DSoundData *pData = (DSoundData*)pDevice->ExtraData;
    DSBCAPS DSBCaps;
    DWORD LastCursor = 0;
    DWORD PlayCursor;
    VOID *WritePtr1, *WritePtr2;
    DWORD WriteCnt1,  WriteCnt2;
    DWORD FragSize;
    DWORD avail;
    HRESULT err;

    memset(&DSBCaps, 0, sizeof(DSBCaps));
    DSBCaps.dwSize = sizeof(DSBCaps);
    err = IDirectSoundBuffer_GetCaps(pData->DSsbuffer, &DSBCaps);
    if(FAILED(err))
    {
        AL_PRINT("Failed to get buffer caps: 0x%lx\n", err);
        return 1;
    }
    FragSize = DSBCaps.dwBufferBytes / num_frags;

    IDirectSoundBuffer_GetCurrentPosition(pData->DSsbuffer, &LastCursor, NULL);
    while(!pData->killNow)
    {
        // Get current play and write cursors
        IDirectSoundBuffer_GetCurrentPosition(pData->DSsbuffer, &PlayCursor, NULL);
        avail = (PlayCursor-LastCursor+DSBCaps.dwBufferBytes) % DSBCaps.dwBufferBytes;

        if(avail < FragSize)
        {
            Sleep(1);
            continue;
        }
        avail -= avail%FragSize;

        // Lock output buffer
        WriteCnt1 = 0;
        WriteCnt2 = 0;
        err = IDirectSoundBuffer_Lock(pData->DSsbuffer, LastCursor, avail, &WritePtr1, &WriteCnt1, &WritePtr2, &WriteCnt2, 0);

        // If the buffer is lost, restore it, play and lock
        if(err == DSERR_BUFFERLOST)
        {
            err = IDirectSoundBuffer_Restore(pData->DSsbuffer);
            if(SUCCEEDED(err))
                err = IDirectSoundBuffer_Play(pData->DSsbuffer, 0, 0, DSBPLAY_LOOPING);
            if(SUCCEEDED(err))
                err = IDirectSoundBuffer_Lock(pData->DSsbuffer, LastCursor, avail, &WritePtr1, &WriteCnt1, &WritePtr2, &WriteCnt2, 0);
        }

        // Successfully locked the output buffer
        if(SUCCEEDED(err))
        {
            // If we have an active context, mix data directly into output buffer otherwise fill with silence
            SuspendContext(NULL);
            aluMixData(pDevice->Context, WritePtr1, WriteCnt1, pDevice->Format);
            aluMixData(pDevice->Context, WritePtr2, WriteCnt2, pDevice->Format);
            ProcessContext(NULL);

            // Unlock output buffer only when successfully locked
            IDirectSoundBuffer_Unlock(pData->DSsbuffer, WritePtr1, WriteCnt1, WritePtr2, WriteCnt2);
        }
        else
            AL_PRINT("Buffer lock error: %#lx\n", err);

        // Update old write cursor location
        LastCursor += WriteCnt1+WriteCnt2;
        LastCursor %= DSBCaps.dwBufferBytes;
    }

    return 0;
}

static ALCboolean DSoundOpenPlayback(ALCdevice *device, const ALCchar *deviceName)
{
    DSoundData *pData = NULL;
    LPGUID guid = NULL;
    HRESULT hr;

    if(ds_handle == NULL)
        return ALC_FALSE;

    if(!deviceName)
        deviceName = dsDevice;
    else if(strcmp(deviceName, dsDevice) != 0)
    {
        ALuint i;
        for(i = 0;i < NumDevices;i++)
        {
            if(strcmp(deviceName, DeviceList[i].name) == 0)
            {
                guid = &DeviceList[i].guid;
                break;
            }
        }
        if(i == NumDevices)
            return ALC_FALSE;
    }

    //Initialise requested device

    pData = calloc(1, sizeof(DSoundData));
    if(!pData)
    {
        SetALCError(ALC_OUT_OF_MEMORY);
        return ALC_FALSE;
    }

    //DirectSound Init code
    hr = pDirectSoundCreate(guid, &pData->lpDS, NULL);
    if(SUCCEEDED(hr))
        hr = IDirectSound_SetCooperativeLevel(pData->lpDS, GetForegroundWindow(), DSSCL_PRIORITY);
    if(FAILED(hr))
    {
        if(pData->lpDS)
            IDirectSound_Release(pData->lpDS);
        free(pData);
        return ALC_FALSE;
    }

    device->szDeviceName = strdup(deviceName);
    device->ExtraData = pData;
    return ALC_TRUE;
}

static void DSoundClosePlayback(ALCdevice *device)
{
    DSoundData *pData = device->ExtraData;

    IDirectSound_Release(pData->lpDS);
    free(pData);
    device->ExtraData = NULL;
}

static ALCboolean DSoundStartContext(ALCdevice *device, ALCcontext *context)
{
    DSoundData *pData = (DSoundData*)device->ExtraData;
    DSBUFFERDESC DSBDescription;
    WAVEFORMATEXTENSIBLE OutputType;
    DWORD frameSize = 0;
    ALenum format = 0;
    DWORD speakers;
    HRESULT hr;
    (void)context;

    memset(&OutputType, 0, sizeof(OutputType));

    hr = IDirectSound_GetSpeakerConfig(pData->lpDS, &speakers);
    if(SUCCEEDED(hr) && *(GetConfigValue(NULL, "format", "")) != 0)
    {
        if(device->Format == AL_FORMAT_MONO8 || device->Format == AL_FORMAT_MONO16)
            speakers = DSSPEAKER_COMBINED(DSSPEAKER_MONO, 0);
        else if(device->Format == AL_FORMAT_STEREO8 || device->Format == AL_FORMAT_STEREO16)
            speakers = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 0);
        else if(device->Format == AL_FORMAT_QUAD8 || device->Format == AL_FORMAT_QUAD16)
            speakers = DSSPEAKER_COMBINED(DSSPEAKER_QUAD, 0);
        else if(device->Format == AL_FORMAT_51CHN8 || device->Format == AL_FORMAT_51CHN16)
            speakers = DSSPEAKER_COMBINED(DSSPEAKER_5POINT1, 0);
        else if(device->Format == AL_FORMAT_71CHN8 || device->Format == AL_FORMAT_71CHN16)
            speakers = DSSPEAKER_COMBINED(DSSPEAKER_7POINT1, 0);
    }
    if(SUCCEEDED(hr))
    {
        speakers = DSSPEAKER_CONFIG(speakers);
        if(speakers == DSSPEAKER_MONO)
        {
            if(aluBytesFromFormat(device->Format) == 1)
                format = AL_FORMAT_MONO8;
            else if(aluBytesFromFormat(device->Format) == 2)
                format = AL_FORMAT_MONO16;
            else if(aluBytesFromFormat(device->Format) == 4)
                format = AL_FORMAT_MONO_FLOAT32;
        }
        else if(speakers == DSSPEAKER_STEREO)
        {
            if(aluBytesFromFormat(device->Format) == 1)
                format = AL_FORMAT_STEREO8;
            else if(aluBytesFromFormat(device->Format) == 2)
                format = AL_FORMAT_STEREO16;
            else if(aluBytesFromFormat(device->Format) == 4)
                format = AL_FORMAT_STEREO_FLOAT32;
        }
        else if(speakers == DSSPEAKER_QUAD)
        {
            if(aluBytesFromFormat(device->Format) == 1)
                format = AL_FORMAT_QUAD8;
            else if(aluBytesFromFormat(device->Format) == 2)
                format = AL_FORMAT_QUAD16;
            else if(aluBytesFromFormat(device->Format) == 4)
                format = AL_FORMAT_QUAD32;
            OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                       SPEAKER_FRONT_RIGHT |
                                       SPEAKER_BACK_LEFT |
                                       SPEAKER_BACK_RIGHT;
        }
        else if(speakers == DSSPEAKER_5POINT1)
        {
            if(aluBytesFromFormat(device->Format) == 1)
                format = AL_FORMAT_51CHN8;
            else if(aluBytesFromFormat(device->Format) == 2)
                format = AL_FORMAT_51CHN16;
            else if(aluBytesFromFormat(device->Format) == 4)
                format = AL_FORMAT_51CHN32;
            OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                       SPEAKER_FRONT_RIGHT |
                                       SPEAKER_FRONT_CENTER |
                                       SPEAKER_LOW_FREQUENCY |
                                       SPEAKER_BACK_LEFT |
                                       SPEAKER_BACK_RIGHT;
        }
        else if(speakers == DSSPEAKER_7POINT1)
        {
            if(aluBytesFromFormat(device->Format) == 1)
                format = AL_FORMAT_71CHN8;
            else if(aluBytesFromFormat(device->Format) == 2)
                format = AL_FORMAT_71CHN16;
            else if(aluBytesFromFormat(device->Format) == 4)
                format = AL_FORMAT_71CHN32;
            OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                       SPEAKER_FRONT_RIGHT |
                                       SPEAKER_FRONT_CENTER |
                                       SPEAKER_LOW_FREQUENCY |
                                       SPEAKER_BACK_LEFT |
                                       SPEAKER_BACK_RIGHT |
                                       SPEAKER_SIDE_LEFT |
                                       SPEAKER_SIDE_RIGHT;
        }
        else
            format = device->Format;
        frameSize = aluBytesFromFormat(format) * aluChannelsFromFormat(format);

        OutputType.Format.wFormatTag = WAVE_FORMAT_PCM;
        OutputType.Format.nChannels = aluChannelsFromFormat(format);
        OutputType.Format.wBitsPerSample = aluBytesFromFormat(format) * 8;
        OutputType.Format.nBlockAlign = OutputType.Format.nChannels*OutputType.Format.wBitsPerSample/8;
        OutputType.Format.nSamplesPerSec = device->Frequency;
        OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec*OutputType.Format.nBlockAlign;
        OutputType.Format.cbSize = 0;
    }

    if(OutputType.Format.nChannels > 2 || OutputType.Format.wBitsPerSample > 16)
    {
        OutputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
        OutputType.Format.cbSize = 22;
        if(OutputType.Format.wBitsPerSample == 32)
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        else
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }
    else
    {
        if(SUCCEEDED(hr))
        {
            memset(&DSBDescription,0,sizeof(DSBUFFERDESC));
            DSBDescription.dwSize=sizeof(DSBUFFERDESC);
            DSBDescription.dwFlags=DSBCAPS_PRIMARYBUFFER;
            hr = IDirectSound_CreateSoundBuffer(pData->lpDS, &DSBDescription, &pData->DSpbuffer, NULL);
        }
        if(SUCCEEDED(hr))
            hr = IDirectSoundBuffer_SetFormat(pData->DSpbuffer,&OutputType.Format);
    }

    if(SUCCEEDED(hr))
    {
        memset(&DSBDescription,0,sizeof(DSBUFFERDESC));
        DSBDescription.dwSize=sizeof(DSBUFFERDESC);
        DSBDescription.dwFlags=DSBCAPS_GLOBALFOCUS|DSBCAPS_GETCURRENTPOSITION2;
        DSBDescription.dwBufferBytes=(device->BufferSize/num_frags) * num_frags * frameSize;
        DSBDescription.lpwfxFormat=&OutputType.Format;
        hr = IDirectSound_CreateSoundBuffer(pData->lpDS, &DSBDescription, &pData->DSsbuffer, NULL);
    }

    if(SUCCEEDED(hr))
        hr = IDirectSoundBuffer_Play(pData->DSsbuffer, 0, 0, DSBPLAY_LOOPING);

    if(SUCCEEDED(hr))
    {
        device->ExtraData = pData;
        pData->thread = StartThread(DSoundProc, device);
        if(!pData->thread)
            hr = E_FAIL;
    }

    if(FAILED(hr))
    {
        if (pData->DSsbuffer)
            IDirectSoundBuffer_Release(pData->DSsbuffer);
        pData->DSsbuffer = NULL;
        if (pData->DSpbuffer)
            IDirectSoundBuffer_Release(pData->DSpbuffer);
        pData->DSpbuffer = NULL;
        return ALC_FALSE;
    }

    device->Format = format;
    device->UpdateSize = device->BufferSize/num_frags;

    return ALC_TRUE;
}

static void DSoundStopContext(ALCdevice *device, ALCcontext *context)
{
    DSoundData *pData = device->ExtraData;
    (void)context;

    if(!pData->thread)
        return;

    pData->killNow = 1;
    StopThread(pData->thread);
    pData->thread = NULL;

    IDirectSoundBuffer_Release(pData->DSsbuffer);
    pData->DSsbuffer = NULL;
    if (pData->DSpbuffer)
        IDirectSoundBuffer_Release(pData->DSpbuffer);
    pData->DSpbuffer = NULL;
}


static ALCboolean DSoundOpenCapture(ALCdevice *pDevice, const ALCchar *deviceName)
{
    (void)pDevice;
    (void)deviceName;
    return ALC_FALSE;
}

static void DSoundCloseCapture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void DSoundStartCapture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void DSoundStopCapture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void DSoundCaptureSamples(ALCdevice *pDevice, ALCvoid *pBuffer, ALCuint lSamples)
{
    (void)pDevice;
    (void)pBuffer;
    (void)lSamples;
}

static ALCuint DSoundAvailableSamples(ALCdevice *pDevice)
{
    (void)pDevice;
    return 0;
}


BackendFuncs DSoundFuncs = {
    DSoundOpenPlayback,
    DSoundClosePlayback,
    DSoundStartContext,
    DSoundStopContext,
    DSoundOpenCapture,
    DSoundCloseCapture,
    DSoundStartCapture,
    DSoundStopCapture,
    DSoundCaptureSamples,
    DSoundAvailableSamples
};

static BOOL CALLBACK DSoundEnumDevices(LPGUID guid, LPCSTR desc, LPCSTR drvname, LPVOID data)
{
    (void)data;
    (void)drvname;

    if(guid)
    {
        char str[128];
        void *temp;

        temp = realloc(DeviceList, sizeof(DevMap) * (NumDevices+1));
        if(temp)
        {
            DeviceList = temp;

            snprintf(str, sizeof(str), "DirectSound Software on %s", desc);
            AppendAllDeviceList(str);

            DeviceList[NumDevices].name = strdup(str);
            DeviceList[NumDevices].guid = *guid;
            NumDevices++;
        }
    }

    return TRUE;
}

void alcDSoundInit(BackendFuncs *FuncList)
{
    *FuncList = DSoundFuncs;

#ifdef _WIN32
    ds_handle = LoadLibraryA("dsound.dll");
    if(ds_handle == NULL)
    {
        AL_PRINT("Failed to load dsound.dll\n");
        return;
    }

#define LOAD_FUNC(f) do { \
    p##f = (void*)GetProcAddress((HMODULE)ds_handle, #f); \
    if(p##f == NULL) \
    { \
        FreeLibrary(ds_handle); \
        ds_handle = NULL; \
        AL_PRINT("Could not load %s from dsound.dll\n", #f); \
        return; \
    } \
} while(0)
#else
    ds_handle = (void*)0xDEADBEEF;
#define LOAD_FUNC(f) p##f = f
#endif

LOAD_FUNC(DirectSoundCreate);
LOAD_FUNC(DirectSoundEnumerateA);
#undef LOAD_FUNC

    num_frags = GetConfigValueInt("dsound", "periods", 4);
    if(num_frags < 2) num_frags = 2;
}

void alcDSoundDeinit(void)
{
    ALuint i;

    for(i = 0;i < NumDevices;++i)
        free(DeviceList[i].name);
    free(DeviceList);
    DeviceList = NULL;
    NumDevices = 0;

#ifdef _WIN32
    if(ds_handle)
        FreeLibrary(ds_handle);
    ds_handle = NULL;
#endif
}

void alcDSoundProbe(int type)
{
    if(!ds_handle)
        return;

    if(type == DEVICE_PROBE)
        AppendDeviceList(dsDevice);
    else if(type == ALL_DEVICE_PROBE)
    {
        HRESULT hr;
        ALuint i;

        for(i = 0;i < NumDevices;++i)
            free(DeviceList[i].name);
        free(DeviceList);
        DeviceList = NULL;
        NumDevices = 0;

        hr = pDirectSoundEnumerateA(DSoundEnumDevices, NULL);
        if(FAILED(hr))
            AL_PRINT("Error enumerating DirectSound devices (%#x)!\n", (unsigned int)hr);
    }
}
