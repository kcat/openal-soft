
#include "config.h"

#include <stdlib.h>

#include <thread>

#include "alMain.h"
#include "alu.h"

#include "backends/base.h"


void ALCdevice_Lock(ALCdevice *device)
{ V0(device->Backend,lock)(); }

void ALCdevice_Unlock(ALCdevice *device)
{ V0(device->Backend,unlock)(); }

ClockLatency GetClockLatency(ALCdevice *device)
{
    ClockLatency ret = V0(device->Backend,getClockLatency)();
    ret.Latency += device->FixedLatency;
    return ret;
}


/* Base ALCbackend method implementations. */
ALCbackend::ALCbackend(ALCdevice *device) noexcept : mDevice{device}
{ }

ALCbackend::~ALCbackend()
{ }

ALCboolean ALCbackend_reset(ALCbackend* UNUSED(self))
{
    return ALC_FALSE;
}

ALCenum ALCbackend_captureSamples(ALCbackend* UNUSED(self), void* UNUSED(buffer), ALCuint UNUSED(samples))
{
    return ALC_INVALID_DEVICE;
}

ALCuint ALCbackend_availableSamples(ALCbackend* UNUSED(self))
{
    return 0;
}

ClockLatency ALCbackend_getClockLatency(ALCbackend *self)
{
    ALCdevice *device = self->mDevice;
    ALuint refcount;
    ClockLatency ret;

    do {
        while(((refcount=device->MixCount.load(std::memory_order_acquire))&1))
            std::this_thread::yield();
        ret.ClockTime = GetDeviceClockTime(device);
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->MixCount.load(std::memory_order_relaxed));

    /* NOTE: The device will generally have about all but one periods filled at
     * any given time during playback. Without a more accurate measurement from
     * the output, this is an okay approximation.
     */
    ret.Latency  = std::chrono::seconds{device->UpdateSize*maxi(device->NumUpdates-1, 0)};
    ret.Latency /= device->Frequency;

    return ret;
}

void ALCbackend_lock(ALCbackend *self)
{
    try {
        self->mMutex.lock();
    }
    catch(...) {
        std::terminate();
    }
}

void ALCbackend_unlock(ALCbackend *self)
{
    try {
        self->mMutex.unlock();
    }
    catch(...) {
        std::terminate();
    }
}
