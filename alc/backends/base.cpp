
#include "config.h"

#include "base.h"

#include <array>
#include <atomic>
#include <utility>

#include "core/devformat.h"


namespace al {

auto backend_exception::make_string(std::string_view fmt, std::format_args args) -> std::string
{ return std::vformat(fmt, std::move(args)); }

} // namespace al


bool BackendBase::reset()
{ throw al::backend_exception{al::backend_error::DeviceError, "Invalid BackendBase call"}; }

void BackendBase::captureSamples(std::span<std::byte> outbuffer [[maybe_unused]])
{ }

auto BackendBase::availableSamples() -> uint
{ return 0u; }

auto BackendBase::getClockLatency() -> ClockLatency
{
    auto ret = ClockLatency{};

    auto refcount = uint{};
    do {
        refcount = mDevice->waitForMix();
        ret.ClockTime = mDevice->getClockTime();
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != mDevice->mMixCount.load(std::memory_order_relaxed));

    /* NOTE: The device will generally have about all but one periods filled at
     * any given time during playback. Without a more accurate measurement from
     * the output, this is an okay approximation.
     */
    ret.Latency = std::chrono::seconds{mDevice->mBufferSize - mDevice->mUpdateSize};
    ret.Latency /= mDevice->mSampleRate;

    return ret;
}

void BackendBase::setDefaultWFXChannelOrder() const
{
    mDevice->RealOut.ChannelIndex.fill(InvalidChannelIndex);

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
    case DevFmtX714:
        mDevice->RealOut.ChannelIndex[FrontLeft]     = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]    = 1;
        mDevice->RealOut.ChannelIndex[FrontCenter]   = 2;
        mDevice->RealOut.ChannelIndex[LFE]           = 3;
        mDevice->RealOut.ChannelIndex[BackLeft]      = 4;
        mDevice->RealOut.ChannelIndex[BackRight]     = 5;
        mDevice->RealOut.ChannelIndex[SideLeft]      = 6;
        mDevice->RealOut.ChannelIndex[SideRight]     = 7;
        mDevice->RealOut.ChannelIndex[TopFrontLeft]  = 8;
        mDevice->RealOut.ChannelIndex[TopFrontRight] = 9;
        mDevice->RealOut.ChannelIndex[TopBackLeft]   = 10;
        mDevice->RealOut.ChannelIndex[TopBackRight]  = 11;
        break;
    case DevFmtX7144:
        mDevice->RealOut.ChannelIndex[FrontLeft]        = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]       = 1;
        mDevice->RealOut.ChannelIndex[FrontCenter]      = 2;
        mDevice->RealOut.ChannelIndex[LFE]              = 3;
        mDevice->RealOut.ChannelIndex[BackLeft]         = 4;
        mDevice->RealOut.ChannelIndex[BackRight]        = 5;
        mDevice->RealOut.ChannelIndex[SideLeft]         = 6;
        mDevice->RealOut.ChannelIndex[SideRight]        = 7;
        mDevice->RealOut.ChannelIndex[TopFrontLeft]     = 8;
        mDevice->RealOut.ChannelIndex[TopFrontRight]    = 9;
        mDevice->RealOut.ChannelIndex[TopBackLeft]      = 10;
        mDevice->RealOut.ChannelIndex[TopBackRight]     = 11;
        mDevice->RealOut.ChannelIndex[BottomFrontLeft]  = 12;
        mDevice->RealOut.ChannelIndex[BottomFrontRight] = 13;
        mDevice->RealOut.ChannelIndex[BottomBackLeft]   = 14;
        mDevice->RealOut.ChannelIndex[BottomBackRight]  = 15;
        break;
    case DevFmtX3D71:
        mDevice->RealOut.ChannelIndex[FrontLeft]   = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]  = 1;
        mDevice->RealOut.ChannelIndex[FrontCenter] = 2;
        mDevice->RealOut.ChannelIndex[LFE]         = 3;
        mDevice->RealOut.ChannelIndex[Aux0]        = 4;
        mDevice->RealOut.ChannelIndex[Aux1]        = 5;
        mDevice->RealOut.ChannelIndex[SideLeft]    = 6;
        mDevice->RealOut.ChannelIndex[SideRight]   = 7;
        break;
    case DevFmtAmbi3D:
        break;
    }
}

