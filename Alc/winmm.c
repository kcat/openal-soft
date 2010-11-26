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

#include <windows.h>
#include <mmsystem.h>

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"


typedef struct {
    // MMSYSTEM Device
    volatile ALboolean bWaveShutdown;
    HANDLE       hWaveHdrEvent;
    HANDLE       hWaveThreadEvent;
    HANDLE       hWaveThread;
    DWORD        ulWaveThreadID;
    ALint        lWaveBuffersCommitted;
    WAVEHDR      WaveBuffer[4];

    union {
        HWAVEIN  In;
    } hWaveHandle;

    RingBuffer   *pRing;
} WinMMData;


static ALCchar **CaptureDeviceList;
static ALuint  NumCaptureDevices;

static void ProbeCaptureDevices(void)
{
    ALuint i;

    for(i = 0;i < NumCaptureDevices;i++)
        free(CaptureDeviceList[i]);

    NumCaptureDevices = waveInGetNumDevs();
    CaptureDeviceList = realloc(CaptureDeviceList, sizeof(ALCchar*) * NumCaptureDevices);
    for(i = 0;i < NumCaptureDevices;i++)
    {
        WAVEINCAPS WaveInCaps;

        CaptureDeviceList[i] = NULL;
        if(waveInGetDevCaps(i, &WaveInCaps, sizeof(WAVEINCAPS)) == MMSYSERR_NOERROR)
        {
            char name[1024];
            ALuint count, j;

            count = 0;
            do {
                if(count == 0)
                    snprintf(name, sizeof(name), "%s via WaveIn", WaveInCaps.szPname);
                else
                    snprintf(name, sizeof(name), "%s #%d via WaveIn", WaveInCaps.szPname, count+1);
                count++;

                for(j = 0;j < i;j++)
                {
                    if(strcmp(name, CaptureDeviceList[j]) == 0)
                        break;
                }
            } while(j != i);

            CaptureDeviceList[i] = strdup(name);
        }
    }
}


/*
    WaveInProc

    Posts a message to 'CaptureThreadProc' everytime a WaveIn Buffer is completed and
    returns to the application (with more data)
*/
static void CALLBACK WaveInProc(HWAVEIN hDevice,UINT uMsg,DWORD_PTR dwInstance,DWORD_PTR dwParam1,DWORD_PTR dwParam2)
{
    ALCdevice *pDevice = (ALCdevice*)dwInstance;
    WinMMData *pData = pDevice->ExtraData;

    (void)hDevice;
    (void)dwParam2;

    if(uMsg != WIM_DATA)
        return;

    // Decrement number of buffers in use
    pData->lWaveBuffersCommitted--;

    if(pData->bWaveShutdown == AL_FALSE)
    {
        // Notify Wave Processor Thread that a Wave Header has returned
        PostThreadMessage(pData->ulWaveThreadID,uMsg,0,dwParam1);
    }
    else
    {
        if(pData->lWaveBuffersCommitted == 0)
        {
            // Signal Wave Buffers Returned event
            if(pData->hWaveHdrEvent)
                SetEvent(pData->hWaveHdrEvent);

            // Post 'Quit' Message to WaveIn Processor Thread
            PostThreadMessage(pData->ulWaveThreadID,WM_QUIT,0,0);
        }
    }
}

/*
    CaptureThreadProc

    Used by "MMSYSTEM" Device.  Called when a WaveIn buffer had been filled with new
    audio data.
*/
static DWORD WINAPI CaptureThreadProc(LPVOID lpParameter)
{
    ALCdevice *pDevice = (ALCdevice*)lpParameter;
    WinMMData *pData = pDevice->ExtraData;
    LPWAVEHDR pWaveHdr;
    ALuint FrameSize;
    MSG msg;

    FrameSize = aluFrameSizeFromFormat(pDevice->Format);

    while(GetMessage(&msg, NULL, 0, 0))
    {
        if(msg.message != WIM_DATA || pData->bWaveShutdown)
            continue;

        pWaveHdr = ((LPWAVEHDR)msg.lParam);

        WriteRingBuffer(pData->pRing, (ALubyte*)pWaveHdr->lpData,
                        pWaveHdr->dwBytesRecorded/FrameSize);

        // Send buffer back to capture more data
        waveInAddBuffer(pData->hWaveHandle.In,pWaveHdr,sizeof(WAVEHDR));
        pData->lWaveBuffersCommitted++;
    }

    // Signal Wave Thread completed event
    if(pData->hWaveThreadEvent)
        SetEvent(pData->hWaveThreadEvent);

    ExitThread(0);

    return 0;
}


static ALCboolean WinMMOpenPlayback(ALCdevice *device, const ALCchar *deviceName)
{
    (void)device;
    (void)deviceName;
    return ALC_FALSE;
}

static void WinMMClosePlayback(ALCdevice *device)
{
    (void)device;
}

