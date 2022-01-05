#ifndef EAX_FX_SLOT_INDEX_INCLUDED
#define EAX_FX_SLOT_INDEX_INCLUDED


#include <cstddef>

#include "eax_api.h"


using EaxFxSlotIndexValue = std::size_t;


class EaxFxSlotIndex
{
public:
    EaxFxSlotIndex() noexcept = default;

    EaxFxSlotIndex(
        EaxFxSlotIndexValue index);

    EaxFxSlotIndex(
        const EaxFxSlotIndex& rhs) noexcept;

    void operator=(
        EaxFxSlotIndexValue index);

    void operator=(
        const GUID& guid);

    void operator=(
        const EaxFxSlotIndex& rhs) noexcept;


    bool has_value() const noexcept;

    EaxFxSlotIndexValue get() const;

    void reset() noexcept;

    void set(
        EaxFxSlotIndexValue index);

    void set(
        const GUID& guid);

    operator EaxFxSlotIndexValue() const;


private:
    [[noreturn]]
    static void fail(
        const char* message);


    bool has_value_{};
    EaxFxSlotIndexValue value_{};
}; // EaxFxSlotIndex


bool operator==(
    const EaxFxSlotIndex& lhs,
    const EaxFxSlotIndex& rhs) noexcept;

bool operator!=(
    const EaxFxSlotIndex& lhs,
    const EaxFxSlotIndex& rhs) noexcept;


#endif // !EAX_FX_SLOT_INDEX_INCLUDED
