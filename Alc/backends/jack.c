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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include "alMain.h"
#include "alu.h"
#include "threads.h"
#include "compat.h"

#include "backends/base.h"

#include <jack/jack.h>
#include <jack/ringbuffer.h>


static const ALCchar jackDevice[] = "JACK Default";


#ifdef HAVE_DYNLOAD
#define JACK_FUNCS(MAGIC)         \
    MAGIC(jack_client_open);      \
    MAGIC(jack_client_close);     \
    MAGIC(jack_client_name_size); \
    MAGIC(jack_get_client_name);

static void *jack_handle;
#define MAKE_FUNC(f) static __typeof(f) * p##f
JACK_FUNCS(MAKE_FUNC);
#undef MAKE_FUNC

#define jack_client_open pjack_client_open
#define jack_client_close pjack_client_close
#define jack_client_name_size pjack_client_name_size
#define jack_get_client_name pjack_get_client_name
#endif


static ALCboolean jack_load(void)
{
    ALCboolean error = ALC_FALSE;

#ifdef HAVE_DYNLOAD
    if(!jack_handle)
    {
        jack_handle = LoadLib("libjack.so.0");
        if(!jack_handle)
            return ALC_FALSE;

        error = ALC_FALSE;
#define LOAD_FUNC(f) do {                                                     \
    p##f = GetSymbol(jack_handle, #f);                                        \
    if(p##f == NULL) {                                                        \
        error = ALC_TRUE;                                                     \
    }                                                                         \
} while(0)
        JACK_FUNCS(LOAD_FUNC);
#undef LOAD_FUNC

        if(error)
        {
            CloseLib(jack_handle);
            jack_handle = NULL;
            return ALC_FALSE;
        }
    }
#endif

    return !error;
}


typedef struct ALCjackPlayback {
    DERIVE_FROM_TYPE(ALCbackend);

    jack_client_t *Client;

    volatile int killNow;
    althrd_t thread;
} ALCjackPlayback;

static void ALCjackPlayback_Construct(ALCjackPlayback *self, ALCdevice *device);
static void ALCjackPlayback_Destruct(ALCjackPlayback *self);
static ALCenum ALCjackPlayback_open(ALCjackPlayback *self, const ALCchar *name);
static void ALCjackPlayback_close(ALCjackPlayback *self);
static ALCboolean ALCjackPlayback_reset(ALCjackPlayback *self);
static ALCboolean ALCjackPlayback_start(ALCjackPlayback *self);
static void ALCjackPlayback_stop(ALCjackPlayback *self);
static DECLARE_FORWARD2(ALCjackPlayback, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
static DECLARE_FORWARD(ALCjackPlayback, ALCbackend, ALCuint, availableSamples)
static DECLARE_FORWARD(ALCjackPlayback, ALCbackend, ALint64, getLatency)
static DECLARE_FORWARD(ALCjackPlayback, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCjackPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCjackPlayback)

DEFINE_ALCBACKEND_VTABLE(ALCjackPlayback);


static void ALCjackPlayback_Construct(ALCjackPlayback *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCjackPlayback, ALCbackend, self);

    self->Client = NULL;
    self->killNow = 1;
}

static void ALCjackPlayback_Destruct(ALCjackPlayback *self)
{
    if(self->Client)
        jack_client_close(self->Client);
    self->Client = NULL;

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


static ALCenum ALCjackPlayback_open(ALCjackPlayback *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    if(!name)
        name = jackDevice;
    else if(strcmp(name, jackDevice) != 0)
        return ALC_INVALID_VALUE;


    al_string_copy_cstr(&device->DeviceName, name);

    return ALC_INVALID_VALUE;
}

static void ALCjackPlayback_close(ALCjackPlayback *UNUSED(self))
{
}

static ALCboolean ALCjackPlayback_reset(ALCjackPlayback *UNUSED(self))
{
    //jack_set_process_callback (client, process, &data);
    return ALC_FALSE;
}

static ALCboolean ALCjackPlayback_start(ALCjackPlayback *UNUSED(self))
{
    return ALC_FALSE;
}

static void ALCjackPlayback_stop(ALCjackPlayback *self)
{
    int res;

    if(self->killNow)
        return;

    self->killNow = 1;
    althrd_join(self->thread, &res);
}


typedef struct ALCjackBackendFactory {
    DERIVE_FROM_TYPE(ALCbackendFactory);
} ALCjackBackendFactory;
#define ALCJACKBACKENDFACTORY_INITIALIZER { { GET_VTABLE2(ALCjackBackendFactory, ALCbackendFactory) } }

static ALCboolean ALCjackBackendFactory_init(ALCjackBackendFactory* UNUSED(self))
{
    jack_client_t *client;
    jack_status_t status;

    if(!jack_load())
        return ALC_FALSE;

    client = jack_client_open("alsoft", 0, &status, NULL);
    if(client == NULL)
    {
        WARN("jack_client_open() failed, 0x%02x\n", status);
        if((status&JackServerFailed))
            ERR("Unable to connect to JACK server\n");
        return ALC_FALSE;
    }

    jack_client_close(client);
    return ALC_TRUE;
}

static void ALCjackBackendFactory_deinit(ALCjackBackendFactory* UNUSED(self))
{
#ifdef HAVE_DYNLOAD
    if(jack_handle)
        CloseLib(jack_handle);
    jack_handle = NULL;
#endif
}

static ALCboolean ALCjackBackendFactory_querySupport(ALCjackBackendFactory* UNUSED(self), ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
        return ALC_TRUE;
    return ALC_FALSE;
}

static void ALCjackBackendFactory_probe(ALCjackBackendFactory* UNUSED(self), enum DevProbe type)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            AppendAllDevicesList(jackDevice);
            break;

        case CAPTURE_DEVICE_PROBE:
            break;
    }
}

static ALCbackend* ALCjackBackendFactory_createBackend(ALCjackBackendFactory* UNUSED(self), ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCjackPlayback *backend;

        backend = ALCjackPlayback_New(sizeof(*backend));
        if(!backend) return NULL;
        memset(backend, 0, sizeof(*backend));

        ALCjackPlayback_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }

    return NULL;
}

DEFINE_ALCBACKENDFACTORY_VTABLE(ALCjackBackendFactory);


ALCbackendFactory *ALCjackBackendFactory_getFactory(void)
{
    static ALCjackBackendFactory factory = ALCJACKBACKENDFACTORY_INITIALIZER;
    return STATIC_CAST(ALCbackendFactory, &factory);
}