static ALCboolean WinMMOpenCapture(ALCdevice *pDevice, const ALCchar *deviceName)
{
    WAVEFORMATEX wfexCaptureFormat;
    DWORD ulCapturedDataSize;
    WinMMData *pData = NULL;
    ALint lDeviceID = 0;
    ALbyte *BufferData;
    ALint lBufferSize;
    MMRESULT res;
    ALuint i;

    if(!CaptureDeviceList)
        ProbeCaptureDevices();

    // Find the Device ID matching the deviceName if valid
    if(deviceName)
    {
        for(i = 0;i < NumCaptureDevices;i++)
        {
            if(CaptureDeviceList[i] &&
               strcmp(deviceName, CaptureDeviceList[i]) == 0)
            {
                lDeviceID = i;
                break;
            }
        }
    }
    else
    {
        for(i = 0;i < NumCaptureDevices;i++)
        {
            if(CaptureDeviceList[i])
            {
                lDeviceID = i;
                break;
            }
        }
    }
    if(i == NumCaptureDevices)
        return ALC_FALSE;

    pData = calloc(1, sizeof(*pData));
    if(!pData)
    {
        alcSetError(pDevice, ALC_OUT_OF_MEMORY);
        return ALC_FALSE;
    }

    memset(&wfexCaptureFormat, 0, sizeof(WAVEFORMATEX));
    wfexCaptureFormat.wFormatTag = WAVE_FORMAT_PCM;
    wfexCaptureFormat.nChannels = aluChannelsFromFormat(pDevice->Format);
    wfexCaptureFormat.wBitsPerSample = aluBytesFromFormat(pDevice->Format) * 8;
    wfexCaptureFormat.nBlockAlign = wfexCaptureFormat.wBitsPerSample *
                                    wfexCaptureFormat.nChannels / 8;
    wfexCaptureFormat.nSamplesPerSec = pDevice->Frequency;
    wfexCaptureFormat.nAvgBytesPerSec = wfexCaptureFormat.nSamplesPerSec *
                                        wfexCaptureFormat.nBlockAlign;
    wfexCaptureFormat.cbSize = 0;

    if((res=waveInOpen(&pData->hWaveHandle.In, lDeviceID, &wfexCaptureFormat, (DWORD_PTR)&WaveInProc, (DWORD_PTR)pDevice, CALLBACK_FUNCTION)) != MMSYSERR_NOERROR)
    {
        AL_PRINT("waveInOpen failed: %u\n", res);
        goto failure;
    }

    pData->hWaveHdrEvent = CreateEvent(NULL, AL_TRUE, AL_FALSE, "WaveInAllHeadersReturned");
    pData->hWaveThreadEvent = CreateEvent(NULL, AL_TRUE, AL_FALSE, "WaveInThreadDestroyed");
    if(pData->hWaveHdrEvent == NULL || pData->hWaveThreadEvent == NULL)
    {
        AL_PRINT("CreateEvent failed: %lu\n", GetLastError());
        goto failure;
    }

    // Allocate circular memory buffer for the captured audio
    ulCapturedDataSize = pDevice->UpdateSize*pDevice->NumUpdates;

    // Make sure circular buffer is at least 100ms in size
    if(ulCapturedDataSize < (wfexCaptureFormat.nSamplesPerSec / 10))
        ulCapturedDataSize = wfexCaptureFormat.nSamplesPerSec / 10;

    pData->pRing = CreateRingBuffer(wfexCaptureFormat.nBlockAlign, ulCapturedDataSize);
    if(!pData->pRing)
        goto failure;

    pData->lWaveBuffersCommitted = 0;

    // Create 4 Buffers of 50ms each
    lBufferSize = wfexCaptureFormat.nAvgBytesPerSec / 20;
    lBufferSize -= (lBufferSize % wfexCaptureFormat.nBlockAlign);

    BufferData = calloc(4, lBufferSize);
    if(!BufferData)
        goto failure;

    for(i = 0;i < 4;i++)
    {
        memset(&pData->WaveBuffer[i], 0, sizeof(WAVEHDR));
        pData->WaveBuffer[i].dwBufferLength = lBufferSize;
        pData->WaveBuffer[i].lpData = ((i==0) ? (LPSTR)BufferData :
                                       (pData->WaveBuffer[i-1].lpData +
                                        pData->WaveBuffer[i-1].dwBufferLength));
        pData->WaveBuffer[i].dwFlags = 0;
        pData->WaveBuffer[i].dwLoops = 0;
        waveInPrepareHeader(pData->hWaveHandle.In, &pData->WaveBuffer[i], sizeof(WAVEHDR));
        waveInAddBuffer(pData->hWaveHandle.In, &pData->WaveBuffer[i], sizeof(WAVEHDR));
        pData->lWaveBuffersCommitted++;
    }

    pDevice->ExtraData = pData;

    pData->hWaveThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)CaptureThreadProc, (LPVOID)pDevice, 0, &pData->ulWaveThreadID);
    if (pData->hWaveThread == NULL)
        goto failure;

    pDevice->szDeviceName = strdup(CaptureDeviceList[lDeviceID]);
    return ALC_TRUE;

