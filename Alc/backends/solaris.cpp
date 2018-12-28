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

#include "backends/solaris.h"

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <math.h>

#include <thread>

#include "alMain.h"
#include "alu.h"
#include "alconfig.h"
#include "threads.h"
#include "vector.h"
#include "compat.h"

#include <sys/audioio.h>


namespace {

constexpr ALCchar solaris_device[] = "Solaris Default";

const char *solaris_driver = "/dev/audio";


struct SolarisBackend final : public ALCbackend {
    SolarisBackend(ALCdevice *device) noexcept : ALCbackend{device} { }
    ~SolarisBackend() override;

    int mixerProc();

    int mFd{-1};

    al::vector<ALubyte> mBuffer;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    static constexpr inline const char *CurrentPrefix() noexcept { return "SolarisBackend::"; }
};

void SolarisBackend_Construct(SolarisBackend *self, ALCdevice *device);
void SolarisBackend_Destruct(SolarisBackend *self);
ALCenum SolarisBackend_open(SolarisBackend *self, const ALCchar *name);
ALCboolean SolarisBackend_reset(SolarisBackend *self);
ALCboolean SolarisBackend_start(SolarisBackend *self);
void SolarisBackend_stop(SolarisBackend *self);
DECLARE_FORWARD2(SolarisBackend, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
DECLARE_FORWARD(SolarisBackend, ALCbackend, ALCuint, availableSamples)
DECLARE_FORWARD(SolarisBackend, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(SolarisBackend, ALCbackend, void, lock)
DECLARE_FORWARD(SolarisBackend, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(SolarisBackend)

DEFINE_ALCBACKEND_VTABLE(SolarisBackend);

void SolarisBackend_Construct(SolarisBackend *self, ALCdevice *device)
{
    new (self) SolarisBackend{device};
    SET_VTABLE2(SolarisBackend, ALCbackend, self);
}

void SolarisBackend_Destruct(SolarisBackend *self)
{ self->~SolarisBackend(); }

SolarisBackend::~SolarisBackend()
{
    if(mFd != -1)
        close(mFd);
    mFd = -1;
}

int SolarisBackend::mixerProc()
{
    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    const int frame_size{mDevice->frameSizeFromFmt()};

    SolarisBackend_lock(this);
    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        pollfd pollitem{};
        pollitem.fd = mFd;
        pollitem.events = POLLOUT;

        SolarisBackend_unlock(this);
        int pret{poll(&pollitem, 1, 1000)};
        SolarisBackend_lock(this);
        if(pret < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
                continue;
            ERR("poll failed: %s\n", strerror(errno));
            aluHandleDisconnect(mDevice, "Failed to wait for playback buffer: %s",
                strerror(errno));
            break;
        }
        else if(pret == 0)
        {
            WARN("poll timeout\n");
            continue;
        }

        ALubyte *write_ptr{mBuffer.data()};
        size_t to_write{mBuffer.size()};
        aluMixData(mDevice, write_ptr, to_write/frame_size);
        while(to_write > 0 && !mKillNow.load(std::memory_order_acquire))
        {
            ssize_t wrote{write(mFd, write_ptr, to_write)};
            if(wrote < 0)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    continue;
                ERR("write failed: %s\n", strerror(errno));
                aluHandleDisconnect(mDevice, "Failed to write playback samples: %s",
                    strerror(errno));
                break;
            }

            to_write -= wrote;
            write_ptr += wrote;
        }
    }
    SolarisBackend_unlock(this);

    return 0;
}


ALCenum SolarisBackend_open(SolarisBackend *self, const ALCchar *name)
{
    if(!name)
        name = solaris_device;
    else if(strcmp(name, solaris_device) != 0)
        return ALC_INVALID_VALUE;

    self->mFd = open(solaris_driver, O_WRONLY);
    if(self->mFd == -1)
    {
        ERR("Could not open %s: %s\n", solaris_driver, strerror(errno));
        return ALC_INVALID_VALUE;
    }

    ALCdevice *device{self->mDevice};
    device->DeviceName = name;

    return ALC_NO_ERROR;
}

ALCboolean SolarisBackend_reset(SolarisBackend *self)
{
    ALCdevice *device{self->mDevice};
    audio_info_t info;
    ALsizei frameSize;
    ALsizei numChannels;

    AUDIO_INITINFO(&info);

    info.play.sample_rate = device->Frequency;

    if(device->FmtChans != DevFmtMono)
        device->FmtChans = DevFmtStereo;
    numChannels = device->channelsFromFmt();
    info.play.channels = numChannels;

    switch(device->FmtType)
    {
        case DevFmtByte:
            info.play.precision = 8;
            info.play.encoding = AUDIO_ENCODING_LINEAR;
            break;
        case DevFmtUByte:
            info.play.precision = 8;
            info.play.encoding = AUDIO_ENCODING_LINEAR8;
            break;
        case DevFmtUShort:
        case DevFmtInt:
        case DevFmtUInt:
        case DevFmtFloat:
            device->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            info.play.precision = 16;
            info.play.encoding = AUDIO_ENCODING_LINEAR;
            break;
    }

    frameSize = numChannels * device->bytesFromFmt();
    info.play.buffer_size = device->UpdateSize*device->NumUpdates * frameSize;

    if(ioctl(self->mFd, AUDIO_SETINFO, &info) < 0)
    {
        ERR("ioctl failed: %s\n", strerror(errno));
        return ALC_FALSE;
    }

    if(device->channelsFromFmt() != (ALsizei)info.play.channels)
    {
        ERR("Failed to set %s, got %u channels instead\n", DevFmtChannelsString(device->FmtChans),
            info.play.channels);
        return ALC_FALSE;
    }

    if(!((info.play.precision == 8 && info.play.encoding == AUDIO_ENCODING_LINEAR8 && device->FmtType == DevFmtUByte) ||
         (info.play.precision == 8 && info.play.encoding == AUDIO_ENCODING_LINEAR && device->FmtType == DevFmtByte) ||
         (info.play.precision == 16 && info.play.encoding == AUDIO_ENCODING_LINEAR && device->FmtType == DevFmtShort) ||
         (info.play.precision == 32 && info.play.encoding == AUDIO_ENCODING_LINEAR && device->FmtType == DevFmtInt)))
    {
        ERR("Could not set %s samples, got %d (0x%x)\n", DevFmtTypeString(device->FmtType),
            info.play.precision, info.play.encoding);
        return ALC_FALSE;
    }

    device->Frequency = info.play.sample_rate;
    device->UpdateSize = (info.play.buffer_size/device->NumUpdates) + 1;

    SetDefaultChannelOrder(device);

    self->mBuffer.resize(device->UpdateSize * device->frameSizeFromFmt());
    std::fill(self->mBuffer.begin(), self->mBuffer.end(), 0);

    return ALC_TRUE;
}

ALCboolean SolarisBackend_start(SolarisBackend *self)
{
    try {
        self->mKillNow.store(false, std::memory_order_release);
        self->mThread = std::thread{std::mem_fn(&SolarisBackend::mixerProc), self};
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Could not create playback thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
}

void SolarisBackend_stop(SolarisBackend *self)
{
    if(self->mKillNow.exchange(true, std::memory_order_acq_rel) || !self->mThread.joinable())
        return;

    self->mThread.join();

    if(ioctl(self->mFd, AUDIO_DRAIN) < 0)
        ERR("Error draining device: %s\n", strerror(errno));
}

} // namespace

BackendFactory &SolarisBackendFactory::getFactory()
{
    static SolarisBackendFactory factory{};
    return factory;
}

bool SolarisBackendFactory::init()
{
    ConfigValueStr(nullptr, "solaris", "device", &solaris_driver);
    return true;
}

bool SolarisBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback); }

void SolarisBackendFactory::probe(DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
        {
#ifdef HAVE_STAT
            struct stat buf;
            if(stat(solaris_driver, &buf) == 0)
#endif
                outnames->append(solaris_device, sizeof(solaris_device));
        }
        break;

        case CAPTURE_DEVICE_PROBE:
            break;
    }
}

ALCbackend *SolarisBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        SolarisBackend *backend;
        NEW_OBJ(backend, SolarisBackend)(device);
        return backend;
    }

    return nullptr;
}
