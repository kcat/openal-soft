
#include "config.h"

#include "effectslot.h"

#include <cstddef>

#include "almalloc.h"
#include "context.h"


EffectSlotArray *EffectSlot::CreatePtrArray(size_t count) noexcept
{
    /* Allocate space for twice as many pointers, so the mixer has scratch
     * space to store a sorted list during mixing.
     */
    static constexpr auto AlignVal = std::align_val_t{alignof(EffectSlotArray)};
    if(gsl::owner<void*> ptr{::operator new[](EffectSlotArray::Sizeof(count*2), AlignVal)})
        return al::construct_at(static_cast<EffectSlotArray*>(ptr), count);
    return nullptr;
}
