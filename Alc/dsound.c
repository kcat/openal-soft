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

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"


typedef struct {
    // DirectSound Playback Device
    LPDIRECTSOUND          lpDS;
    LPDIRECTSOUNDBUFFER    DSpbuffer;
    LPDIRECTSOUNDBUFFER    DSsbuffer;
    MMRESULT               ulDSTimerID;
    DWORD                  OldWriteCursor;
} DSoundData;


static ALCchar *DeviceList[16];

static void CALLBACK DirectSoundProc(UINT uID,UINT uReserved,DWORD_PTR dwUser,DWORD_PTR dwReserved1,DWORD_PTR dwReserved2)
{
    ALCdevice *pDevice = (ALCdevice *)dwUser;
    DSoundData *pData = pDevice->ExtraData;
    DWORD PlayCursor,WriteCursor;
    BYTE *WritePtr1,*WritePtr2;
    DWORD WriteCnt1,WriteCnt2;
    WAVEFORMATEX OutputType;
    DWORD BytesPlayed;
    DWORD BufSize;
    HRESULT DSRes;

    (void)uID;
    (void)uReserved;
    (void)dwReserved1;
    (void)dwReserved2;

    BufSize = pDevice->UpdateFreq * pDevice->FrameSize;

    // Get current play and write cursors
    IDirectSoundBuffer_GetCurrentPosition(pData->DSsbuffer,&PlayCursor,&WriteCursor);
    if (!pData->OldWriteCursor) pData->OldWriteCursor=WriteCursor-PlayCursor;

    // Get the output format and figure the number of bytes played (block aligned)
    IDirectSoundBuffer_GetFormat(pData->DSsbuffer,&OutputType,sizeof(WAVEFORMATEX),NULL);
    BytesPlayed=((((WriteCursor<pData->OldWriteCursor)?(BufSize+WriteCursor-pData->OldWriteCursor):(WriteCursor-pData->OldWriteCursor))/OutputType.nBlockAlign)*OutputType.nBlockAlign);

    // Lock output buffer started at 40msec in front of the old write cursor (15msec in front of the actual write cursor)
    DSRes=IDirectSoundBuffer_Lock(pData->DSsbuffer,(pData->OldWriteCursor+(OutputType.nSamplesPerSec/25)*OutputType.nBlockAlign)%BufSize,BytesPlayed,(LPVOID*)&WritePtr1,&WriteCnt1,(LPVOID*)&WritePtr2,&WriteCnt2,0);

    // If the buffer is lost, restore it, play and lock
    if (DSRes==DSERR_BUFFERLOST)
    {
        IDirectSoundBuffer_Restore(pData->DSsbuffer);
        IDirectSoundBuffer_Play(pData->DSsbuffer,0,0,DSBPLAY_LOOPING);
        DSRes=IDirectSoundBuffer_Lock(pData->DSsbuffer,(pData->OldWriteCursor+(OutputType.nSamplesPerSec/25)*OutputType.nBlockAlign)%BufSize,BytesPlayed,(LPVOID*)&WritePtr1,&WriteCnt1,(LPVOID*)&WritePtr2,&WriteCnt2,0);
    }

    // Successfully locked the output buffer
    if (DSRes==DS_OK)
    {
        // If we have an active context, mix data directly into output buffer otherwise fill with silence
        SuspendContext(NULL);
        if (WritePtr1)
            aluMixData(pDevice->Context, WritePtr1, WriteCnt1, pDevice->Format);
        if (WritePtr2)
            aluMixData(pDevice->Context, WritePtr2, WriteCnt2, pDevice->Format);
        ProcessContext(NULL);

        // Unlock output buffer only when successfully locked
        IDirectSoundBuffer_Unlock(pData->DSsbuffer,WritePtr1,WriteCnt1,WritePtr2,WriteCnt2);
    }

    // Update old write cursor location
    pData->OldWriteCursor=((pData->OldWriteCursor+BytesPlayed)%BufSize);
}


