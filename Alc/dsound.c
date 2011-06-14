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
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <dsound.h>
#include <cguid.h>
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

#define DirectSoundCreate     pDirectSoundCreate
#define DirectSoundEnumerateA pDirectSoundEnumerateA


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

static const ALCchar dsDevice[] = "DirectSound Default";
static DevMap *DeviceList;
static ALuint NumDevices;


static void *DSoundLoad(void)
{
    if(!ds_handle)
    {
        ALboolean failed = AL_FALSE;

        ds_handle = LoadLibraryA("dsound.dll");
        if(ds_handle == NULL)
        {
            AL_PRINT("Failed to load dsound.dll\n");
            return NULL;
        }

#define LOAD_FUNC(x) do {                                                     \
    if((p##x = (void*)GetProcAddress((HMODULE)ds_handle, #x)) == NULL) {      \
        AL_PRINT("Could not load %s from dsound.dll\n", #x);                  \
        failed = AL_TRUE;                                                     \
    }                                                                         \
} while(0)
        LOAD_FUNC(DirectSoundCreate);
        LOAD_FUNC(DirectSoundEnumerateA);
#undef LOAD_FUNC

        if(failed)
        {
            FreeLibrary(ds_handle);
            ds_handle = NULL;
        }
    }
    return ds_handle;
}


static BOOL CALLBACK DSoundEnumDevices(LPGUID guid, LPCSTR desc, LPCSTR drvname, LPVOID data)
{
    char str[1024];
    void *temp;
    int count;
    ALuint i;

    (void)data;
    (void)drvname;

    if(NumDevices == 0)
    {
        temp = realloc(DeviceList, sizeof(DevMap) * (NumDevices+1));
        if(temp)
        {
            DeviceList = temp;
            DeviceList[NumDevices].name = strdup(dsDevice);
            DeviceList[NumDevices].guid = GUID_NULL;
            NumDevices++;
        }
    }

    if(!guid)
        return TRUE;

    count = 0;
    do {
        if(count == 0)
            snprintf(str, sizeof(str), "%s via DirectSound", desc);
        else
            snprintf(str, sizeof(str), "%s #%d via DirectSound", desc, count+1);
        count++;

        for(i = 0;i < NumDevices;i++)
        {
            if(strcmp(str, DeviceList[i].name) == 0)
                break;
        }
    } while(i != NumDevices);

    temp = realloc(DeviceList, sizeof(DevMap) * (NumDevices+1));
    if(temp)
    {
        DeviceList = temp;
        DeviceList[NumDevices].name = strdup(str);
        DeviceList[NumDevices].guid = *guid;
        NumDevices++;
    }

    return TRUE;
}


static ALuint DSoundProc(ALvoid *ptr)
{
    ALCdevice *pDevice = (ALCdevice*)ptr;
    DSoundData *pData = (DSoundData*)pDevice->ExtraData;
    DSBCAPS DSBCaps;
    DWORD LastCursor = 0;
    DWORD PlayCursor;
    VOID *WritePtr1, *WritePtr2;
    DWORD WriteCnt1,  WriteCnt2;
    BOOL Playing = FALSE;
    DWORD FrameSize;
    DWORD FragSize;
    DWORD avail;
    HRESULT err;

    SetRTPriority();

    memset(&DSBCaps, 0, sizeof(DSBCaps));
    DSBCaps.dwSize = sizeof(DSBCaps);
    err = IDirectSoundBuffer_GetCaps(pData->DSsbuffer, &DSBCaps);
    if(FAILED(err))
    {
        AL_PRINT("Failed to get buffer caps: 0x%lx\n", err);
        aluHandleDisconnect(pDevice);
        return 1;
    }

    FrameSize = FrameSizeFromDevFmt(pDevice->FmtChans, pDevice->FmtType);
    FragSize = pDevice->UpdateSize * FrameSize;

    IDirectSoundBuffer_GetCurrentPosition(pData->DSsbuffer, &LastCursor, NULL);
    while(!pData->killNow)
    {
        // Get current play and write cursors
        IDirectSoundBuffer_GetCurrentPosition(pData->DSsbuffer, &PlayCursor, NULL);
        avail = (PlayCursor-LastCursor+DSBCaps.dwBufferBytes) % DSBCaps.dwBufferBytes;

        if(avail < FragSize)
        {
            if(!Playing)
            {
                err = IDirectSoundBuffer_Play(pData->DSsbuffer, 0, 0, DSBPLAY_LOOPING);
                if(FAILED(err))
                {
                    AL_PRINT("Failed to play buffer: 0x%lx\n", err);
                    aluHandleDisconnect(pDevice);
                    return 1;
                }
                Playing = TRUE;
            }
            Sleep(1);
            continue;
        }
        avail -= avail%FragSize;

        // Lock output buffer
        WriteCnt1 = 0;
        WriteCnt2 = 0;
        err = IDirectSoundBuffer_Lock(pData->DSsbuffer, LastCursor, avail, &WritePtr1, &WriteCnt1, &WritePtr2, &WriteCnt2, 0);

        // If the buffer is lost, restore it and lock
        if(err == DSERR_BUFFERLOST)
        {
            err = IDirectSoundBuffer_Restore(pData->DSsbuffer);
            if(SUCCEEDED(err))
            {
                Playing = FALSE;
                LastCursor = 0;
                err = IDirectSoundBuffer_Lock(pData->DSsbuffer, 0, DSBCaps.dwBufferBytes, &WritePtr1, &WriteCnt1, &WritePtr2, &WriteCnt2, 0);
            }
        }

        // Successfully locked the output buffer
        if(SUCCEEDED(err))
        {
            // If we have an active context, mix data directly into output buffer otherwise fill with silence
            aluMixData(pDevice, WritePtr1, WriteCnt1/FrameSize);
            aluMixData(pDevice, WritePtr2, WriteCnt2/FrameSize);

            // Unlock output buffer only when successfully locked
            IDirectSoundBuffer_Unlock(pData->DSsbuffer, WritePtr1, WriteCnt1, WritePtr2, WriteCnt2);
        }
        else
        {
            AL_PRINT("Buffer lock error: %#lx\n", err);
            aluHandleDisconnect(pDevice);
            return 1;
        }

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

    if(!DSoundLoad())
        return ALC_FALSE;

    if(!deviceName)
        deviceName = dsDevice;
    else if(strcmp(deviceName, dsDevice) != 0)
    {
        ALuint i;

        if(!DeviceList)
        {
            hr = DirectSoundEnumerateA(DSoundEnumDevices, NULL);
            if(FAILED(hr))
                AL_PRINT("Error enumerating DirectSound devices (%#x)!\n", (unsigned int)hr);
        }

        for(i = 0;i < NumDevices;i++)
        {
            if(strcmp(deviceName, DeviceList[i].name) == 0)
            {
                if(i > 0)
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
        alcSetError(device, ALC_OUT_OF_MEMORY);
        return ALC_FALSE;
    }

    //DirectSound Init code
    hr = DirectSoundCreate(guid, &pData->lpDS, NULL);
    if(SUCCEEDED(hr))
        hr = IDirectSound_SetCooperativeLevel(pData->lpDS, GetForegroundWindow(), DSSCL_PRIORITY);
    if(FAILED(hr))
    {
        if(pData->lpDS)
            IDirectSound_Release(pData->lpDS);
        free(pData);
        AL_PRINT("Device init failed: 0x%08lx\n", hr);
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

static ALCboolean DSoundResetPlayback(ALCdevice *device)
{
    DSoundData *pData = (DSoundData*)device->ExtraData;
    DSBUFFERDESC DSBDescription;
    WAVEFORMATEXTENSIBLE OutputType;
    DWORD speakers;
    HRESULT hr;

    memset(&OutputType, 0, sizeof(OutputType));

    switch(device->FmtType)
    {
        case DevFmtByte:
            device->FmtType = DevFmtUByte;
            break;
        case DevFmtUShort:
            device->FmtType = DevFmtShort;
            break;
        case DevFmtUByte:
        case DevFmtShort:
        case DevFmtFloat:
            break;
    }

    hr = IDirectSound_GetSpeakerConfig(pData->lpDS, &speakers);
    if(SUCCEEDED(hr))
    {
        if(!(device->Flags&DEVICE_CHANNELS_REQUEST))
        {
            speakers = DSSPEAKER_CONFIG(speakers);
            if(speakers == DSSPEAKER_MONO)
                device->FmtChans = DevFmtMono;
            else if(speakers == DSSPEAKER_STEREO || speakers == DSSPEAKER_HEADPHONE)
                device->FmtChans = DevFmtStereo;
            else if(speakers == DSSPEAKER_QUAD)
                device->FmtChans = DevFmtQuad;
            else if(speakers == DSSPEAKER_5POINT1)
                device->FmtChans = DevFmtX51;
            else if(speakers == DSSPEAKER_7POINT1)
                device->FmtChans = DevFmtX71;
            else
                AL_PRINT("Unknown system speaker config: 0x%lx\n", speakers);
        }

        switch(device->FmtChans)
        {
            case DevFmtMono:
                OutputType.dwChannelMask = SPEAKER_FRONT_CENTER;
                break;
            case DevFmtStereo:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT;
                break;
            case DevFmtQuad:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_BACK_LEFT |
                                           SPEAKER_BACK_RIGHT;
                break;
            case DevFmtX51:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_FRONT_CENTER |
                                           SPEAKER_LOW_FREQUENCY |
                                           SPEAKER_BACK_LEFT |
                                           SPEAKER_BACK_RIGHT;
                break;
            case DevFmtX51Side:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_FRONT_CENTER |
                                           SPEAKER_LOW_FREQUENCY |
                                           SPEAKER_SIDE_LEFT |
                                           SPEAKER_SIDE_RIGHT;
                break;
            case DevFmtX61:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_FRONT_CENTER |
                                           SPEAKER_LOW_FREQUENCY |
                                           SPEAKER_BACK_CENTER |
                                           SPEAKER_SIDE_LEFT |
                                           SPEAKER_SIDE_RIGHT;
                break;
            case DevFmtX71:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_FRONT_CENTER |
                                           SPEAKER_LOW_FREQUENCY |
                                           SPEAKER_BACK_LEFT |
                                           SPEAKER_BACK_RIGHT |
                                           SPEAKER_SIDE_LEFT |
                                           SPEAKER_SIDE_RIGHT;
                break;
        }

        OutputType.Format.wFormatTag = WAVE_FORMAT_PCM;
        OutputType.Format.nChannels = ChannelsFromDevFmt(device->FmtChans);
        OutputType.Format.wBitsPerSample = BytesFromDevFmt(device->FmtType) * 8;
        OutputType.Format.nBlockAlign = OutputType.Format.nChannels*OutputType.Format.wBitsPerSample/8;
        OutputType.Format.nSamplesPerSec = device->Frequency;
        OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec*OutputType.Format.nBlockAlign;
        OutputType.Format.cbSize = 0;
    }

    if(OutputType.Format.nChannels > 2 || device->FmtType == DevFmtFloat)
    {
        OutputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
        OutputType.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        if(device->FmtType == DevFmtFloat)
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
        DSBDescription.dwBufferBytes=device->UpdateSize * device->NumUpdates *
                                     OutputType.Format.nBlockAlign;
        DSBDescription.lpwfxFormat=&OutputType.Format;
        hr = IDirectSound_CreateSoundBuffer(pData->lpDS, &DSBDescription, &pData->DSsbuffer, NULL);
    }

    if(SUCCEEDED(hr))
    {
        SetDefaultWFXChannelOrder(device);
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

    return ALC_TRUE;
}

static void DSoundStopPlayback(ALCdevice *device)
{
    DSoundData *pData = device->ExtraData;

    if(!pData->thread)
        return;

    pData->killNow = 1;
    StopThread(pData->thread);
    pData->thread = NULL;

    pData->killNow = 0;

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


static const BackendFuncs DSoundFuncs = {
    DSoundOpenPlayback,
    DSoundClosePlayback,
    DSoundResetPlayback,
    DSoundStopPlayback,
    DSoundOpenCapture,
    DSoundCloseCapture,
    DSoundStartCapture,
    DSoundStopCapture,
    DSoundCaptureSamples,
    DSoundAvailableSamples
};


void alcDSoundInit(BackendFuncs *FuncList)
{
    *FuncList = DSoundFuncs;
}

void alcDSoundDeinit(void)
{
    ALuint i;

    for(i = 0;i < NumDevices;++i)
        free(DeviceList[i].name);
    free(DeviceList);
    DeviceList = NULL;
    NumDevices = 0;

    if(ds_handle)
    {
#ifdef _WIN32
        FreeLibrary(ds_handle);
#endif
        ds_handle = NULL;
    }
}

void alcDSoundProbe(enum DevProbe type)
{
    HRESULT hr;
    ALuint i;

    if(!DSoundLoad()) return;

    switch(type)
    {
        case DEVICE_PROBE:
            AppendDeviceList(dsDevice);
            break;

        case ALL_DEVICE_PROBE:
            for(i = 0;i < NumDevices;++i)
                free(DeviceList[i].name);
            free(DeviceList);
            DeviceList = NULL;
            NumDevices = 0;

            hr = DirectSoundEnumerateA(DSoundEnumDevices, NULL);
            if(FAILED(hr))
                AL_PRINT("Error enumerating DirectSound devices (%#x)!\n", (unsigned int)hr);
            else
            {
                for(i = 0;i < NumDevices;i++)
                    AppendAllDeviceList(DeviceList[i].name);
            }
            break;

        case CAPTURE_DEVICE_PROBE:
            break;
    }
}
