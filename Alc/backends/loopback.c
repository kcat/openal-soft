/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by Chris Robinson
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

#include "alMain.h"
#include "alu.h"

#include "backends/base.h"


typedef struct ALCloopback {
    DERIVE_FROM_TYPE(ALCbackend);
} ALCloopback;
DECLARE_ALCBACKEND_VTABLE(ALCloopback);

static DECLARE_FORWARD(ALCloopback, ALCbackend, void, Destruct)
static DECLARE_FORWARD(ALCloopback, ALCbackend, ALint64, getLatency)
static DECLARE_FORWARD(ALCloopback, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCloopback, ALCbackend, void, unlock)


static void ALCloopback_Construct(ALCloopback *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCloopback, ALCbackend, self);
}


static ALCenum ALCloopback_open(ALCloopback *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    device->DeviceName = strdup(name);
    return ALC_NO_ERROR;
}

static void ALCloopback_close(ALCloopback* UNUSED(self))
{
}

static ALCboolean ALCloopback_reset(ALCloopback *self)
{
    SetDefaultWFXChannelOrder(STATIC_CAST(ALCbackend, self)->mDevice);
    return ALC_TRUE;
}

static ALCboolean ALCloopback_start(ALCloopback* UNUSED(self))
{
    return ALC_TRUE;
}

static void ALCloopback_stop(ALCloopback* UNUSED(self))
{
}

ALCenum ALCloopback_captureSamples(ALCloopback* UNUSED(self), void* UNUSED(buffer), ALCuint UNUSED(samples))
{
    return ALC_INVALID_VALUE;
}

ALCuint ALCloopback_availableSamples(ALCloopback* UNUSED(self))
{
    return 0;
}


static void ALCloopback_Delete(ALCloopback *self)
{
    free(self);
}

DEFINE_ALCBACKEND_VTABLE(ALCloopback);


typedef struct ALCloopbackFactory {
    DERIVE_FROM_TYPE(ALCbackendFactory);
} ALCloopbackFactory;
#define ALCNULLBACKENDFACTORY_INITIALIZER { { GET_VTABLE2(ALCloopbackFactory, ALCbackendFactory) } }

ALCboolean ALCloopbackFactory_init(ALCloopbackFactory* UNUSED(self))
{
    return ALC_TRUE;
}

void ALCloopbackFactory_deinit(ALCloopbackFactory* UNUSED(self))
{
}

ALCboolean ALCloopbackFactory_querySupport(ALCloopbackFactory* UNUSED(self), ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
        return ALC_TRUE;
    return ALC_FALSE;
}

void ALCloopbackFactory_probe(ALCloopbackFactory* UNUSED(self), enum DevProbe UNUSED(type))
{
}

ALCbackend* ALCloopbackFactory_createBackend(ALCloopbackFactory* UNUSED(self), ALCdevice *device, ALCbackend_Type type)
{
    ALCloopback *backend;

    assert(type == ALCbackend_Playback);

    backend = calloc(1, sizeof(*backend));
    if(!backend) return NULL;

    ALCloopback_Construct(backend, device);

    return STATIC_CAST(ALCbackend, backend);
}

DEFINE_ALCBACKENDFACTORY_VTABLE(ALCloopbackFactory);


ALCbackendFactory *ALCloopbackFactory_getFactory(void)
{
    static ALCloopbackFactory factory = ALCNULLBACKENDFACTORY_INITIALIZER;
    return STATIC_CAST(ALCbackendFactory, &factory);
}