failure:
    if(pData->hWaveThread)
        CloseHandle(pData->hWaveThread);

    for(i = 0;i < 4;i++)
    {
        if(pData->WaveBuffer[i].lpData)
        {
            waveInUnprepareHeader(pData->hWaveHandle.In, &pData->WaveBuffer[i], sizeof(WAVEHDR));
            if(i == 0)
                free(pData->WaveBuffer[i].lpData);
        }
    }

    if(pData->pRing)
        DestroyRingBuffer(pData->pRing);

    if(pData->hWaveThreadEvent)
        CloseHandle(pData->hWaveThreadEvent);
    if(pData->hWaveHdrEvent)
        CloseHandle(pData->hWaveHdrEvent);

    if(pData->hWaveHandle.In)
        waveInClose(pData->hWaveHandle.In);

    free(pData);
    pDevice->ExtraData = NULL;
    return ALC_FALSE;
}

static void WinMMCloseCapture(ALCdevice *pDevice)
{
    WinMMData *pData = (WinMMData*)pDevice->ExtraData;
    int i;

    // Call waveOutReset to shutdown wave device
    pData->bWaveShutdown = AL_TRUE;
    waveInReset(pData->hWaveHandle.In);

    // Wait for signal that all Wave Buffers have returned
    WaitForSingleObjectEx(pData->hWaveHdrEvent, 5000, FALSE);

    // Wait for signal that Wave Thread has been destroyed
    WaitForSingleObjectEx(pData->hWaveThreadEvent, 5000, FALSE);

    CloseHandle(pData->hWaveThread);
    pData->hWaveThread = 0;

    // Release the wave buffers
    for(i = 0;i < 4;i++)
    {
        waveInUnprepareHeader(pData->hWaveHandle.In, &pData->WaveBuffer[i], sizeof(WAVEHDR));
        if(i == 0)
            free(pData->WaveBuffer[i].lpData);
        pData->WaveBuffer[i].lpData = NULL;
    }

    DestroyRingBuffer(pData->pRing);
    pData->pRing = NULL;

    // Close the Wave device
    CloseHandle(pData->hWaveThreadEvent);
    pData->hWaveThreadEvent = 0;

    CloseHandle(pData->hWaveHdrEvent);
    pData->hWaveHdrEvent = 0;

    waveInClose(pData->hWaveHandle.In);
    pData->hWaveHandle.In = 0;

    free(pData);
    pDevice->ExtraData = NULL;
}

static void WinMMStartCapture(ALCdevice *pDevice)
{
    WinMMData *pData = (WinMMData*)pDevice->ExtraData;
    waveInStart(pData->hWaveHandle.In);
}

static void WinMMStopCapture(ALCdevice *pDevice)
{
    WinMMData *pData = (WinMMData*)pDevice->ExtraData;
    waveInStop(pData->hWaveHandle.In);
}

static ALCuint WinMMAvailableSamples(ALCdevice *pDevice)
{
    WinMMData *pData = (WinMMData*)pDevice->ExtraData;
    return RingBufferSize(pData->pRing);
}

static void WinMMCaptureSamples(ALCdevice *pDevice, ALCvoid *pBuffer, ALCuint lSamples)
{
    WinMMData *pData = (WinMMData*)pDevice->ExtraData;

    if(WinMMAvailableSamples(pDevice) >= lSamples)
        ReadRingBuffer(pData->pRing, pBuffer, lSamples);
    else
        alcSetError(pDevice, ALC_INVALID_VALUE);
}


static BackendFuncs WinMMFuncs = {
    WinMMOpenPlayback,
    WinMMClosePlayback,
    NULL,
    NULL,
    WinMMOpenCapture,
    WinMMCloseCapture,
    WinMMStartCapture,
    WinMMStopCapture,
    WinMMCaptureSamples,
    WinMMAvailableSamples
};

void alcWinMMInit(BackendFuncs *FuncList)
{
    *FuncList = WinMMFuncs;
}

void alcWinMMDeinit()
{
    ALuint lLoop;

    for(lLoop = 0; lLoop < NumCaptureDevices; lLoop++)
        free(CaptureDeviceList[lLoop]);
    free(CaptureDeviceList);
    CaptureDeviceList = NULL;

    NumCaptureDevices = 0;
}

void alcWinMMProbe(int type)
{
    ALuint i;

    if(type == CAPTURE_DEVICE_PROBE)
    {
        ProbeCaptureDevices();
        for(i = 0;i < NumCaptureDevices;i++)
        {
            if(CaptureDeviceList[i])
                AppendCaptureDeviceList(CaptureDeviceList[i]);
        }
    }
}