static ALCboolean DSoundOpenPlayback(ALCdevice *device, const ALCchar *deviceName)
{
    DSBUFFERDESC DSBDescription;
    DSoundData *pData = NULL;
    WAVEFORMATEX OutputType;
    HRESULT hr;

    if(deviceName)
    {
        int i;
        for(i = 0;DeviceList[i];i++)
        {
            if(strcmp(deviceName, DeviceList[i]) == 0)
            {
                device->szDeviceName = DeviceList[i];
                break;
            }
        }
        if(!DeviceList[i])
            return ALC_FALSE;
    }
    else
        device->szDeviceName = DeviceList[0];

    //Platform specific
    memset(&OutputType, 0, sizeof(WAVEFORMATEX));
    OutputType.wFormatTag = WAVE_FORMAT_PCM;
    OutputType.nChannels = device->Channels;
    OutputType.wBitsPerSample = (((device->Format==AL_FORMAT_MONO16)||(device->Format==AL_FORMAT_STEREO16)||(device->Format==AL_FORMAT_QUAD16))?16:8);
    OutputType.nBlockAlign = OutputType.nChannels*OutputType.wBitsPerSample/8;
    OutputType.nSamplesPerSec = device->Frequency;
    OutputType.nAvgBytesPerSec = OutputType.nSamplesPerSec*OutputType.nBlockAlign;
    OutputType.cbSize = 0;

    //Initialise requested device

    pData = calloc(1, sizeof(DSoundData));
    if(!pData)
    {
        SetALCError(ALC_OUT_OF_MEMORY);
        return ALC_FALSE;
    }

    //Init COM
    CoInitialize(NULL);

    //DirectSound Init code
    hr = CoCreateInstance(&CLSID_DirectSound, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectSound, (LPVOID*)&pData->lpDS);
    if(SUCCEEDED(hr))
        hr = IDirectSound_Initialize(pData->lpDS, NULL);
    if(SUCCEEDED(hr))
        hr = IDirectSound_SetCooperativeLevel(pData->lpDS, GetForegroundWindow(), DSSCL_PRIORITY);

    if(SUCCEEDED(hr))
    {
        memset(&DSBDescription,0,sizeof(DSBUFFERDESC));
        DSBDescription.dwSize=sizeof(DSBUFFERDESC);
        DSBDescription.dwFlags=DSBCAPS_PRIMARYBUFFER;
        hr = IDirectSound_CreateSoundBuffer(pData->lpDS, &DSBDescription, &pData->DSpbuffer, NULL);
    }
    if(SUCCEEDED(hr))
        hr = IDirectSoundBuffer_SetFormat(pData->DSpbuffer,&OutputType);

    if(SUCCEEDED(hr))
    {
        memset(&DSBDescription,0,sizeof(DSBUFFERDESC));
        DSBDescription.dwSize=sizeof(DSBUFFERDESC);
        DSBDescription.dwFlags=DSBCAPS_GLOBALFOCUS|DSBCAPS_GETCURRENTPOSITION2;
        DSBDescription.dwBufferBytes=device->UpdateFreq * device->FrameSize;
        DSBDescription.lpwfxFormat=&OutputType;
        hr = IDirectSound_CreateSoundBuffer(pData->lpDS, &DSBDescription, &pData->DSsbuffer, NULL);
    }

    if(SUCCEEDED(hr))
        hr = IDirectSoundBuffer_Play(pData->DSsbuffer, 0, 0, DSBPLAY_LOOPING);

    if(FAILED(hr))
    {
        if (pData->DSsbuffer)
            IDirectSoundBuffer_Release(pData->DSsbuffer);
        if (pData->DSpbuffer)
            IDirectSoundBuffer_Release(pData->DSpbuffer);
        if (pData->lpDS)
            IDirectSound_Release(pData->lpDS);

        free(pData);
        return ALC_FALSE;
    }

    pData->ulDSTimerID = timeSetEvent(25, 0, (LPTIMECALLBACK)DirectSoundProc, (DWORD)device, (UINT)TIME_CALLBACK_FUNCTION|TIME_PERIODIC);
    device->MaxNoOfSources = 256;

    device->ExtraData = pData;
    return ALC_TRUE;
}

static void DSoundClosePlayback(ALCdevice *device)
{
    DSoundData *pData = device->ExtraData;

    // Stop and release the DS timer
    if (pData->ulDSTimerID)
        timeKillEvent(pData->ulDSTimerID);

    // Wait ... just in case any timer events happen
    Sleep(100);

    SuspendContext(NULL);

    if (pData->DSsbuffer)
        IDirectSoundBuffer_Release(pData->DSsbuffer);
    if (pData->DSpbuffer)
        IDirectSoundBuffer_Release(pData->DSpbuffer);
    IDirectSound_Release(pData->lpDS);

    //Deinit COM
    CoUninitialize();

    ProcessContext(NULL);

    free(pData);
    device->ExtraData = NULL;
}


static ALCboolean DSoundOpenCapture(ALCdevice *pDevice, const ALCchar *deviceName, ALCuint frequency, ALCenum format, ALCsizei SampleSize)
{
    (void)pDevice;
    (void)deviceName;
    (void)frequency;
    (void)format;
    (void)SampleSize;
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

    DeviceList[0] = AppendDeviceList("DirectSound Software");
    AppendAllDeviceList(DeviceList[0]);
}
