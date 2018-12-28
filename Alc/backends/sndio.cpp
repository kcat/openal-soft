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
#include "vector.h"
#include "ringbuffer.h"

#include <sndio.h>


namespace {

static const ALCchar sndio_device[] = "SndIO Default";


struct SndioPlayback final : public ALCbackend {
    SndioPlayback(ALCdevice *device) noexcept : ALCbackend{device} { }
    ~SndioPlayback() override;

    int mixerProc();

    sio_hdl *mSndHandle{nullptr};

    al::vector<ALubyte> mBuffer;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    static constexpr inline const char *CurrentPrefix() noexcept { return "SndioPlayback::"; }
};

void SndioPlayback_Construct(SndioPlayback *self, ALCdevice *device);
void SndioPlayback_Destruct(SndioPlayback *self);
ALCenum SndioPlayback_open(SndioPlayback *self, const ALCchar *name);
ALCboolean SndioPlayback_reset(SndioPlayback *self);
ALCboolean SndioPlayback_start(SndioPlayback *self);
void SndioPlayback_stop(SndioPlayback *self);
DECLARE_FORWARD2(SndioPlayback, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
DECLARE_FORWARD(SndioPlayback, ALCbackend, ALCuint, availableSamples)
DECLARE_FORWARD(SndioPlayback, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(SndioPlayback, ALCbackend, void, lock)
DECLARE_FORWARD(SndioPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(SndioPlayback)

DEFINE_ALCBACKEND_VTABLE(SndioPlayback);

void SndioPlayback_Construct(SndioPlayback *self, ALCdevice *device)
{
    new (self) SndioPlayback{device};
    SET_VTABLE2(SndioPlayback, ALCbackend, self);
}

void SndioPlayback_Destruct(SndioPlayback *self)
{ self->~SndioPlayback(); }

SndioPlayback::~SndioPlayback()
{
    if(mSndHandle)
        sio_close(mSndHandle);
    mSndHandle = nullptr;
}

int SndioPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    const ALsizei frameSize{mDevice->frameSizeFromFmt()};

    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        auto WritePtr = static_cast<ALubyte*>(mBuffer.data());
        size_t len{mBuffer.size()};

        SndioPlayback_lock(this);
        aluMixData(mDevice, WritePtr, len/frameSize);
        SndioPlayback_unlock(this);
        while(len > 0 && !mKillNow.load(std::memory_order_acquire))
        {
            size_t wrote{sio_write(mSndHandle, WritePtr, len)};
            if(wrote == 0)
            {
                ERR("sio_write failed\n");
                SndioPlayback_lock(this);
                aluHandleDisconnect(mDevice, "Failed to write playback samples");
                SndioPlayback_unlock(this);
                break;
            }

            len -= wrote;
            WritePtr += wrote;
        }
    }

    return 0;
}


ALCenum SndioPlayback_open(SndioPlayback *self, const ALCchar *name)
{
    ALCdevice *device{self->mDevice};

    if(!name)
        name = sndio_device;
    else if(strcmp(name, sndio_device) != 0)
        return ALC_INVALID_VALUE;

    self->mSndHandle = sio_open(nullptr, SIO_PLAY, 0);
    if(self->mSndHandle == nullptr)
    {
        ERR("Could not open device\n");
        return ALC_INVALID_VALUE;
    }

    device->DeviceName = name;
    return ALC_NO_ERROR;
}

ALCboolean SndioPlayback_reset(SndioPlayback *self)
{
    ALCdevice *device{self->mDevice};
    sio_par par;

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

    if(!sio_setpar(self->mSndHandle, &par) || !sio_getpar(self->mSndHandle, &par))
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

    SetDefaultChannelOrder(device);

    device->UpdateSize = par.round;
    device->NumUpdates = (par.bufsz/par.round) + 1;

    self->mBuffer.resize(device->UpdateSize * device->frameSizeFromFmt());
    std::fill(self->mBuffer.begin(), self->mBuffer.end(), 0);

    return ALC_TRUE;
}

ALCboolean SndioPlayback_start(SndioPlayback *self)
{
    if(!sio_start(self->mSndHandle))
    {
        ERR("Error starting playback\n");
        return ALC_FALSE;
    }

    try {
        self->mKillNow.store(false, std::memory_order_release);
        self->mThread = std::thread{std::mem_fn(&SndioPlayback::mixerProc), self};
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Could not create playback thread: %s\n", e.what());
    }
    catch(...) {
    }
    sio_stop(self->mSndHandle);
    return ALC_FALSE;
}

void SndioPlayback_stop(SndioPlayback *self)
{
    if(self->mKillNow.exchange(true, std::memory_order_acq_rel) || !self->mThread.joinable())
        return;
    self->mThread.join();

    if(!sio_stop(self->mSndHandle))
        ERR("Error stopping device\n");
}


struct SndioCapture final : public ALCbackend {
    SndioCapture(ALCdevice *device) noexcept : ALCbackend{device} { }
    ~SndioCapture() override;

    int recordProc();

    sio_hdl *mSndHandle{nullptr};

    RingBufferPtr mRing;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    static constexpr inline const char *CurrentPrefix() noexcept { return "SndioCapture::"; }
};

void SndioCapture_Construct(SndioCapture *self, ALCdevice *device);
void SndioCapture_Destruct(SndioCapture *self);
ALCenum SndioCapture_open(SndioCapture *self, const ALCchar *name);
DECLARE_FORWARD(SndioCapture, ALCbackend, ALCboolean, reset)
ALCboolean SndioCapture_start(SndioCapture *self);
void SndioCapture_stop(SndioCapture *self);
ALCenum SndioCapture_captureSamples(SndioCapture *self, void *buffer, ALCuint samples);
ALCuint SndioCapture_availableSamples(SndioCapture *self);
DECLARE_FORWARD(SndioCapture, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(SndioCapture, ALCbackend, void, lock)
DECLARE_FORWARD(SndioCapture, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(SndioCapture)

DEFINE_ALCBACKEND_VTABLE(SndioCapture);

void SndioCapture_Construct(SndioCapture *self, ALCdevice *device)
{
    new (self) SndioCapture{device};
    SET_VTABLE2(SndioCapture, ALCbackend, self);
}

void SndioCapture_Destruct(SndioCapture *self)
{ self->~SndioCapture(); }

SndioCapture::~SndioCapture()
{
    if(mSndHandle)
        sio_close(mSndHandle);
    mSndHandle = nullptr;
}

int SndioCapture::recordProc()
{
    SetRTPriority();
    althrd_setname(RECORD_THREAD_NAME);

    const ALsizei frameSize{mDevice->frameSizeFromFmt()};

    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        auto data = mRing->getWriteVector();
        size_t todo{data.first.len + data.second.len};
        if(todo == 0)
        {
            static char junk[4096];
            sio_read(mSndHandle, junk,
                minz(sizeof(junk)/frameSize, mDevice->UpdateSize)*frameSize);
            continue;
        }

        size_t total{0u};
        data.first.len  *= frameSize;
        data.second.len *= frameSize;
        todo = minz(todo, mDevice->UpdateSize) * frameSize;
        while(total < todo)
        {
            if(!data.first.len)
                data.first = data.second;

            size_t got{sio_read(mSndHandle, data.first.buf, minz(todo-total, data.first.len))};
            if(!got)
            {
                SndioCapture_lock(this);
                aluHandleDisconnect(mDevice, "Failed to read capture samples");
                SndioCapture_unlock(this);
                break;
            }

            data.first.buf += got;
            data.first.len -= got;
            total += got;
        }
        mRing->writeAdvance(total / frameSize);
    }

    return 0;
}


ALCenum SndioCapture_open(SndioCapture *self, const ALCchar *name)
{
    ALCdevice *device{self->mDevice};
    sio_par par;

    if(!name)
        name = sndio_device;
    else if(strcmp(name, sndio_device) != 0)
        return ALC_INVALID_VALUE;

    self->mSndHandle = sio_open(nullptr, SIO_REC, 0);
    if(self->mSndHandle == nullptr)
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

    if(!sio_setpar(self->mSndHandle, &par) || !sio_getpar(self->mSndHandle, &par))
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

    self->mRing = CreateRingBuffer(device->UpdateSize*device->NumUpdates, par.bps*par.rchan, false);
    if(!self->mRing)
    {
        ERR("Failed to allocate %u-byte ringbuffer\n",
            device->UpdateSize*device->NumUpdates*par.bps*par.rchan);
        return ALC_OUT_OF_MEMORY;
    }

    SetDefaultChannelOrder(device);

    device->DeviceName = name;
    return ALC_NO_ERROR;
}

ALCboolean SndioCapture_start(SndioCapture *self)
{
    if(!sio_start(self->mSndHandle))
    {
        ERR("Error starting playback\n");
        return ALC_FALSE;
    }

    try {
        self->mKillNow.store(false, std::memory_order_release);
        self->mThread = std::thread{std::mem_fn(&SndioCapture::recordProc), self};
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Could not create record thread: %s\n", e.what());
    }
    catch(...) {
    }
    sio_stop(self->mSndHandle);
    return ALC_FALSE;
}

void SndioCapture_stop(SndioCapture *self)
{
    if(self->mKillNow.exchange(true, std::memory_order_acq_rel) || !self->mThread.joinable())
        return;
    self->mThread.join();

    if(!sio_stop(self->mSndHandle))
        ERR("Error stopping device\n");
}

ALCenum SndioCapture_captureSamples(SndioCapture *self, void *buffer, ALCuint samples)
{
    RingBuffer *ring{self->mRing.get()};
    ring->read(buffer, samples);
    return ALC_NO_ERROR;
}

ALCuint SndioCapture_availableSamples(SndioCapture *self)
{
    RingBuffer *ring{self->mRing.get()};
    return ring->readSpace();
}

} // namespace

BackendFactory &SndIOBackendFactory::getFactory()
{
    static SndIOBackendFactory factory{};
    return factory;
}

bool SndIOBackendFactory::init()
{ return true; }

bool SndIOBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback || type == ALCbackend_Capture); }

void SndIOBackendFactory::probe(DevProbe type, std::string *outnames)
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
        return backend;
    }
    if(type == ALCbackend_Capture)
    {
        SndioCapture *backend;
        NEW_OBJ(backend, SndioCapture)(device);
        return backend;
    }

    return nullptr;
}
