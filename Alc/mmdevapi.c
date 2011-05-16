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


DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

static ALboolean is_loaded = AL_FALSE;


typedef struct {

    volatile int killNow;
    ALvoid *thread;
} MMDevApiData;


static const ALCchar mmDevice[] = "MMDevApi Default";


static void *MMDevApiLoad(void)
{
    return (void*)is_loaded;
}


static ALCboolean MMDevApiOpenPlayback(ALCdevice *device, const ALCchar *deviceName)
{
    MMDevApiData *pData = NULL;
    HRESULT hr = E_FAIL;

    if(!MMDevApiLoad())
        return ALC_FALSE;

    if(!deviceName)
        deviceName = mmDevice;
    else if(strcmp(deviceName, mmDevice) != 0)
        return ALC_FALSE;

    //Initialise requested device
    pData = calloc(1, sizeof(MMDevApiData));
    if(!pData)
    {
        alcSetError(device, ALC_OUT_OF_MEMORY);
        return ALC_FALSE;
    }

    //MMDevApi Init code
    if(FAILED(hr))
    {
        free(pData);
        AL_PRINT("Device init failed: 0x%08lx\n", hr);
        return ALC_FALSE;
    }

    device->szDeviceName = strdup(deviceName);
    device->ExtraData = pData;
    return ALC_TRUE;
}

static void MMDevApiClosePlayback(ALCdevice *device)
{
    MMDevApiData *pData = device->ExtraData;

    free(pData);
    device->ExtraData = NULL;
}

static ALCboolean MMDevApiResetPlayback(ALCdevice *device)
{
    (void)device;
    return ALC_FALSE;
}

static void MMDevApiStopPlayback(ALCdevice *device)
{
    MMDevApiData *pData = device->ExtraData;

    if(!pData->thread)
        return;

    pData->killNow = 1;
    StopThread(pData->thread);
    pData->thread = NULL;

    pData->killNow = 0;

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
    if(is_loaded)
    {
        is_loaded = AL_FALSE;
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
