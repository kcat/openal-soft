#include "config.h"

#include "fx_slots.h"

#include <array>

#include "exception.h"


namespace
{

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
class EaxFxSlotsException : public EaxException {
public:
    explicit EaxFxSlotsException(const std::string_view message)
        : EaxException{"EAX_FX_SLOTS", message}
    { }
};

} // namespace


void EaxFxSlots::initialize(gsl::strict_not_null<al::Context*> al_context)
{
    auto fx_slot_index = EaxFxSlotIndexValue{};

    for(auto& fx_slot : fx_slots_)
    {
        fx_slot = eax_create_al_effect_slot(al_context);
        fx_slot->eax_initialize(fx_slot_index);
        fx_slot_index += 1;
    }
}

void EaxFxSlots::uninitialize() noexcept
{
    for (auto& fx_slot : fx_slots_)
    {
        fx_slot = nullptr;
    }
}

const ALeffectslot& EaxFxSlots::get(EaxFxSlotIndex index) const
{
    if(!index.has_value())
        fail("Empty index.");
    return *fx_slots_[index.value()];
}

ALeffectslot& EaxFxSlots::get(EaxFxSlotIndex index)
{
    if(!index.has_value())
        fail("Empty index.");
    return *fx_slots_[index.value()];
}

[[noreturn]]
void EaxFxSlots::fail(const std::string_view message)
{ throw EaxFxSlotsException{message}; }
