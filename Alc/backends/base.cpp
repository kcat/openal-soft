
#include "config.h"

#include <stdlib.h>

#include <thread>

#include "alMain.h"
#include "alu.h"

#include "backends/base.h"


void ALCdevice_Lock(ALCdevice *device)
{ device->Backend->lock(); }

void ALCdevice_Unlock(ALCdevice *device)
{ device->Backend->unlock(); }

ClockLatency GetClockLatency(ALCdevice *device)
{
    BackendBase *backend{device->Backend.get()};
    ClockLatency ret{backend->getClockLatency()};
    ret.Latency += device->FixedLatency;
    return ret;
}


/* BackendBase method implementations. */
BackendBase::BackendBase(ALCdevice *device) noexcept : mDevice{device}
{ }

BackendBase::~BackendBase()
{ }

ALCboolean BackendBase::reset()
{ return ALC_FALSE; }

ALCenum BackendBase::captureSamples(void* UNUSED(buffer), ALCuint UNUSED(samples))
{ return ALC_INVALID_DEVICE; }

ALCuint BackendBase::availableSamples()
{ return 0; }

ClockLatency BackendBase::getClockLatency()
{
    ClockLatency ret;

    ALuint refcount;
    do {
        while(((refcount=mDevice->MixCount.load(std::memory_order_acquire))&1))
            std::this_thread::yield();
        ret.ClockTime = GetDeviceClockTime(mDevice);
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != mDevice->MixCount.load(std::memory_order_relaxed));

    /* NOTE: The device will generally have about all but one periods filled at
     * any given time during playback. Without a more accurate measurement from
     * the output, this is an okay approximation.
     */
    ret.Latency  = std::chrono::seconds{mDevice->UpdateSize*maxi(mDevice->NumUpdates-1, 0)};
    ret.Latency /= mDevice->Frequency;

    return ret;
}

void BackendBase::lock() noexcept
{ mMutex.lock(); }

void BackendBase::unlock() noexcept
{ mMutex.unlock(); }
