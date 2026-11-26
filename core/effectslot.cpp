
#include "config.h"

#include <cstddef>
#include <memory>

#if HAVE_CXXMODULES
import core.context;
import core.effectslot;
import flexarray;
#else
#include "core/context.h"
#include "core/effectslot.h"
#include "flexarray.hpp"
#endif

auto EffectSlotBase::CreatePtrArray(std::size_t const count) -> std::unique_ptr<EffectSlotArray>
{
    return std::unique_ptr<EffectSlotArray>{new(FamCount{count}) EffectSlotArray(count)};
}
