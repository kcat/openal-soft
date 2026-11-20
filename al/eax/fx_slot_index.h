#ifndef EAX_FX_SLOT_INDEX_INCLUDED
#define EAX_FX_SLOT_INDEX_INCLUDED

#include <cstdint>
#include <optional>
#include <string_view>

struct AL_GUID;

using EaxFxSlotIndexValue = std::uint8_t;

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
    auto operator==(EaxFxSlotIndex const& lhs, auto const& rhs) noexcept -> bool
    { return static_cast<std::optional<EaxFxSlotIndexValue> const&>(lhs) == rhs; }

    [[nodiscard]] friend constexpr
    auto operator<=>(EaxFxSlotIndex const& lhs, auto const& rhs) noexcept
    { return static_cast<std::optional<EaxFxSlotIndexValue> const&>(lhs) <=> rhs; }
};

#endif // !EAX_FX_SLOT_INDEX_INCLUDED
