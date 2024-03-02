
#include "config.h"

#include "effectslot.h"

#include <cstddef>

#include "almalloc.h"
#include "context.h"


std::unique_ptr<EffectSlotArray> EffectSlot::CreatePtrArray(size_t count)
{
    /* Allocate space for twice as many pointers, so the mixer has scratch
     * space to store a sorted list during mixing.
     */
    return std::unique_ptr<EffectSlotArray>{new(FamCount{count*2}) EffectSlotArray(count)};
}
