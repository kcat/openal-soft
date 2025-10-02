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

#include "sndio.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <span>
#include <system_error>
#include <thread>
#include <vector>

#include "althrd_setname.h"
#include "core/device.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "gsl/gsl"
#include "ringbuffer.h"

#include <sndio.h>


namespace {

using namespace std::string_view_literals;

[[nodiscard]] constexpr auto GetDefaultName() noexcept { return "SndIO Default"sv; }

struct SioPar : public sio_par {
    SioPar() : sio_par{} { sio_initpar(this); }

    void clear() { sio_initpar(this); }
};

struct SndioPlayback final : public BackendBase {
    explicit SndioPlayback(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~SndioPlayback() override;

    int mixerProc();

    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    sio_hdl *mSndHandle{nullptr};
    uint mFrameStep{};

    std::vector<std::byte> mBuffer;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

SndioPlayback::~SndioPlayback()
{
    if(mSndHandle)
        sio_close(mSndHandle);
    mSndHandle = nullptr;
}

int SndioPlayback::mixerProc()
{
    const size_t frameStep{mFrameStep};
    const size_t frameSize{frameStep * mDevice->bytesFromFmt()};

    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        auto buffer = std::span{mBuffer};

        mDevice->renderSamples(buffer.data(), gsl::narrow_cast<uint>(buffer.size() / frameSize),
            frameStep);
        while(!buffer.empty() && !mKillNow.load(std::memory_order_acquire))
        {
            const auto wrote = sio_write(mSndHandle, buffer.data(), buffer.size());
            if(wrote > buffer.size() || wrote == 0)
            {
                ERR("sio_write failed: {:#x}", wrote);
                mDevice->handleDisconnect("Failed to write playback samples");
                break;
            }
            buffer = buffer.subspan(wrote);
        }
    }

    return 0;
}


void SndioPlayback::open(std::string_view name)
{
    if(name.empty())
        name = GetDefaultName();
    else if(name != GetDefaultName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};

    sio_hdl *sndHandle{sio_open(nullptr, SIO_PLAY, 0)};
    if(!sndHandle)
        throw al::backend_exception{al::backend_error::NoDevice, "Could not open backend device"};

    if(mSndHandle)
        sio_close(mSndHandle);
    mSndHandle = sndHandle;

    mDeviceName = name;
}

bool SndioPlayback::reset()
{
    SioPar par;

    auto tryfmt = mDevice->FmtType;
    while(true)
    {
        switch(tryfmt)
        {
        case DevFmtByte:
            par.bits = 8;
            par.sig = 1;
            break;
        case DevFmtUByte:
            par.bits = 8;
            par.sig = 0;
            break;
        case DevFmtShort:
            par.bits = 16;
            par.sig = 1;
            break;
        case DevFmtUShort:
            par.bits = 16;
            par.sig = 0;
            break;
        case DevFmtFloat:
        case DevFmtInt:
            par.bits = 32;
            par.sig = 1;
            break;
        case DevFmtUInt:
            par.bits = 32;
            par.sig = 0;
            break;
        }
        par.bps = SIO_BPS(par.bits);
        par.le = SIO_LE_NATIVE;
        par.msb = 1;

        par.rate = mDevice->mSampleRate;
        par.pchan = mDevice->channelsFromFmt();

        par.round = mDevice->mUpdateSize;
        par.appbufsz = mDevice->mBufferSize - mDevice->mUpdateSize;
        if(!par.appbufsz) par.appbufsz = mDevice->mUpdateSize;

        try {
            if(!sio_setpar(mSndHandle, &par))
                throw al::backend_exception{al::backend_error::DeviceError,
                    "Failed to set device parameters"};

            par.clear();
            if(!sio_getpar(mSndHandle, &par))
                throw al::backend_exception{al::backend_error::DeviceError,
                    "Failed to get device parameters"};

            if(par.bps > 1 && par.le != SIO_LE_NATIVE)
                throw al::backend_exception{al::backend_error::DeviceError,
                    "{}-endian samples not supported", par.le ? "Little" : "Big"};
            if(par.bits < par.bps*8 && !par.msb)
                throw al::backend_exception{al::backend_error::DeviceError,
                    "MSB-padded samples not supported ({} of {} bits)", par.bits, par.bps*8};
            if(par.pchan < 1)
                throw al::backend_exception{al::backend_error::DeviceError,
                    "No playback channels on device"};

            break;
        }
        catch(al::backend_exception &e) {
            if(tryfmt == DevFmtShort)
                throw;
            par.clear();
            tryfmt = DevFmtShort;
        }
    }

    if(par.bps == 1)
        mDevice->FmtType = (par.sig==1) ? DevFmtByte : DevFmtUByte;
    else if(par.bps == 2)
        mDevice->FmtType = (par.sig==1) ? DevFmtShort : DevFmtUShort;
    else if(par.bps == 4)
        mDevice->FmtType = (par.sig==1) ? DevFmtInt : DevFmtUInt;
    else
        throw al::backend_exception{al::backend_error::DeviceError,
            "Unhandled sample format: {} {}-bit", (par.sig?"signed":"unsigned"), par.bps*8};

    mFrameStep = par.pchan;
    if(par.pchan != mDevice->channelsFromFmt())
    {
        WARN("Got {} channel{} for {}", par.pchan, (par.pchan==1)?"":"s",
            DevFmtChannelsString(mDevice->FmtChans));
        if(par.pchan < 2) mDevice->FmtChans = DevFmtMono;
        else mDevice->FmtChans = DevFmtStereo;
    }
    mDevice->mSampleRate = par.rate;

    setDefaultChannelOrder();

    mDevice->mUpdateSize = par.round;
    mDevice->mBufferSize = par.bufsz + par.round;

    mBuffer.resize(size_t{mDevice->mUpdateSize} * par.pchan*par.bps);

    return true;
}

void SndioPlayback::start()
{
    if(!sio_start(mSndHandle))
        throw al::backend_exception{al::backend_error::DeviceError, "Error starting playback"};

    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{&SndioPlayback::mixerProc, this};
    }
    catch(std::exception& e) {
        sio_stop(mSndHandle);
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: {}", e.what()};
    }
}

void SndioPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    if(!sio_stop(mSndHandle))
        ERR("Error stopping device");
}


