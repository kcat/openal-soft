
#include "config.h"

#include <cstdlib>

#include <thread>

#include "alMain.h"
#include "alu.h"

#include "backends/base.h"


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

BackendBase::~BackendBase() = default;

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
    ret.Latency  = std::chrono::seconds{maxi(mDevice->BufferSize-mDevice->UpdateSize, 0)};
    ret.Latency /= mDevice->Frequency;

    return ret;
}
