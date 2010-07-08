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
    // MMSYSTEM Capture Device
    ALboolean        bWaveInShutdown;
    HANDLE           hWaveInHdrEvent;
    HANDLE           hWaveInThreadEvent;
    HANDLE           hWaveInThread;
    DWORD            ulWaveInThreadID;
    ALint            lWaveInBuffersCommitted;
    HWAVEIN          hWaveInHandle;
    WAVEHDR          WaveInBuffer[4];
    RingBuffer       *pRing;
} WinMMData;


static ALCchar **CaptureDeviceList;
static ALuint  NumCaptureDevices;

static void ProbeDevices(void)
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
    pData->lWaveInBuffersCommitted--;

    if(pData->bWaveInShutdown == AL_FALSE)
    {
        // Notify Wave Processor Thread that a Wave Header has returned
        PostThreadMessage(pData->ulWaveInThreadID,uMsg,0,dwParam1);
    }
    else
    {
        if(pData->lWaveInBuffersCommitted == 0)
        {
            // Signal Wave Buffers Returned event
            if(pData->hWaveInHdrEvent)
                SetEvent(pData->hWaveInHdrEvent);

            // Post 'Quit' Message to WaveIn Processor Thread
            PostThreadMessage(pData->ulWaveInThreadID,WM_QUIT,0,0);
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
        if(msg.message != WIM_DATA || pData->bWaveInShutdown)
            continue;

        pWaveHdr = ((LPWAVEHDR)msg.lParam);

        WriteRingBuffer(pData->pRing, (ALubyte*)pWaveHdr->lpData,
                        pWaveHdr->dwBytesRecorded/FrameSize);

        // Send buffer back to capture more data
        waveInAddBuffer(pData->hWaveInHandle,pWaveHdr,sizeof(WAVEHDR));
        pData->lWaveInBuffersCommitted++;
    }

    // Signal Wave Thread completed event
    if(pData->hWaveInThreadEvent)
        SetEvent(pData->hWaveInThreadEvent);

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
    ALint lBufferSize;
    ALuint i;

    if(!CaptureDeviceList)
        ProbeDevices();

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

    if (waveInOpen(&pData->hWaveInHandle, lDeviceID, &wfexCaptureFormat, (DWORD_PTR)&WaveInProc, (DWORD_PTR)pDevice, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
        goto failure;

    pData->hWaveInHdrEvent = CreateEvent(NULL, AL_TRUE, AL_FALSE, "WaveInAllHeadersReturned");
    if (pData->hWaveInHdrEvent == NULL)
        goto failure;

    pData->hWaveInThreadEvent = CreateEvent(NULL, AL_TRUE, AL_FALSE, "WaveInThreadDestroyed");
    if (pData->hWaveInThreadEvent == NULL)
        goto failure;

    // Allocate circular memory buffer for the captured audio
    ulCapturedDataSize = pDevice->UpdateSize*pDevice->NumUpdates;

    // Make sure circular buffer is at least 100ms in size
    if(ulCapturedDataSize < (wfexCaptureFormat.nSamplesPerSec / 10))
        ulCapturedDataSize = wfexCaptureFormat.nSamplesPerSec / 10;

    pData->pRing = CreateRingBuffer(wfexCaptureFormat.nBlockAlign, ulCapturedDataSize);
    if(!pData->pRing)
        goto failure;

    pData->lWaveInBuffersCommitted=0;

    // Create 4 Buffers of 50ms each
    lBufferSize = wfexCaptureFormat.nAvgBytesPerSec / 20;
    lBufferSize -= (lBufferSize % wfexCaptureFormat.nBlockAlign);

    for (i=0;i<4;i++)
    {
        memset(&pData->WaveInBuffer[i], 0, sizeof(WAVEHDR));
        pData->WaveInBuffer[i].dwBufferLength = lBufferSize;
        pData->WaveInBuffer[i].lpData = calloc(1,pData->WaveInBuffer[i].dwBufferLength);
        pData->WaveInBuffer[i].dwFlags = 0;
        pData->WaveInBuffer[i].dwLoops = 0;
        waveInPrepareHeader(pData->hWaveInHandle, &pData->WaveInBuffer[i], sizeof(WAVEHDR));
        waveInAddBuffer(pData->hWaveInHandle, &pData->WaveInBuffer[i], sizeof(WAVEHDR));
        pData->lWaveInBuffersCommitted++;
    }

    pDevice->ExtraData = pData;

    pData->hWaveInThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)CaptureThreadProc, (LPVOID)pDevice, 0, &pData->ulWaveInThreadID);
    if (pData->hWaveInThread == NULL)
        goto failure;

    pDevice->szDeviceName = strdup(CaptureDeviceList[lDeviceID]);
    return ALC_TRUE;

failure:
    for (i=0;i<4;i++)
    {
        if(pData->WaveInBuffer[i].lpData)
        {
            waveInUnprepareHeader(pData->hWaveInHandle, &pData->WaveInBuffer[i], sizeof(WAVEHDR));
            free(pData->WaveInBuffer[i].lpData);
        }
    }

    if(pData->hWaveInHandle)
        waveInClose(pData->hWaveInHandle);
    if(pData->hWaveInThread)
        CloseHandle(pData->hWaveInThread);
    if (pData->hWaveInHdrEvent)
        CloseHandle(pData->hWaveInHdrEvent);
    if (pData->hWaveInThreadEvent)
        CloseHandle(pData->hWaveInThreadEvent);

    free(pData);
    pDevice->ExtraData = NULL;
    return ALC_FALSE;
}

