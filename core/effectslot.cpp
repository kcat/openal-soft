
#include "config.h"

#include <cstddef>
#include <memory>

#include "flexarray.h"

#if HAVE_CXXMODULES
import core.context;
import core.effectslot;
#else
#include "context.h"
#include "effectslot.h"
#endif

auto EffectSlotBase::CreatePtrArray(std::size_t const count) -> std::unique_ptr<EffectSlotArray>
{
    return std::unique_ptr<EffectSlotArray>{new(FamCount{count}) EffectSlotArray(count)};
}
