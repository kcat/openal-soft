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
#include <functional>

#include "alcmain.h"
#include "alexcpt.h"
#include "alu.h"
#include "threads.h"
#include "vector.h"
#include "ringbuffer.h"

#include <sndio.h>


namespace {

static const ALCchar sndio_device[] = "SndIO Default";


struct SndioPlayback final : public BackendBase {
    SndioPlayback(ALCdevice *device) noexcept : BackendBase{device} { }
    ~SndioPlayback() override;

    int mixerProc();

    void open(const ALCchar *name) override;
    bool reset() override;
    bool start() override;
    void stop() override;

    sio_hdl *mSndHandle{nullptr};

    al::vector<ALubyte> mBuffer;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    DEF_NEWDEL(SndioPlayback)
};

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

    const size_t frameStep{mDevice->channelsFromFmt()};
    const ALuint frameSize{mDevice->frameSizeFromFmt()};

    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        ALubyte *WritePtr{mBuffer.data()};
        size_t len{mBuffer.size()};

        {
            std::lock_guard<SndioPlayback> _{*this};
            aluMixData(mDevice, WritePtr, static_cast<ALuint>(len/frameSize), frameStep);
        }
        while(len > 0 && !mKillNow.load(std::memory_order_acquire))
        {
            size_t wrote{sio_write(mSndHandle, WritePtr, len)};
            if(wrote == 0)
            {
                ERR("sio_write failed\n");
                aluHandleDisconnect(mDevice, "Failed to write playback samples");
                break;
            }

            len -= wrote;
            WritePtr += wrote;
        }
    }

    return 0;
}


void SndioPlayback::open(const ALCchar *name)
{
    if(!name)
        name = sndio_device;
    else if(strcmp(name, sndio_device) != 0)
        throw al::backend_exception{ALC_INVALID_VALUE, "Device name \"%s\" not found", name};

    mSndHandle = sio_open(nullptr, SIO_PLAY, 0);
    if(mSndHandle == nullptr)
        throw al::backend_exception{ALC_INVALID_VALUE, "Could not open backend device"};

    mDevice->DeviceName = name;
}

bool SndioPlayback::reset()
{
    sio_par par;
    sio_initpar(&par);

    par.rate = mDevice->Frequency;
    par.pchan = ((mDevice->FmtChans != DevFmtMono) ? 2 : 1);

    switch(mDevice->FmtType)
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

    par.round = mDevice->UpdateSize;
    par.appbufsz = mDevice->BufferSize - mDevice->UpdateSize;
    if(!par.appbufsz) par.appbufsz = mDevice->UpdateSize;

    if(!sio_setpar(mSndHandle, &par) || !sio_getpar(mSndHandle, &par))
    {
        ERR("Failed to set device parameters\n");
        return false;
    }

    if(par.bits != par.bps*8)
    {
        ERR("Padded samples not supported (%u of %u bits)\n", par.bits, par.bps*8);
        return true;
    }

    mDevice->Frequency = par.rate;
    mDevice->FmtChans = ((par.pchan==1) ? DevFmtMono : DevFmtStereo);

    if(par.bits == 8 && par.sig == 1)
        mDevice->FmtType = DevFmtByte;
    else if(par.bits == 8 && par.sig == 0)
        mDevice->FmtType = DevFmtUByte;
    else if(par.bits == 16 && par.sig == 1)
        mDevice->FmtType = DevFmtShort;
    else if(par.bits == 16 && par.sig == 0)
        mDevice->FmtType = DevFmtUShort;
    else if(par.bits == 32 && par.sig == 1)
        mDevice->FmtType = DevFmtInt;
    else if(par.bits == 32 && par.sig == 0)
        mDevice->FmtType = DevFmtUInt;
    else
    {
        ERR("Unhandled sample format: %s %u-bit\n", (par.sig?"signed":"unsigned"), par.bits);
        return false;
    }

    SetDefaultChannelOrder(mDevice);

    mDevice->UpdateSize = par.round;
    mDevice->BufferSize = par.bufsz + par.round;

    mBuffer.resize(mDevice->UpdateSize * mDevice->frameSizeFromFmt());
    std::fill(mBuffer.begin(), mBuffer.end(), 0);

    return true;
}

bool SndioPlayback::start()
{
    if(!sio_start(mSndHandle))
    {
        ERR("Error starting playback\n");
        return false;
    }

    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&SndioPlayback::mixerProc), this};
        return true;
    }
    catch(std::exception& e) {
        ERR("Could not create playback thread: %s\n", e.what());
    }
    catch(...) {
    }
    sio_stop(mSndHandle);
    return false;
}

void SndioPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    if(!sio_stop(mSndHandle))
        ERR("Error stopping device\n");
}


struct SndioCapture final : public BackendBase {
    SndioCapture(ALCdevice *device) noexcept : BackendBase{device} { }
    ~SndioCapture() override;

