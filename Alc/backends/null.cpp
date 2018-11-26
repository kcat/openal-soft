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

#include "backends/null.h"

#include <stdlib.h>
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#include <chrono>
#include <thread>

#include "alMain.h"
#include "alu.h"
#include "compat.h"


namespace {

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

constexpr ALCchar nullDevice[] = "No Output";


struct ALCnullBackend final : public ALCbackend {
    ATOMIC(int) killNow{AL_TRUE};
    std::thread thread;
};

int ALCnullBackend_mixerProc(ALCnullBackend *self);

void ALCnullBackend_Construct(ALCnullBackend *self, ALCdevice *device);
void ALCnullBackend_Destruct(ALCnullBackend *self);
ALCenum ALCnullBackend_open(ALCnullBackend *self, const ALCchar *name);
ALCboolean ALCnullBackend_reset(ALCnullBackend *self);
ALCboolean ALCnullBackend_start(ALCnullBackend *self);
void ALCnullBackend_stop(ALCnullBackend *self);
DECLARE_FORWARD2(ALCnullBackend, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
DECLARE_FORWARD(ALCnullBackend, ALCbackend, ALCuint, availableSamples)
DECLARE_FORWARD(ALCnullBackend, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(ALCnullBackend, ALCbackend, void, lock)
DECLARE_FORWARD(ALCnullBackend, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCnullBackend)

DEFINE_ALCBACKEND_VTABLE(ALCnullBackend);


void ALCnullBackend_Construct(ALCnullBackend *self, ALCdevice *device)
{
    new (self) ALCnullBackend{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCnullBackend, ALCbackend, self);
}

void ALCnullBackend_Destruct(ALCnullBackend *self)
{
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~ALCnullBackend();
}


int ALCnullBackend_mixerProc(ALCnullBackend *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    const milliseconds restTime{device->UpdateSize*1000/device->Frequency / 2};

    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

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


ALCenum ALCnullBackend_open(ALCnullBackend *self, const ALCchar *name)
{
    ALCdevice *device;

    if(!name)
        name = nullDevice;
    else if(strcmp(name, nullDevice) != 0)
        return ALC_INVALID_VALUE;

    device = STATIC_CAST(ALCbackend, self)->mDevice;
    device->DeviceName = name;

    return ALC_NO_ERROR;
}

ALCboolean ALCnullBackend_reset(ALCnullBackend *self)
{
    SetDefaultWFXChannelOrder(STATIC_CAST(ALCbackend, self)->mDevice);
    return ALC_TRUE;
}

ALCboolean ALCnullBackend_start(ALCnullBackend *self)
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

void ALCnullBackend_stop(ALCnullBackend *self)
{
    if(self->killNow.exchange(AL_TRUE, std::memory_order_acq_rel) || !self->thread.joinable())
        return;
    self->thread.join();
}

} // namespace


bool NullBackendFactory::init()
{ return true; }

bool NullBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback); }

void NullBackendFactory::probe(enum DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            /* Includes null char. */
            outnames->append(nullDevice, sizeof(nullDevice));
            break;
        case CAPTURE_DEVICE_PROBE:
            break;
    }
}

ALCbackend *NullBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
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

BackendFactory &NullBackendFactory::getFactory()
{
    static NullBackendFactory factory{};
    return factory;
}
