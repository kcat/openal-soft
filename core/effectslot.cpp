
#include "config.h"

#include "effectslot.h"

#include <cstddef>

#include "context.h"


std::unique_ptr<EffectSlotArray> EffectSlotBase::CreatePtrArray(std::size_t const count)
{
    return std::unique_ptr<EffectSlotArray>{new(FamCount{count}) EffectSlotArray(count)};
}
