
#include "config.h"

#include "base.h"

#include <atomic>
#include <thread>

#include "AL/al.h"

#include "alcmain.h"
#include "alexcpt.h"
#include "alnumeric.h"
#include "atomic.h"


bool BackendBase::reset()
{ throw al::backend_exception{ALC_INVALID_DEVICE, "Invalid BackendBase call"}; }

ALCenum BackendBase::captureSamples(al::byte*, ALCuint)
{ return ALC_INVALID_DEVICE; }

ALCuint BackendBase::availableSamples()
{ return 0; }

ClockLatency BackendBase::getClockLatency()
{
    ClockLatency ret;

    ALuint refcount;
    do {
        refcount = mDevice->waitForMix();
        ret.ClockTime = GetDeviceClockTime(mDevice);
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != ReadRef(mDevice->MixCount));

    /* NOTE: The device will generally have about all but one periods filled at
     * any given time during playback. Without a more accurate measurement from
     * the output, this is an okay approximation.
     */
    ret.Latency = std::max(std::chrono::seconds{mDevice->BufferSize-mDevice->UpdateSize},
        std::chrono::seconds::zero());
    ret.Latency /= mDevice->Frequency;

    return ret;
}
