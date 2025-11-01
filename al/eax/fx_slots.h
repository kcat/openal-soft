#ifndef EAX_FX_SLOTS_INCLUDED
#define EAX_FX_SLOTS_INCLUDED

#include <array>
#include <string_view>

#include "al/auxeffectslot.h"
#include "fx_slot_index.h"
#include "gsl/gsl"

namespace al {
struct Context;
} // namespace al


class EaxFxSlots {
public:
    void initialize(gsl::not_null<al::Context*> al_context);
    void uninitialize() noexcept;

    void commit() const
    {
        for(auto& fx_slot : fx_slots_)
            fx_slot->eax_commit();
    }

    [[nodiscard]] auto get(EaxFxSlotIndex index) const -> const al::EffectSlot&;
    [[nodiscard]] auto get(EaxFxSlotIndex index) -> al::EffectSlot&;

private:
    using Items = std::array<EaxAlEffectSlotUPtr, EAX_MAX_FXSLOTS>;

    Items fx_slots_{};

    [[noreturn]]
    static void fail(const std::string_view message);
}; // EaxFxSlots


#endif // !EAX_FX_SLOTS_INCLUDED
