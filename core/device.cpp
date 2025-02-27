
#include "config.h"

#include "bformatdec.h"
#include "bs2b.h"
#include "device.h"
#include "front_stablizer.h"
#include "hrtf.h"
#include "mastering.h"


DeviceBase::DeviceBase(DeviceType type)
    : Type{type}, mContexts{al::FlexArray<ContextBase*>::Create(0)}
{
}

DeviceBase::~DeviceBase() = default;
