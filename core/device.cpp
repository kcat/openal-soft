
#include "config.h"

#include <algorithm>
#include <span>

#include "bs2b.h"
#include "core/bformatdec.h"
#include "core/mastering.h"

#if HAVE_CXXMODULES
/* HACK: GCC 16 seems to have an issue when trying to instantiate
 * FlexArray<ContextBase*>::Create. Including the device header seems to work
 * around it.
 */
#if !defined(__INTEL_COMPILER) && !defined(__clang__) && defined(__GNUC__) && __GNUC__ == 16
#include "core/device.h"
#endif
import core.context;
import core.device;
import core.front_stablizer;
import core.hrtf;
import core.hrtfstore;
import flexarray;
import gsl;

#else

#include "core/context.h"
#include "core/device.h"
#include "core/front_stablizer.hpp"
#include "core/hrtf.hpp"
#include "core/hrtfstore.hpp"
#include "flexarray.hpp"
#include "gsl/gsl"
#endif


DeviceBase::DeviceBase(DeviceType const type)
    : Type{type}, mContexts{al::FlexArray<ContextBase*>::Create(0)}
{
}

DeviceBase::~DeviceBase() = default;

auto DeviceBase::removeContext(ContextBase *context) -> std::size_t
{
    auto oldarray = std::span{*mContexts.load(std::memory_order_acquire)};
    if(const auto toremove = std::ranges::count(oldarray, context))
    {
        const auto newsize = oldarray.size() - gsl::narrow_cast<std::size_t>(toremove);
        auto newarray = ContextArray::Create(newsize);

        /* Copy the current/old context handles to the new array, excluding the
         * given context.
         */
        std::ranges::copy_if(oldarray, newarray->begin(), [context](const ContextBase *ctx)
        { return ctx != context; });

        /* Store the new context array in the device. Wait for any current mix
         * to finish before deleting the old array.
         */
        auto prevarray = mContexts.exchange(std::move(newarray));
        waitForCurrentMix();

        return newsize;
    }

    return oldarray.size();
}
