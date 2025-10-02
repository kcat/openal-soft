
#include "config.h"

#include "bformatdec.h"
#include "bs2b.h"
#include "device.h"
#include "front_stablizer.h"
#include "gsl/gsl"
#include "hrtf.h"
#include "mastering.h"


DeviceBase::DeviceBase(DeviceType type)
    : Type{type}, mContexts{al::FlexArray<ContextBase*>::Create(0)}
{
}

DeviceBase::~DeviceBase() = default;

auto DeviceBase::removeContext(ContextBase *context) -> size_t
{
    auto oldarray = std::span{*mContexts.load(std::memory_order_acquire)};
    if(const auto toremove = std::ranges::count(oldarray, context))
    {
        const auto newsize = oldarray.size() - gsl::narrow_cast<size_t>(toremove);
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
        std::ignore = waitForMix();

        return newsize;
    }

    return oldarray.size();
}