    int recordProc();

    void open(const ALCchar *name) override;
    bool start() override;
    void stop() override;
    ALCenum captureSamples(al::byte *buffer, ALCuint samples) override;
    ALCuint availableSamples() override;

    sio_hdl *mSndHandle{nullptr};

    RingBufferPtr mRing;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    DEF_NEWDEL(SndioCapture)
};

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

    const ALuint frameSize{mDevice->frameSizeFromFmt()};

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
                aluHandleDisconnect(mDevice, "Failed to read capture samples");
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


void SndioCapture::open(const ALCchar *name)
{
    if(!name)
        name = sndio_device;
    else if(strcmp(name, sndio_device) != 0)
        throw al::backend_exception{ALC_INVALID_VALUE, "Device name \"%s\" not found", name};

    mSndHandle = sio_open(nullptr, SIO_REC, 0);
    if(mSndHandle == nullptr)
        throw al::backend_exception{ALC_INVALID_VALUE, "Could not open backend device"};

    sio_par par;
    sio_initpar(&par);

    switch(mDevice->FmtType)
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
        throw al::backend_exception{ALC_INVALID_VALUE, "%s capture samples not supported",
            DevFmtTypeString(mDevice->FmtType)};
    }
    par.bits = par.bps * 8;
    par.le = SIO_LE_NATIVE;
    par.msb = SIO_LE_NATIVE ? 0 : 1;
    par.rchan = mDevice->channelsFromFmt();
    par.rate = mDevice->Frequency;

    par.appbufsz = maxu(mDevice->BufferSize, mDevice->Frequency/10);
    par.round = minu(par.appbufsz, mDevice->Frequency/40);

    mDevice->UpdateSize = par.round;
    mDevice->BufferSize = par.appbufsz;

    if(!sio_setpar(mSndHandle, &par) || !sio_getpar(mSndHandle, &par))
        throw al::backend_exception{ALC_INVALID_VALUE, "Failed to set device praameters"};

    if(par.bits != par.bps*8)
        throw al::backend_exception{ALC_INVALID_VALUE,
            "Padded samples not supported (got %u of %u bits)", par.bits, par.bps*8};

    if(!((mDevice->FmtType == DevFmtByte && par.bits == 8 && par.sig != 0)
        || (mDevice->FmtType == DevFmtUByte && par.bits == 8 && par.sig == 0)
        || (mDevice->FmtType == DevFmtShort && par.bits == 16 && par.sig != 0)
        || (mDevice->FmtType == DevFmtUShort && par.bits == 16 && par.sig == 0)
        || (mDevice->FmtType == DevFmtInt && par.bits == 32 && par.sig != 0)
        || (mDevice->FmtType == DevFmtUInt && par.bits == 32 && par.sig == 0))
        || mDevice->channelsFromFmt() != par.rchan || mDevice->Frequency != par.rate)
        throw al::backend_exception{ALC_INVALID_VALUE,
            "Failed to set format %s %s %uhz, got %c%u %u-channel %uhz instead",
            DevFmtTypeString(mDevice->FmtType), DevFmtChannelsString(mDevice->FmtChans),
            mDevice->Frequency, par.sig?'s':'u', par.bits, par.rchan, par.rate};

    mRing = RingBuffer::Create(mDevice->BufferSize, par.bps*par.rchan, false);

    SetDefaultChannelOrder(mDevice);

    mDevice->DeviceName = name;
}

bool SndioCapture::start()
{
    if(!sio_start(mSndHandle))
    {
        ERR("Error starting playback\n");
        return false;
    }

    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&SndioCapture::recordProc), this};
        return true;
    }
    catch(std::exception& e) {
        ERR("Could not create record thread: %s\n", e.what());
    }
    catch(...) {
    }
    sio_stop(mSndHandle);
    return false;
}

void SndioCapture::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    if(!sio_stop(mSndHandle))
        ERR("Error stopping device\n");
}

ALCenum SndioCapture::captureSamples(al::byte *buffer, ALCuint samples)
{
    mRing->read(buffer, samples);
    return ALC_NO_ERROR;
}

ALCuint SndioCapture::availableSamples()
{ return static_cast<ALCuint>(mRing->readSpace()); }

} // namespace

BackendFactory &SndIOBackendFactory::getFactory()
{
    static SndIOBackendFactory factory{};
    return factory;
}

bool SndIOBackendFactory::init()
{ return true; }

bool SndIOBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback || type == BackendType::Capture); }

void SndIOBackendFactory::probe(DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case DevProbe::Playback:
        case DevProbe::Capture:
            /* Includes null char. */
            outnames->append(sndio_device, sizeof(sndio_device));
            break;
    }
}

BackendPtr SndIOBackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new SndioPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new SndioCapture{device}};
    return nullptr;
}
