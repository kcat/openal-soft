/**
 * OpenAL cross platform audio library
 * Copyright (C) 2010 by Chris Robinson
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

#include <stdlib.h>
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#include "alMain.h"
#include "alu.h"
#include "threads.h"
#include "compat.h"

#include "backends/base.h"


typedef struct ALCnullBackend {
    DERIVE_FROM_TYPE(ALCbackend);

    volatile int killNow;
    althread_t thread;
} ALCnullBackend;
DECLARE_ALCBACKEND_VTABLE(ALCnullBackend);

static ALuint ALCnullBackend_mixerProc(ALvoid *ptr);

static void ALCnullBackend_Construct(ALCnullBackend *self, ALCdevice *device);
static DECLARE_FORWARD(ALCnullBackend, ALCbackend, void, Destruct)
static ALCenum ALCnullBackend_open(ALCnullBackend *self, const ALCchar *name);
static void ALCnullBackend_close(ALCnullBackend *self);
static ALCboolean ALCnullBackend_reset(ALCnullBackend *self);
static ALCboolean ALCnullBackend_start(ALCnullBackend *self);
static void ALCnullBackend_stop(ALCnullBackend *self);
static DECLARE_FORWARD2(ALCnullBackend, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
static DECLARE_FORWARD(ALCnullBackend, ALCbackend, ALCuint, availableSamples)
static DECLARE_FORWARD(ALCnullBackend, ALCbackend, ALint64, getLatency)
static DECLARE_FORWARD(ALCnullBackend, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCnullBackend, ALCbackend, void, unlock)

static const ALCchar nullDevice[] = "No Output";


static void ALCnullBackend_Construct(ALCnullBackend *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCnullBackend, ALCbackend, self);
}


static ALuint ALCnullBackend_mixerProc(ALvoid *ptr)
{
    ALCnullBackend *self = (ALCnullBackend*)ptr;
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    ALuint now, start;
    ALuint64 avail, done;

    SetRTPriority();
    SetThreadName(MIXER_THREAD_NAME);

    done = 0;
    start = timeGetTime();
    while(!self->killNow && device->Connected)
    {
        now = timeGetTime();

        avail = (ALuint64)(now-start) * device->Frequency / 1000;
        if(avail < done)
        {
            /* Timer wrapped (50 days???). Add the remainder of the cycle to
             * the available count and reset the number of samples done */
            avail += (U64(1)<<32)*device->Frequency/1000 - done;
            done = 0;
        }
        if(avail-done < device->UpdateSize)
        {
            ALuint restTime = (ALuint)((device->UpdateSize - (avail-done)) * 1000 /
                                       device->Frequency);
            Sleep(restTime);
            continue;
        }

        do {
            aluMixData(device, NULL, device->UpdateSize);
            done += device->UpdateSize;
        } while(avail-done >= device->UpdateSize);
    }

    return 0;
}


static ALCenum ALCnullBackend_open(ALCnullBackend *self, const ALCchar *name)
{
    ALCdevice *device;

    if(!name)
        name = nullDevice;
    else if(strcmp(name, nullDevice) != 0)
        return ALC_INVALID_VALUE;

    device = STATIC_CAST(ALCbackend, self)->mDevice;
    device->DeviceName = strdup(name);

    return ALC_NO_ERROR;
}

static void ALCnullBackend_close(ALCnullBackend* UNUSED(self))
{
}

static ALCboolean ALCnullBackend_reset(ALCnullBackend *self)
{
    SetDefaultWFXChannelOrder(STATIC_CAST(ALCbackend, self)->mDevice);
    return ALC_TRUE;
}

static ALCboolean ALCnullBackend_start(ALCnullBackend *self)
{
    if(!StartThread(&self->thread, ALCnullBackend_mixerProc, self))
        return ALC_FALSE;
    return ALC_TRUE;
}

static void ALCnullBackend_stop(ALCnullBackend *self)
{
    if(!self->thread)
        return;

    self->killNow = 1;
    StopThread(self->thread);
    self->thread = NULL;

    self->killNow = 0;
}


static void ALCnullBackend_Delete(ALCnullBackend *self)
{
    free(self);
}

DEFINE_ALCBACKEND_VTABLE(ALCnullBackend);


typedef struct ALCnullBackendFactory {
    DERIVE_FROM_TYPE(ALCbackendFactory);
} ALCnullBackendFactory;
#define ALCNULLBACKENDFACTORY_INITIALIZER { { GET_VTABLE2(ALCnullBackendFactory, ALCbackendFactory) } }

ALCbackendFactory *ALCnullBackendFactory_getFactory(void);

static ALCboolean ALCnullBackendFactory_init(ALCnullBackendFactory *self);
static DECLARE_FORWARD(ALCnullBackendFactory, ALCbackendFactory, void, deinit)
static ALCboolean ALCnullBackendFactory_querySupport(ALCnullBackendFactory *self, ALCbackend_Type type);
static void ALCnullBackendFactory_probe(ALCnullBackendFactory *self, enum DevProbe type);
static ALCbackend* ALCnullBackendFactory_createBackend(ALCnullBackendFactory *self, ALCdevice *device, ALCbackend_Type type);
DEFINE_ALCBACKENDFACTORY_VTABLE(ALCnullBackendFactory);


ALCbackendFactory *ALCnullBackendFactory_getFactory(void)
{
    static ALCnullBackendFactory factory = ALCNULLBACKENDFACTORY_INITIALIZER;
    return STATIC_CAST(ALCbackendFactory, &factory);
}


static ALCboolean ALCnullBackendFactory_init(ALCnullBackendFactory* UNUSED(self))
{
    return ALC_TRUE;
}

static ALCboolean ALCnullBackendFactory_querySupport(ALCnullBackendFactory* UNUSED(self), ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
        return ALC_TRUE;
    return ALC_FALSE;
}

static void ALCnullBackendFactory_probe(ALCnullBackendFactory* UNUSED(self), enum DevProbe type)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            AppendAllDevicesList(nullDevice);
            break;
        case CAPTURE_DEVICE_PROBE:
            break;
    }
}

static ALCbackend* ALCnullBackendFactory_createBackend(ALCnullBackendFactory* UNUSED(self), ALCdevice *device, ALCbackend_Type type)
{
    ALCnullBackend *backend;

    assert(type == ALCbackend_Playback);

    backend = calloc(1, sizeof(*backend));
    if(!backend) return NULL;

    ALCnullBackend_Construct(backend, device);

    return STATIC_CAST(ALCbackend, backend);
}
