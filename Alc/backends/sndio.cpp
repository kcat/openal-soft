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

#include "backends/sndio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <thread>

#include "alMain.h"
#include "alu.h"
#include "threads.h"
#include "ringbuffer.h"

#include <sndio.h>


static const ALCchar sndio_device[] = "SndIO Default";


struct SndioPlayback final : public ALCbackend {
    struct sio_hdl *sndHandle{nullptr};

    ALvoid *mix_data{nullptr};
    ALsizei data_size{0};

    std::atomic<ALenum> mKillNow{AL_TRUE};
    std::thread mThread;
};

static int SndioPlayback_mixerProc(SndioPlayback *self);

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
    new (self) SndioPlayback{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(SndioPlayback, ALCbackend, self);
}

static void SndioPlayback_Destruct(SndioPlayback *self)
{
    if(self->sndHandle)
        sio_close(self->sndHandle);
    self->sndHandle = nullptr;

    al_free(self->mix_data);
    self->mix_data = nullptr;

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~SndioPlayback();
}


static int SndioPlayback_mixerProc(SndioPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    ALsizei frameSize;
    size_t wrote;

    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    frameSize = device->frameSizeFromFmt();

    while(!self->mKillNow.load(std::memory_order_acquire) &&
          device->Connected.load(std::memory_order_acquire))
    {
        ALsizei len = self->data_size;
        ALubyte *WritePtr = static_cast<ALubyte*>(self->mix_data);

        SndioPlayback_lock(self);
        aluMixData(device, WritePtr, len/frameSize);
        SndioPlayback_unlock(self);
        while(len > 0 && !self->mKillNow.load(std::memory_order_acquire))
        {
            wrote = sio_write(self->sndHandle, WritePtr, len);
            if(wrote == 0)
            {
                ERR("sio_write failed\n");
                SndioPlayback_lock(self);
                aluHandleDisconnect(device, "Failed to write playback samples");
                SndioPlayback_unlock(self);
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

    self->sndHandle = sio_open(nullptr, SIO_PLAY, 0);
    if(self->sndHandle == nullptr)
    {
        ERR("Could not open device\n");
        return ALC_INVALID_VALUE;
    }

    device->DeviceName = name;
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

    self->data_size = device->UpdateSize * device->frameSizeFromFmt();
    al_free(self->mix_data);
    self->mix_data = al_calloc(16, self->data_size);

    if(!sio_start(self->sndHandle))
    {
        ERR("Error starting playback\n");
        return ALC_FALSE;
    }

    try {
        self->mKillNow.store(AL_FALSE, std::memory_order_release);
        self->mThread = std::thread(SndioPlayback_mixerProc, self);
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Could not create playback thread: %s\n", e.what());
    }
    catch(...) {
    }
    sio_stop(self->sndHandle);
    return ALC_FALSE;
}

static void SndioPlayback_stop(SndioPlayback *self)
{
    if(self->mKillNow.exchange(AL_TRUE, std::memory_order_acq_rel) || !self->mThread.joinable())
        return;
    self->mThread.join();

    if(!sio_stop(self->sndHandle))
        ERR("Error stopping device\n");

    al_free(self->mix_data);
    self->mix_data = nullptr;
}


struct SndioCapture final : public ALCbackend {
    struct sio_hdl *sndHandle{nullptr};

    RingBufferPtr ring{nullptr};

    std::atomic<ALenum> mKillNow{AL_TRUE};
    std::thread mThread;
};

static int SndioCapture_recordProc(SndioCapture *self);

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
    new (self) SndioCapture{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(SndioCapture, ALCbackend, self);
}

static void SndioCapture_Destruct(SndioCapture *self)
{
    if(self->sndHandle)
        sio_close(self->sndHandle);
    self->sndHandle = nullptr;

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~SndioCapture();
}


static int SndioCapture_recordProc(SndioCapture *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    ALsizei frameSize;

    SetRTPriority();
    althrd_setname(RECORD_THREAD_NAME);

    frameSize = device->frameSizeFromFmt();

    while(!self->mKillNow.load(std::memory_order_acquire) &&
          device->Connected.load(std::memory_order_acquire))
    {
        size_t total, todo;

        auto data = ll_ringbuffer_get_write_vector(self->ring.get());
        todo = data.first.len + data.second.len;
        if(todo == 0)
        {
            static char junk[4096];
            sio_read(self->sndHandle, junk, minz(sizeof(junk)/frameSize, device->UpdateSize)*frameSize);
            continue;
        }

        total = 0;
        data.first.len  *= frameSize;
        data.second.len *= frameSize;
        todo = minz(todo, device->UpdateSize) * frameSize;
        while(total < todo)
        {
            size_t got;

            if(!data.first.len)
                data.first = data.second;

            got = sio_read(self->sndHandle, data.first.buf, minz(todo-total, data.first.len));
            if(!got)
            {
                SndioCapture_lock(self);
                aluHandleDisconnect(device, "Failed to read capture samples");
                SndioCapture_unlock(self);
                break;
            }

            data.first.buf += got;
            data.first.len -= got;
            total += got;
        }
        ll_ringbuffer_write_advance(self->ring.get(), total / frameSize);
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

    self->sndHandle = sio_open(nullptr, SIO_REC, 0);
    if(self->sndHandle == nullptr)
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
    par.rchan = device->channelsFromFmt();
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
       device->channelsFromFmt() != (ALsizei)par.rchan ||
       device->Frequency != par.rate)
    {
        ERR("Failed to set format %s %s %uhz, got %c%u %u-channel %uhz instead\n",
            DevFmtTypeString(device->FmtType), DevFmtChannelsString(device->FmtChans),
            device->Frequency, par.sig?'s':'u', par.bits, par.rchan, par.rate);
        return ALC_INVALID_VALUE;
    }

    self->ring.reset(ll_ringbuffer_create(device->UpdateSize*device->NumUpdates,
        par.bps*par.rchan, false));
    if(!self->ring)
    {
        ERR("Failed to allocate %u-byte ringbuffer\n",
            device->UpdateSize*device->NumUpdates*par.bps*par.rchan);
        return ALC_OUT_OF_MEMORY;
    }

    SetDefaultChannelOrder(device);

    device->DeviceName = name;
    return ALC_NO_ERROR;
}

static ALCboolean SndioCapture_start(SndioCapture *self)
{
    if(!sio_start(self->sndHandle))
    {
        ERR("Error starting playback\n");
        return ALC_FALSE;
    }

    try {
        self->mKillNow.store(AL_FALSE, std::memory_order_release);
        self->mThread = std::thread(SndioCapture_recordProc, self);
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Could not create record thread: %s\n", e.what());
    }
    catch(...) {
    }
    sio_stop(self->sndHandle);
    return ALC_FALSE;
}

static void SndioCapture_stop(SndioCapture *self)
{
    if(self->mKillNow.exchange(AL_TRUE, std::memory_order_acq_rel) || !self->mThread.joinable())
        return;
    self->mThread.join();

    if(!sio_stop(self->sndHandle))
        ERR("Error stopping device\n");
}

static ALCenum SndioCapture_captureSamples(SndioCapture *self, void *buffer, ALCuint samples)
{
    ll_ringbuffer_read(self->ring.get(), static_cast<char*>(buffer), samples);
    return ALC_NO_ERROR;
}

static ALCuint SndioCapture_availableSamples(SndioCapture *self)
{
    return ll_ringbuffer_read_space(self->ring.get());
}


BackendFactory &SndIOBackendFactory::getFactory()
{
    static SndIOBackendFactory factory{};
    return factory;
}

bool SndIOBackendFactory::init()
{ return true; }

bool SndIOBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback || type == ALCbackend_Capture); }

void SndIOBackendFactory::probe(enum DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
        case CAPTURE_DEVICE_PROBE:
            /* Includes null char. */
            outnames->append(sndio_device, sizeof(sndio_device));
            break;
    }
}

ALCbackend *SndIOBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        SndioPlayback *backend;
        NEW_OBJ(backend, SndioPlayback)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }
    if(type == ALCbackend_Capture)
    {
        SndioCapture *backend;
        NEW_OBJ(backend, SndioCapture)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    return nullptr;
}
