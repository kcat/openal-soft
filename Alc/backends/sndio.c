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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "alu.h"
#include "threads.h"
#include "ringbuffer.h"

#include "backends/base.h"

#include <sndio.h>


static const ALCchar sndio_device[] = "SndIO Default";


typedef struct SndioPlayback {
    DERIVE_FROM_TYPE(ALCbackend);

    struct sio_hdl *sndHandle;

    ALvoid *mix_data;
    ALsizei data_size;

    ATOMIC(int) killNow;
    althrd_t thread;
} SndioPlayback;

static int SndioPlayback_mixerProc(void *ptr);

static void SndioPlayback_Construct(SndioPlayback *self, ALCdevice *device);
static void SndioPlayback_Destruct(SndioPlayback *self);
static ALCenum SndioPlayback_open(SndioPlayback *self, const ALCchar *name);
static ALCboolean SndioPlayback_reset(SndioPlayback *self);
static ALCboolean SndioPlayback_start(SndioPlayback *self);
static void SndioPlayback_stop(SndioPlayback *self);
static DECLARE_FORWARD2(SndioPlayback, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
static DECLARE_FORWARD(SndioPlayback, ALCbackend, ALCuint, availableSamples)
static DECLARE_FORWARD(SndioPlayback, ALCbackend, ClockLatency, getClockLatency)
static DECLARE_FORWARD(SndioPlayback, ALCbackend, void, lock)
static DECLARE_FORWARD(SndioPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(SndioPlayback)

DEFINE_ALCBACKEND_VTABLE(SndioPlayback);


static void SndioPlayback_Construct(SndioPlayback *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(SndioPlayback, ALCbackend, self);

    self->sndHandle = NULL;
    self->mix_data = NULL;
    ATOMIC_INIT(&self->killNow, AL_TRUE);
}

static void SndioPlayback_Destruct(SndioPlayback *self)
{
    if(self->sndHandle)
        sio_close(self->sndHandle);
    self->sndHandle = NULL;

    al_free(self->mix_data);
    self->mix_data = NULL;

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


static int SndioPlayback_mixerProc(void *ptr)
{
    SndioPlayback *self = (SndioPlayback*)ptr;
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    ALsizei frameSize;
    size_t wrote;

    SetRTPriority();
    althrd_setname(althrd_current(), MIXER_THREAD_NAME);

    frameSize = FrameSizeFromDevFmt(device->FmtChans, device->FmtType, device->AmbiOrder);

    while(!ATOMIC_LOAD(&self->killNow, almemory_order_acquire) &&
          ATOMIC_LOAD(&device->Connected, almemory_order_acquire))
    {
        ALsizei len = self->data_size;
        ALubyte *WritePtr = self->mix_data;

        SndioPlayback_lock(self);
        aluMixData(device, WritePtr, len/frameSize);
        SndioPlayback_unlock(self);
        while(len > 0 && !ATOMIC_LOAD(&self->killNow, almemory_order_acquire))
        {
            wrote = sio_write(self->sndHandle, WritePtr, len);
            if(wrote == 0)
            {
                ERR("sio_write failed\n");
                ALCdevice_Lock(device);
                aluHandleDisconnect(device, "Failed to write playback samples");
                ALCdevice_Unlock(device);
                break;
            }

            len -= wrote;
            WritePtr += wrote;
        }
    }

    return 0;
}


static ALCenum SndioPlayback_open(SndioPlayback *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;

    if(!name)
        name = sndio_device;
    else if(strcmp(name, sndio_device) != 0)
        return ALC_INVALID_VALUE;

    self->sndHandle = sio_open(NULL, SIO_PLAY, 0);
    if(self->sndHandle == NULL)
    {
        ERR("Could not open device\n");
        return ALC_INVALID_VALUE;
    }

    alstr_copy_cstr(&device->DeviceName, name);

    return ALC_NO_ERROR;
}

static ALCboolean SndioPlayback_reset(SndioPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    struct sio_par par;

    sio_initpar(&par);

    par.rate = device->Frequency;
    par.pchan = ((device->FmtChans != DevFmtMono) ? 2 : 1);

    switch(device->FmtType)
    {
        case DevFmtByte:
            par.bits = 8;
            par.sig = 1;
            break;
        case DevFmtUByte:
            par.bits = 8;
            par.sig = 0;
            break;
        case DevFmtFloat:
        case DevFmtShort:
            par.bits = 16;
            par.sig = 1;
            break;
        case DevFmtUShort:
            par.bits = 16;
            par.sig = 0;
            break;
        case DevFmtInt:
            par.bits = 32;
            par.sig = 1;
            break;
        case DevFmtUInt:
            par.bits = 32;
            par.sig = 0;
            break;
    }
    par.le = SIO_LE_NATIVE;

    par.round = device->UpdateSize;
    par.appbufsz = device->UpdateSize * (device->NumUpdates-1);
    if(!par.appbufsz) par.appbufsz = device->UpdateSize;

    if(!sio_setpar(self->sndHandle, &par) || !sio_getpar(self->sndHandle, &par))
    {
        ERR("Failed to set device parameters\n");
        return ALC_FALSE;
    }

    if(par.bits != par.bps*8)
    {
        ERR("Padded samples not supported (%u of %u bits)\n", par.bits, par.bps*8);
        return ALC_FALSE;
    }

    device->Frequency = par.rate;
    device->FmtChans = ((par.pchan==1) ? DevFmtMono : DevFmtStereo);

    if(par.bits == 8 && par.sig == 1)
        device->FmtType = DevFmtByte;
    else if(par.bits == 8 && par.sig == 0)
        device->FmtType = DevFmtUByte;
    else if(par.bits == 16 && par.sig == 1)
        device->FmtType = DevFmtShort;
    else if(par.bits == 16 && par.sig == 0)
        device->FmtType = DevFmtUShort;
    else if(par.bits == 32 && par.sig == 1)
        device->FmtType = DevFmtInt;
    else if(par.bits == 32 && par.sig == 0)
        device->FmtType = DevFmtUInt;
    else
    {
        ERR("Unhandled sample format: %s %u-bit\n", (par.sig?"signed":"unsigned"), par.bits);
        return ALC_FALSE;
    }

    device->UpdateSize = par.round;
    device->NumUpdates = (par.bufsz/par.round) + 1;

    SetDefaultChannelOrder(device);

    return ALC_TRUE;
}

static ALCboolean SndioPlayback_start(SndioPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;

    self->data_size = device->UpdateSize * FrameSizeFromDevFmt(
        device->FmtChans, device->FmtType, device->AmbiOrder
    );
    al_free(self->mix_data);
    self->mix_data = al_calloc(16, self->data_size);

    if(!sio_start(self->sndHandle))
    {
        ERR("Error starting playback\n");
        return ALC_FALSE;
    }

    ATOMIC_STORE(&self->killNow, AL_FALSE, almemory_order_release);
    if(althrd_create(&self->thread, SndioPlayback_mixerProc, self) != althrd_success)
    {
        sio_stop(self->sndHandle);
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void SndioPlayback_stop(SndioPlayback *self)
{
    int res;

    if(ATOMIC_EXCHANGE(&self->killNow, AL_TRUE, almemory_order_acq_rel))
        return;
    althrd_join(self->thread, &res);

    if(!sio_stop(self->sndHandle))
        ERR("Error stopping device\n");

    al_free(self->mix_data);
    self->mix_data = NULL;
}


typedef struct SndioCapture {
    DERIVE_FROM_TYPE(ALCbackend);

    struct sio_hdl *sndHandle;

    ll_ringbuffer_t *ring;

    ATOMIC(int) killNow;
    althrd_t thread;
} SndioCapture;

static int SndioCapture_recordProc(void *ptr);

static void SndioCapture_Construct(SndioCapture *self, ALCdevice *device);
static void SndioCapture_Destruct(SndioCapture *self);
static ALCenum SndioCapture_open(SndioCapture *self, const ALCchar *name);
static DECLARE_FORWARD(SndioCapture, ALCbackend, ALCboolean, reset)
static ALCboolean SndioCapture_start(SndioCapture *self);
static void SndioCapture_stop(SndioCapture *self);
static ALCenum SndioCapture_captureSamples(SndioCapture *self, void *buffer, ALCuint samples);
static ALCuint SndioCapture_availableSamples(SndioCapture *self);
static DECLARE_FORWARD(SndioCapture, ALCbackend, ClockLatency, getClockLatency)
static DECLARE_FORWARD(SndioCapture, ALCbackend, void, lock)
static DECLARE_FORWARD(SndioCapture, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(SndioCapture)

DEFINE_ALCBACKEND_VTABLE(SndioCapture);


static void SndioCapture_Construct(SndioCapture *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(SndioCapture, ALCbackend, self);

    self->sndHandle = NULL;
    self->ring = NULL;
    ATOMIC_INIT(&self->killNow, AL_TRUE);
}

static void SndioCapture_Destruct(SndioCapture *self)
{
    if(self->sndHandle)
        sio_close(self->sndHandle);
    self->sndHandle = NULL;

    ll_ringbuffer_free(self->ring);
    self->ring = NULL;

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


static int SndioCapture_recordProc(void* ptr)
{
    SndioCapture *self = (SndioCapture*)ptr;
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    ALsizei frameSize;

    SetRTPriority();
    althrd_setname(althrd_current(), RECORD_THREAD_NAME);

    frameSize = FrameSizeFromDevFmt(device->FmtChans, device->FmtType, device->AmbiOrder);

    while(!ATOMIC_LOAD(&self->killNow, almemory_order_acquire) &&
          ATOMIC_LOAD(&device->Connected, almemory_order_acquire))
    {
        ll_ringbuffer_data_t data[2];
        size_t total, todo;

        ll_ringbuffer_get_write_vector(self->ring, data);
        todo = data[0].len + data[1].len;
        if(todo == 0)
        {
            static char junk[4096];
            sio_read(self->sndHandle, junk, minz(sizeof(junk)/frameSize, device->UpdateSize)*frameSize);
            continue;
        }

        total = 0;
        data[0].len *= frameSize;
        data[1].len *= frameSize;
        todo = minz(todo, device->UpdateSize) * frameSize;
        while(total < todo)
        {
            size_t got;

            if(!data[0].len)
                data[0] = data[1];

            got = sio_read(self->sndHandle, data[0].buf, minz(todo-total, data[0].len));
            if(!got)
            {
                SndioCapture_lock(self);
                aluHandleDisconnect(device, "Failed to read capture samples");
                SndioCapture_unlock(self);
                break;
            }

            data[0].buf += got;
            data[0].len -= got;
            total += got;
        }
        ll_ringbuffer_write_advance(self->ring, total / frameSize);
    }

    return 0;
}


static ALCenum SndioCapture_open(SndioCapture *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    struct sio_par par;

    if(!name)
        name = sndio_device;
    else if(strcmp(name, sndio_device) != 0)
        return ALC_INVALID_VALUE;

    self->sndHandle = sio_open(NULL, SIO_REC, 0);
    if(self->sndHandle == NULL)
    {
        ERR("Could not open device\n");
        return ALC_INVALID_VALUE;
    }

    sio_initpar(&par);

    switch(device->FmtType)
    {
        case DevFmtByte:
            par.bps = 1;
            par.sig = 1;
            break;
        case DevFmtUByte:
            par.bps = 1;
            par.sig = 0;
            break;
        case DevFmtShort:
            par.bps = 2;
            par.sig = 1;
            break;
        case DevFmtUShort:
            par.bps = 2;
            par.sig = 0;
            break;
        case DevFmtInt:
            par.bps = 4;
            par.sig = 1;
            break;
        case DevFmtUInt:
            par.bps = 4;
            par.sig = 0;
            break;
        case DevFmtFloat:
            ERR("%s capture samples not supported\n", DevFmtTypeString(device->FmtType));
            return ALC_INVALID_VALUE;
    }
    par.bits = par.bps * 8;
    par.le = SIO_LE_NATIVE;
    par.msb = SIO_LE_NATIVE ? 0 : 1;
    par.rchan = ChannelsFromDevFmt(device->FmtChans, device->AmbiOrder);
    par.rate = device->Frequency;

    par.appbufsz = maxu(device->UpdateSize*device->NumUpdates, (device->Frequency+9)/10);
    par.round = clampu(par.appbufsz/device->NumUpdates, (device->Frequency+99)/100,
                       (device->Frequency+19)/20);

    device->UpdateSize = par.round;
    device->NumUpdates = maxu(par.appbufsz/par.round, 1);

    if(!sio_setpar(self->sndHandle, &par) || !sio_getpar(self->sndHandle, &par))
    {
        ERR("Failed to set device parameters\n");
        return ALC_INVALID_VALUE;
    }

    if(par.bits != par.bps*8)
    {
        ERR("Padded samples not supported (%u of %u bits)\n", par.bits, par.bps*8);
        return ALC_INVALID_VALUE;
    }

    if(!((device->FmtType == DevFmtByte && par.bits == 8 && par.sig != 0) ||
         (device->FmtType == DevFmtUByte && par.bits == 8 && par.sig == 0) ||
         (device->FmtType == DevFmtShort && par.bits == 16 && par.sig != 0) ||
         (device->FmtType == DevFmtUShort && par.bits == 16 && par.sig == 0) ||
         (device->FmtType == DevFmtInt && par.bits == 32 && par.sig != 0) ||
         (device->FmtType == DevFmtUInt && par.bits == 32 && par.sig == 0)) ||
       ChannelsFromDevFmt(device->FmtChans, device->AmbiOrder) != (ALsizei)par.rchan ||
       device->Frequency != par.rate)
    {
        ERR("Failed to set format %s %s %uhz, got %c%u %u-channel %uhz instead\n",
            DevFmtTypeString(device->FmtType), DevFmtChannelsString(device->FmtChans),
            device->Frequency, par.sig?'s':'u', par.bits, par.rchan, par.rate);
        return ALC_INVALID_VALUE;
    }

    self->ring = ll_ringbuffer_create(device->UpdateSize*device->NumUpdates, par.bps*par.rchan, 0);
    if(!self->ring)
    {
        ERR("Failed to allocate %u-byte ringbuffer\n",
            device->UpdateSize*device->NumUpdates*par.bps*par.rchan);
        return ALC_OUT_OF_MEMORY;
    }

    SetDefaultChannelOrder(device);

    alstr_copy_cstr(&device->DeviceName, name);

    return ALC_NO_ERROR;
}

static ALCboolean SndioCapture_start(SndioCapture *self)
{
    if(!sio_start(self->sndHandle))
    {
        ERR("Error starting playback\n");
        return ALC_FALSE;
    }

    ATOMIC_STORE(&self->killNow, AL_FALSE, almemory_order_release);
    if(althrd_create(&self->thread, SndioCapture_recordProc, self) != althrd_success)
    {
        sio_stop(self->sndHandle);
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void SndioCapture_stop(SndioCapture *self)
{
    int res;

    if(ATOMIC_EXCHANGE(&self->killNow, AL_TRUE, almemory_order_acq_rel))
        return;
    althrd_join(self->thread, &res);

    if(!sio_stop(self->sndHandle))
        ERR("Error stopping device\n");
}

static ALCenum SndioCapture_captureSamples(SndioCapture *self, void *buffer, ALCuint samples)
{
    ll_ringbuffer_read(self->ring, buffer, samples);
    return ALC_NO_ERROR;
}

static ALCuint SndioCapture_availableSamples(SndioCapture *self)
{
    return ll_ringbuffer_read_space(self->ring);
}


typedef struct SndioBackendFactory {
    DERIVE_FROM_TYPE(ALCbackendFactory);
} SndioBackendFactory;
#define SNDIOBACKENDFACTORY_INITIALIZER { { GET_VTABLE2(SndioBackendFactory, ALCbackendFactory) } }

ALCbackendFactory *SndioBackendFactory_getFactory(void);

static ALCboolean SndioBackendFactory_init(SndioBackendFactory *self);
static DECLARE_FORWARD(SndioBackendFactory, ALCbackendFactory, void, deinit)
static ALCboolean SndioBackendFactory_querySupport(SndioBackendFactory *self, ALCbackend_Type type);
static void SndioBackendFactory_probe(SndioBackendFactory *self, enum DevProbe type, al_string *outnames);
static ALCbackend* SndioBackendFactory_createBackend(SndioBackendFactory *self, ALCdevice *device, ALCbackend_Type type);
DEFINE_ALCBACKENDFACTORY_VTABLE(SndioBackendFactory);

ALCbackendFactory *SndioBackendFactory_getFactory(void)
{
    static SndioBackendFactory factory = SNDIOBACKENDFACTORY_INITIALIZER;
    return STATIC_CAST(ALCbackendFactory, &factory);
}

static ALCboolean SndioBackendFactory_init(SndioBackendFactory* UNUSED(self))
{
    /* No dynamic loading */
    return ALC_TRUE;
}

static ALCboolean SndioBackendFactory_querySupport(SndioBackendFactory* UNUSED(self), ALCbackend_Type type)
{
    if(type == ALCbackend_Playback || type == ALCbackend_Capture)
        return ALC_TRUE;
    return ALC_FALSE;
}

static void SndioBackendFactory_probe(SndioBackendFactory* UNUSED(self), enum DevProbe type, al_string *outnames)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
        case CAPTURE_DEVICE_PROBE:
            alstr_append_range(outnames, sndio_device, sndio_device+sizeof(sndio_device));
            break;
    }
}

static ALCbackend* SndioBackendFactory_createBackend(SndioBackendFactory* UNUSED(self), ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        SndioPlayback *backend;
        NEW_OBJ(backend, SndioPlayback)(device);
        if(!backend) return NULL;
        return STATIC_CAST(ALCbackend, backend);
    }
    if(type == ALCbackend_Capture)
    {
        SndioCapture *backend;
        NEW_OBJ(backend, SndioCapture)(device);
        if(!backend) return NULL;
        return STATIC_CAST(ALCbackend, backend);
    }

    return NULL;
}