static void WinMMCloseCapture(ALCdevice *pDevice)
{
    WinMMData *pData = (WinMMData*)pDevice->ExtraData;
    int i;

    // Call waveOutReset to shutdown wave device
    pData->bWaveInShutdown = AL_TRUE;
    waveInReset(pData->hWaveInHandle);

    // Wait for signal that all Wave Buffers have returned
    WaitForSingleObjectEx(pData->hWaveInHdrEvent, 5000, FALSE);

    // Wait for signal that Wave Thread has been destroyed
    WaitForSingleObjectEx(pData->hWaveInThreadEvent, 5000, FALSE);

    // Release the wave buffers
    for (i=0;i<4;i++)
    {
        waveInUnprepareHeader(pData->hWaveInHandle, &pData->WaveInBuffer[i], sizeof(WAVEHDR));
        free(pData->WaveInBuffer[i].lpData);
    }

    // Close the Wave device
    waveInClose(pData->hWaveInHandle);
    pData->hWaveInHandle = 0;

    CloseHandle(pData->hWaveInThread);
    pData->hWaveInThread = 0;

    if (pData->hWaveInHdrEvent)
    {
        CloseHandle(pData->hWaveInHdrEvent);
        pData->hWaveInHdrEvent = 0;
    }

    if (pData->hWaveInThreadEvent)
    {
        CloseHandle(pData->hWaveInThreadEvent);
        pData->hWaveInThreadEvent = 0;
    }

    free(pData);
    pDevice->ExtraData = NULL;
}

static void WinMMStartCapture(ALCdevice *pDevice)
{
    WinMMData *pData = (WinMMData*)pDevice->ExtraData;
    waveInStart(pData->hWaveInHandle);
}

static void WinMMStopCapture(ALCdevice *pDevice)
{
    WinMMData *pData = (WinMMData*)pDevice->ExtraData;
    waveInStop(pData->hWaveInHandle);
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

    if(type != CAPTURE_DEVICE_PROBE)
        return;

    ProbeDevices();
    for(i = 0;i < NumCaptureDevices;i++)
    {
        if(CaptureDeviceList[i])
            AppendCaptureDeviceList(CaptureDeviceList[i]);
    }
}