void BackendBase::setDefaultChannelOrder() const
{
    mDevice->RealOut.ChannelIndex.fill(InvalidChannelIndex);

    switch(mDevice->FmtChans)
    {
    case DevFmtX51:
        mDevice->RealOut.ChannelIndex[FrontLeft]   = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]  = 1;
        mDevice->RealOut.ChannelIndex[SideLeft]    = 2;
        mDevice->RealOut.ChannelIndex[SideRight]   = 3;
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
    case DevFmtX714:
        mDevice->RealOut.ChannelIndex[FrontLeft]     = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]    = 1;
        mDevice->RealOut.ChannelIndex[BackLeft]      = 2;
        mDevice->RealOut.ChannelIndex[BackRight]     = 3;
        mDevice->RealOut.ChannelIndex[FrontCenter]   = 4;
        mDevice->RealOut.ChannelIndex[LFE]           = 5;
        mDevice->RealOut.ChannelIndex[SideLeft]      = 6;
        mDevice->RealOut.ChannelIndex[SideRight]     = 7;
        mDevice->RealOut.ChannelIndex[TopFrontLeft]  = 8;
        mDevice->RealOut.ChannelIndex[TopFrontRight] = 9;
        mDevice->RealOut.ChannelIndex[TopBackLeft]   = 10;
        mDevice->RealOut.ChannelIndex[TopBackRight]  = 11;
        break;
    case DevFmtX7144:
        mDevice->RealOut.ChannelIndex[FrontLeft]        = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]       = 1;
        mDevice->RealOut.ChannelIndex[BackLeft]         = 2;
        mDevice->RealOut.ChannelIndex[BackRight]        = 3;
        mDevice->RealOut.ChannelIndex[FrontCenter]      = 4;
        mDevice->RealOut.ChannelIndex[LFE]              = 5;
        mDevice->RealOut.ChannelIndex[SideLeft]         = 6;
        mDevice->RealOut.ChannelIndex[SideRight]        = 7;
        mDevice->RealOut.ChannelIndex[TopFrontLeft]     = 8;
        mDevice->RealOut.ChannelIndex[TopFrontRight]    = 9;
        mDevice->RealOut.ChannelIndex[TopBackLeft]      = 10;
        mDevice->RealOut.ChannelIndex[TopBackRight]     = 11;
        mDevice->RealOut.ChannelIndex[BottomFrontLeft]  = 12;
        mDevice->RealOut.ChannelIndex[BottomFrontRight] = 13;
        mDevice->RealOut.ChannelIndex[BottomBackLeft]   = 14;
        mDevice->RealOut.ChannelIndex[BottomBackRight]  = 15;
        break;
    case DevFmtX3D71:
        mDevice->RealOut.ChannelIndex[FrontLeft]   = 0;
        mDevice->RealOut.ChannelIndex[FrontRight]  = 1;
        mDevice->RealOut.ChannelIndex[Aux0]        = 2;
        mDevice->RealOut.ChannelIndex[Aux1]        = 3;
        mDevice->RealOut.ChannelIndex[FrontCenter] = 4;
        mDevice->RealOut.ChannelIndex[LFE]         = 5;
        mDevice->RealOut.ChannelIndex[SideLeft]    = 6;
        mDevice->RealOut.ChannelIndex[SideRight]   = 7;
        return;

    /* Same as WFX order */
    case DevFmtMono:
    case DevFmtStereo:
    case DevFmtQuad:
    case DevFmtX61:
    case DevFmtAmbi3D:
        setDefaultWFXChannelOrder();
        break;
    }
}