/* TODO: This could be improved by avoiding the ring buffer and record thread,
 * counting the available samples with the sio_onmove callback and reading
 * directly from the device. However, this depends on reasonable support for
 * capture buffer sizes apps may request.
 */
struct SndioCapture final : public BackendBase {
    explicit SndioCapture(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~SndioCapture() override;

    void recordProc();

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    uint availableSamples() override;

    sio_hdl *mSndHandle{nullptr};

    RingBufferPtr<std::byte> mRing;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

SndioCapture::~SndioCapture()
{
    if(mSndHandle)
        sio_close(mSndHandle);
    mSndHandle = nullptr;
}

void SndioCapture::recordProc()
{
    SetRTPriority();
    althrd_setname(GetRecordThreadName());

    const uint frameSize{mDevice->frameSizeFromFmt()};

    const auto nfds_pre = sio_nfds(mSndHandle);
    if(nfds_pre <= 0)
    {
        mDevice->handleDisconnect("Incorrect return value from sio_nfds(): {}", nfds_pre);
        return;
    }

    auto fds = std::vector<pollfd>(gsl::narrow_cast<uint>(nfds_pre));

    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        /* Wait until there's some samples to read. */
        const auto nfds = sio_pollfd(mSndHandle, fds.data(), POLLIN);
        if(nfds <= 0)
        {
            mDevice->handleDisconnect("Failed to get polling fds: {}", nfds);
            break;
        }
        const auto pollres = ::poll(fds.data(), fds.size(), 2000);
        if(pollres < 0)
        {
            if(errno == EINTR) continue;
            mDevice->handleDisconnect("Poll error: {}", std::generic_category().message(errno));
            break;
        }
        if(pollres == 0)
            continue;

        const auto revents = sio_revents(mSndHandle, fds.data());
        if((revents&POLLHUP))
        {
            mDevice->handleDisconnect("Got POLLHUP from poll events");
            break;
        }
        if(!(revents&POLLIN))
            continue;

        auto buffer = mRing->getWriteVector()[0];
        while(!buffer.empty())
        {
            const auto got = sio_read(mSndHandle, buffer.data(), buffer.size());
            if(got == 0)
                break;
            if(got > buffer.size())
            {
                ERR("sio_read failed: {:#x}", got);
                mDevice->handleDisconnect("sio_read failed: {:#x}", got);
                break;
            }

            mRing->writeAdvance(got / frameSize);
            buffer = buffer.subspan(got);
            if(buffer.empty())
                buffer = mRing->getWriteVector()[0];
        }
        if(buffer.empty())
        {
            /* Got samples to read, but no place to store it. Drop it. */
            static auto junk = std::array<char,4096>{};
            sio_read(mSndHandle, junk.data(), junk.size() - (junk.size()%frameSize));
        }
    }
}


void SndioCapture::open(std::string_view name)
{
    if(name.empty())
        name = GetDefaultName();
    else if(name != GetDefaultName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};

    mSndHandle = sio_open(nullptr, SIO_REC, true);
    if(mSndHandle == nullptr)
        throw al::backend_exception{al::backend_error::NoDevice, "Could not open backend device"};

    SioPar par;
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
    case DevFmtFloat:
        throw al::backend_exception{al::backend_error::DeviceError,
            "{} capture samples not supported", DevFmtTypeString(mDevice->FmtType)};
    }
    par.bps = SIO_BPS(par.bits);
    par.le = SIO_LE_NATIVE;
    par.msb = 1;
    par.rchan = mDevice->channelsFromFmt();
    par.rate = mDevice->mSampleRate;

