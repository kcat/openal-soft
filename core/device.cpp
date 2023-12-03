
#include "config.h"

#include "bformatdec.h"
#include "bs2b.h"
#include "device.h"
#include "front_stablizer.h"
#include "hrtf.h"
#include "mastering.h"


static_assert(std::atomic<std::chrono::nanoseconds>::is_always_lock_free);


al::FlexArray<ContextBase*> DeviceBase::sEmptyContextArray{0u};


DeviceBase::DeviceBase(DeviceType type) : Type{type}, mContexts{&sEmptyContextArray}
{
}

DeviceBase::~DeviceBase()
{
    auto *oldarray = mContexts.exchange(nullptr, std::memory_order_relaxed);
    if(oldarray != &sEmptyContextArray) delete oldarray;
}
