#ifndef EAX_FX_SLOT_INDEX_INCLUDED
#define EAX_FX_SLOT_INDEX_INCLUDED

#include <cstddef>
#include <optional>
#include <string_view>

struct AL_GUID;

using EaxFxSlotIndexValue = std::size_t;

class EaxFxSlotIndex : public std::optional<EaxFxSlotIndexValue> {
    [[noreturn]] static
    auto fail(std::string_view message) -> void;

public:
    using std::optional<EaxFxSlotIndexValue>::optional;

    auto operator=(EaxFxSlotIndexValue const& value) & -> EaxFxSlotIndex&
    { set(value); return *this; }
    auto operator=(AL_GUID const& guid) & -> EaxFxSlotIndex&{ set(guid); return *this; }

    auto set(EaxFxSlotIndexValue index) -> void;
    auto set(AL_GUID const& guid) -> void;

    [[nodiscard]] friend constexpr
    auto operator==(EaxFxSlotIndex const&, EaxFxSlotIndex const&) noexcept -> bool = default;
};

#endif // !EAX_FX_SLOT_INDEX_INCLUDED
