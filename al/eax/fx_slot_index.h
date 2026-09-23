#ifndef EAX_FX_SLOT_INDEX_INCLUDED
#define EAX_FX_SLOT_INDEX_INCLUDED


#include <cstddef>
#include <optional>
#include <string_view>

struct AL_GUID;

using EaxFxSlotIndexValue = std::size_t;

class EaxFxSlotIndex : public std::optional<EaxFxSlotIndexValue> {
public:
    using std::optional<EaxFxSlotIndexValue>::optional;

    EaxFxSlotIndex& operator=(const EaxFxSlotIndexValue &value) { set(value); return *this; }
    EaxFxSlotIndex& operator=(AL_GUID const& guid) { set(guid); return *this; }

    void set(EaxFxSlotIndexValue index);
    void set(AL_GUID const& guid);

    [[nodiscard]] friend constexpr
    auto operator==(EaxFxSlotIndex const&, EaxFxSlotIndex const&) noexcept -> bool = default;

private:
    [[noreturn]]
    static void fail(std::string_view message);
}; // EaxFxSlotIndex

#endif // !EAX_FX_SLOT_INDEX_INCLUDED
