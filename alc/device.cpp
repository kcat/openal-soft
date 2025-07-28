
#include "config.h"

#include "device.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <bit>
#include <csignal>
#include <cstddef>
#include <numeric>

#include "al/buffer.h"
#include "al/effect.h"
#include "al/filter.h"
#include "alnumeric.h"
#include "atomic.h"
#include "backends/base.h"
#include "core/devformat.h"
#include "core/hrtf.h"
#include "core/logging.h"
#include "core/mastering.h"
#include "flexarray.h"
#include "gsl/gsl"


namespace {

using voidp = void*;

} // namespace

namespace al {

Device::Device(DeviceType type) : DeviceBase{type}
{ }

Device::~Device()
{
    TRACE("Freeing device {}", voidp{this});

    Backend = nullptr;

    auto count = std::accumulate(BufferList.cbegin(), BufferList.cend(), 0_uz,
        [](size_t cur, const BufferSubList &sublist) noexcept -> size_t
        { return cur + gsl::narrow_cast<uint>(std::popcount(~sublist.FreeMask)); });
    if(count > 0)
        WARN("{} Buffer{} not deleted", count, (count==1)?"":"s");

    count = std::accumulate(EffectList.cbegin(), EffectList.cend(), 0_uz,
        [](size_t cur, const EffectSubList &sublist) noexcept -> size_t
        { return cur + gsl::narrow_cast<uint>(std::popcount(~sublist.FreeMask)); });
    if(count > 0)
        WARN("{} Effect{} not deleted", count, (count==1)?"":"s");

    count = std::accumulate(FilterList.cbegin(), FilterList.cend(), 0_uz,
        [](size_t cur, const FilterSubList &sublist) noexcept -> size_t
        { return cur + gsl::narrow_cast<uint>(std::popcount(~sublist.FreeMask)); });
    if(count > 0)
        WARN("{} Filter{} not deleted", count, (count==1)?"":"s");
}

void Device::enumerateHrtfs()
{
    mHrtfList = EnumerateHrtf(configValue<std::string>({}, "hrtf-paths"));
    if(auto defhrtfopt = configValue<std::string>({}, "default-hrtf"))
    {
        if(const auto iter = std::ranges::find(mHrtfList, *defhrtfopt); iter == mHrtfList.end())
            WARN("Failed to find default HRTF \"{}\"", *defhrtfopt);
        else if(iter != mHrtfList.begin())
            std::rotate(mHrtfList.begin(), iter, iter+1);
    }
}

auto Device::getOutputMode1() const noexcept -> OutputMode1
{
    if(mContexts.load(std::memory_order_relaxed)->empty())
        return OutputMode1::Any;

    switch(FmtChans)
    {
    case DevFmtMono: return OutputMode1::Mono;
    case DevFmtStereo:
        if(mHrtf)
            return OutputMode1::Hrtf;
        if(std::holds_alternative<UhjPostProcess>(mPostProcess))
            return OutputMode1::Uhj2;
        return OutputMode1::StereoBasic;
    case DevFmtQuad: return OutputMode1::Quad;
    case DevFmtX51: return OutputMode1::X51;
    case DevFmtX61: return OutputMode1::X61;
    case DevFmtX71: return OutputMode1::X71;
    case DevFmtX714:
    case DevFmtX7144:
    case DevFmtX3D71:
    case DevFmtAmbi3D:
        break;
    }
    return OutputMode1::Any;
}


void Device::SetError(Device *device, ALCenum errorCode)
{
    WARN("Error generated on device {}, code {:#04x}", voidp{device}, as_unsigned(errorCode));
    if(sTrapALCError)
    {
#ifdef _WIN32
        /* DebugBreak() will cause an exception if there is no debugger */
        if(IsDebuggerPresent())
            DebugBreak();
#elif defined(SIGTRAP)
        raise(SIGTRAP);
#endif
    }

    if(device)
        device->mLastError.store(errorCode);
    else
        sLastGlobalError.store(errorCode);
}

} // namespace al
