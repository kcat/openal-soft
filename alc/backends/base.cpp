
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

void BackendBase::setDefaultWFXChannelOrder()
{
    mDevice->RealOut.ChannelIndex.fill(INVALID_CHANNEL_INDEX);

    switch(mDevice->FmtChans)
    {
    case DevFmtMono:
        mDevice->RealOut.ChannelIndex[FrontCenter] = 0;
        break;
    case DevFmtStereo:
        mDevice->RealOut.ChannelIndex[FrontLeft]  = 0;
        mDevice->RealOut.ChannelIndex[FrontRight] = 1;
        break;
    case DevFmtQuad:
        mDevice->RealOut.ChannelIndex[FrontLeft]  = 0;
        mDevice->RealOut.ChannelIndex[FrontRight] = 1;
        mDevice->RealOut.ChannelIndex[BackLeft]   = 2;
        mDevice->RealOut.ChannelIndex[BackRight]  = 3;
        break;
    case DevFmtX51:
        mDevice->RealOut.ChannelIndex[FrontLeft]   = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]  = 1;
        mDevice->RealOut.ChannelIndex[FrontCenter] = 2;
        mDevice->RealOut.ChannelIndex[LFE]         = 3;
        mDevice->RealOut.ChannelIndex[SideLeft]    = 4;
        mDevice->RealOut.ChannelIndex[SideRight]   = 5;
        break;
    case DevFmtX51Rear:
        mDevice->RealOut.ChannelIndex[FrontLeft]   = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]  = 1;
        mDevice->RealOut.ChannelIndex[FrontCenter] = 2;
        mDevice->RealOut.ChannelIndex[LFE]         = 3;
        mDevice->RealOut.ChannelIndex[BackLeft]    = 4;
        mDevice->RealOut.ChannelIndex[BackRight]   = 5;
        break;
    case DevFmtX61:
        mDevice->RealOut.ChannelIndex[FrontLeft]   = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]  = 1;
        mDevice->RealOut.ChannelIndex[FrontCenter] = 2;
        mDevice->RealOut.ChannelIndex[LFE]         = 3;
        mDevice->RealOut.ChannelIndex[BackCenter]  = 4;
        mDevice->RealOut.ChannelIndex[SideLeft]    = 5;
        mDevice->RealOut.ChannelIndex[SideRight]   = 6;
        break;
    case DevFmtX71:
        mDevice->RealOut.ChannelIndex[FrontLeft]   = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]  = 1;
        mDevice->RealOut.ChannelIndex[FrontCenter] = 2;
        mDevice->RealOut.ChannelIndex[LFE]         = 3;
        mDevice->RealOut.ChannelIndex[BackLeft]    = 4;
        mDevice->RealOut.ChannelIndex[BackRight]   = 5;
        mDevice->RealOut.ChannelIndex[SideLeft]    = 6;
        mDevice->RealOut.ChannelIndex[SideRight]   = 7;
        break;
    case DevFmtAmbi3D:
        mDevice->RealOut.ChannelIndex[Aux0] = 0;
        if(mDevice->mAmbiOrder > 0)
        {
            mDevice->RealOut.ChannelIndex[Aux1] = 1;
            mDevice->RealOut.ChannelIndex[Aux2] = 2;
            mDevice->RealOut.ChannelIndex[Aux3] = 3;
        }
        if(mDevice->mAmbiOrder > 1)
        {
            mDevice->RealOut.ChannelIndex[Aux4] = 4;
            mDevice->RealOut.ChannelIndex[Aux5] = 5;
            mDevice->RealOut.ChannelIndex[Aux6] = 6;
            mDevice->RealOut.ChannelIndex[Aux7] = 7;
            mDevice->RealOut.ChannelIndex[Aux8] = 8;
        }
        if(mDevice->mAmbiOrder > 2)
        {
            mDevice->RealOut.ChannelIndex[Aux9]  = 9;
            mDevice->RealOut.ChannelIndex[Aux10] = 10;
            mDevice->RealOut.ChannelIndex[Aux11] = 11;
            mDevice->RealOut.ChannelIndex[Aux12] = 12;
            mDevice->RealOut.ChannelIndex[Aux13] = 13;
            mDevice->RealOut.ChannelIndex[Aux14] = 14;
            mDevice->RealOut.ChannelIndex[Aux15] = 15;
        }
        break;
    }
}

void BackendBase::setDefaultChannelOrder()
{
    mDevice->RealOut.ChannelIndex.fill(INVALID_CHANNEL_INDEX);

    switch(mDevice->FmtChans)
    {
    case DevFmtX51Rear:
        mDevice->RealOut.ChannelIndex[FrontLeft]   = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]  = 1;
        mDevice->RealOut.ChannelIndex[BackLeft]    = 2;
        mDevice->RealOut.ChannelIndex[BackRight]   = 3;
        mDevice->RealOut.ChannelIndex[FrontCenter] = 4;
        mDevice->RealOut.ChannelIndex[LFE]         = 5;
        return;
    case DevFmtX71:
        mDevice->RealOut.ChannelIndex[FrontLeft]   = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]  = 1;
        mDevice->RealOut.ChannelIndex[BackLeft]    = 2;
        mDevice->RealOut.ChannelIndex[BackRight]   = 3;
        mDevice->RealOut.ChannelIndex[FrontCenter] = 4;
        mDevice->RealOut.ChannelIndex[LFE]         = 5;
        mDevice->RealOut.ChannelIndex[SideLeft]    = 6;
        mDevice->RealOut.ChannelIndex[SideRight]   = 7;
        return;

    /* Same as WFX order */
    case DevFmtMono:
    case DevFmtStereo:
    case DevFmtQuad:
    case DevFmtX51:
    case DevFmtX61:
    case DevFmtAmbi3D:
        setDefaultWFXChannelOrder();
        break;
    }
}
