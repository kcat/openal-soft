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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#include <chrono>
#include <thread>

#include "alMain.h"
#include "alu.h"
#include "compat.h"

#include "backends/base.h"


namespace {

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

constexpr ALCchar nullDevice[] = "No Output";

} // namespace

struct ALCnullBackend final : public ALCbackend {
    ATOMIC(int) killNow;
    std::thread thread;
};

static int ALCnullBackend_mixerProc(ALCnullBackend *self);

static void ALCnullBackend_Construct(ALCnullBackend *self, ALCdevice *device);
static void ALCnullBackend_Destruct(ALCnullBackend *self);
static ALCenum ALCnullBackend_open(ALCnullBackend *self, const ALCchar *name);
static ALCboolean ALCnullBackend_reset(ALCnullBackend *self);
static ALCboolean ALCnullBackend_start(ALCnullBackend *self);
static void ALCnullBackend_stop(ALCnullBackend *self);
static DECLARE_FORWARD2(ALCnullBackend, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
static DECLARE_FORWARD(ALCnullBackend, ALCbackend, ALCuint, availableSamples)
static DECLARE_FORWARD(ALCnullBackend, ALCbackend, ClockLatency, getClockLatency)
static DECLARE_FORWARD(ALCnullBackend, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCnullBackend, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCnullBackend)

DEFINE_ALCBACKEND_VTABLE(ALCnullBackend);


static void ALCnullBackend_Construct(ALCnullBackend *self, ALCdevice *device)
{
    new (self) ALCnullBackend{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCnullBackend, ALCbackend, self);

    ATOMIC_INIT(&self->killNow, AL_TRUE);
}

static void ALCnullBackend_Destruct(ALCnullBackend *self)
{
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~ALCnullBackend();
}


static int ALCnullBackend_mixerProc(ALCnullBackend *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    const milliseconds restTime{device->UpdateSize*1000/device->Frequency / 2};

    SetRTPriority();
    althrd_setname(althrd_current(), MIXER_THREAD_NAME);

    ALint64 done{0};
    auto start = std::chrono::steady_clock::now();
    while(!ATOMIC_LOAD(&self->killNow, almemory_order_acquire) &&
          ATOMIC_LOAD(&device->Connected, almemory_order_acquire))
    {
        auto now = std::chrono::steady_clock::now();

        /* This converts from nanoseconds to nanosamples, then to samples. */
        ALint64 avail{std::chrono::duration_cast<seconds>((now-start) * device->Frequency).count()};
        if(avail-done < device->UpdateSize)
        {
            std::this_thread::sleep_for(restTime);
            continue;
        }
        while(avail-done >= device->UpdateSize)
        {
            ALCnullBackend_lock(self);
            aluMixData(device, nullptr, device->UpdateSize);
            ALCnullBackend_unlock(self);
            done += device->UpdateSize;
        }

        /* For every completed second, increment the start time and reduce the
         * samples done. This prevents the difference between the start time
         * and current time from growing too large, while maintaining the
         * correct number of samples to render.
         */
        if(done >= device->Frequency)
        {
            seconds s{done/device->Frequency};
            start += s;
            done -= device->Frequency*s.count();
        }
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
    alstr_copy_cstr(&device->DeviceName, name);

    return ALC_NO_ERROR;
}

static ALCboolean ALCnullBackend_reset(ALCnullBackend *self)
{
    SetDefaultWFXChannelOrder(STATIC_CAST(ALCbackend, self)->mDevice);
    return ALC_TRUE;
}

static ALCboolean ALCnullBackend_start(ALCnullBackend *self)
{
    try {
        ATOMIC_STORE(&self->killNow, AL_FALSE, almemory_order_release);
        self->thread = std::thread(ALCnullBackend_mixerProc, self);
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Failed to start mixing thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
}

static void ALCnullBackend_stop(ALCnullBackend *self)
{
    if(ATOMIC_EXCHANGE(&self->killNow, AL_TRUE, almemory_order_acq_rel) ||
       !self->thread.joinable())
        return;
    self->thread.join();
}


struct ALCnullBackendFactory final : public ALCbackendFactory {
    ALCnullBackendFactory() noexcept;
};

ALCbackendFactory *ALCnullBackendFactory_getFactory(void);

static ALCboolean ALCnullBackendFactory_init(ALCnullBackendFactory *self);
static DECLARE_FORWARD(ALCnullBackendFactory, ALCbackendFactory, void, deinit)
static ALCboolean ALCnullBackendFactory_querySupport(ALCnullBackendFactory *self, ALCbackend_Type type);
static void ALCnullBackendFactory_probe(ALCnullBackendFactory *self, enum DevProbe type, al_string *outnames);
static ALCbackend* ALCnullBackendFactory_createBackend(ALCnullBackendFactory *self, ALCdevice *device, ALCbackend_Type type);
DEFINE_ALCBACKENDFACTORY_VTABLE(ALCnullBackendFactory);

ALCnullBackendFactory::ALCnullBackendFactory() noexcept
  : ALCbackendFactory{GET_VTABLE2(ALCnullBackendFactory, ALCbackendFactory)}
{
}


ALCbackendFactory *ALCnullBackendFactory_getFactory(void)
{
    static ALCnullBackendFactory factory{};
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

static void ALCnullBackendFactory_probe(ALCnullBackendFactory* UNUSED(self), enum DevProbe type, al_string *outnames)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
        case CAPTURE_DEVICE_PROBE:
            alstr_append_range(outnames, nullDevice, nullDevice+sizeof(nullDevice));
            break;
    }
}

static ALCbackend* ALCnullBackendFactory_createBackend(ALCnullBackendFactory* UNUSED(self), ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCnullBackend *backend;
        NEW_OBJ(backend, ALCnullBackend)(device);
        if(!backend) return NULL;
        return STATIC_CAST(ALCbackend, backend);
    }

    return NULL;
}