    par.appbufsz = std::max(mDevice->mBufferSize, mDevice->mSampleRate/10u);
    par.round = std::min(par.appbufsz/2u, mDevice->mSampleRate/40u);

    if(!sio_setpar(mSndHandle, &par) || !sio_getpar(mSndHandle, &par))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to set device parameters"};

    if(par.bps > 1 && par.le != SIO_LE_NATIVE)
        throw al::backend_exception{al::backend_error::DeviceError,
            "{}-endian samples not supported", par.le ? "Little" : "Big"};
    if(par.bits < par.bps*8 && !par.msb)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Padded samples not supported (got {} of {} bits)", par.bits, par.bps*8};

    auto match_fmt = [](DevFmtType fmttype, const sio_par &p) -> bool
    {
        return (fmttype == DevFmtByte && p.bps == 1 && p.sig != 0)
            || (fmttype == DevFmtUByte && p.bps == 1 && p.sig == 0)
            || (fmttype == DevFmtShort && p.bps == 2 && p.sig != 0)
            || (fmttype == DevFmtUShort && p.bps == 2 && p.sig == 0)
            || (fmttype == DevFmtInt && p.bps == 4 && p.sig != 0)
            || (fmttype == DevFmtUInt && p.bps == 4 && p.sig == 0);
    };
    if(!match_fmt(mDevice->FmtType, par) || mDevice->channelsFromFmt() != par.rchan
        || mDevice->mSampleRate != par.rate)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to set format {} {} {}hz, got {}{} {}-channel {}hz instead",
            DevFmtTypeString(mDevice->FmtType), DevFmtChannelsString(mDevice->FmtChans),
            mDevice->mSampleRate, par.sig?'s':'u', par.bps*8, par.rchan, par.rate};

    mRing = RingBuffer<std::byte>::Create(mDevice->mBufferSize, size_t{par.bps}*par.rchan, false);
    mDevice->mBufferSize = gsl::narrow_cast<uint>(mRing->writeSpace());
    mDevice->mUpdateSize = par.round;

    setDefaultChannelOrder();

    mDeviceName = name;
}

void SndioCapture::start()
{
    if(!sio_start(mSndHandle))
        throw al::backend_exception{al::backend_error::DeviceError, "Error starting capture"};

    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{&SndioCapture::recordProc, this};
    }
    catch(std::exception& e) {
        sio_stop(mSndHandle);
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start capture thread: {}", e.what()};
    }
}

void SndioCapture::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    if(!sio_stop(mSndHandle))
        ERR("Error stopping device");
}

void SndioCapture::captureSamples(std::span<std::byte> outbuffer)
{ std::ignore = mRing->read(outbuffer); }

auto SndioCapture::availableSamples() -> uint
{ return gsl::narrow_cast<uint>(mRing->readSpace()); }

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

auto SndIOBackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    switch(type)
    {
    case BackendType::Playback:
    case BackendType::Capture:
        return std::vector{std::string{GetDefaultName()}};
    }
    return {};
}

auto SndIOBackendFactory::createBackend(gsl::not_null<DeviceBase*> device, BackendType type)
    -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new SndioPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new SndioCapture{device}};
    return nullptr;
}
