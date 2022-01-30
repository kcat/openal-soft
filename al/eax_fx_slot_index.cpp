#include "config.h"

#include "eax_fx_slot_index.h"

#include "eax_exception.h"


namespace
{


class EaxFxSlotIndexException :
    public EaxException
{
public:
    explicit EaxFxSlotIndexException(
        const char* message)
        :
        EaxException{"EAX_FX_SLOT_INDEX", message}
    {
    }
}; // EaxFxSlotIndexException


} // namespace


EaxFxSlotIndex::EaxFxSlotIndex(
    EaxFxSlotIndexValue index)
{
    set(index);
}

EaxFxSlotIndex::EaxFxSlotIndex(
    const EaxFxSlotIndex& rhs) noexcept
    :
    has_value_{rhs.has_value_},
    value_{rhs.value_}
{
}

void EaxFxSlotIndex::operator=(
    EaxFxSlotIndexValue index)
{
    set(index);
}

void EaxFxSlotIndex::operator=(
    const GUID& guid)
{
    set(guid);
}

void EaxFxSlotIndex::operator=(
    const EaxFxSlotIndex& rhs) noexcept
{
    has_value_ = rhs.has_value_;
    value_ = rhs.value_;
}

bool EaxFxSlotIndex::has_value() const noexcept
{
    return has_value_;
}

EaxFxSlotIndexValue EaxFxSlotIndex::get() const
{
    if (!has_value_)
    {
        throw EaxFxSlotIndexException{"No value."};
    }

    return value_;
}

void EaxFxSlotIndex::reset() noexcept
{
    has_value_ = false;
}

void EaxFxSlotIndex::set(
    EaxFxSlotIndexValue index)
{
    if (index >= static_cast<EaxFxSlotIndexValue>(EAX_MAX_FXSLOTS))
    {
        fail("Index out of range.");
    }

    has_value_ = true;
    value_ = index;
}

void EaxFxSlotIndex::set(
    const GUID& guid)
{
    if (false)
    {
    }
    else if (guid == EAX_NULL_GUID)
    {
        has_value_ = false;
    }
    else if (guid == EAXPROPERTYID_EAX40_FXSlot0 || guid == EAXPROPERTYID_EAX50_FXSlot0)
    {
        has_value_ = true;
        value_ = 0;
    }
    else if (guid == EAXPROPERTYID_EAX40_FXSlot1 || guid == EAXPROPERTYID_EAX50_FXSlot1)
    {
        has_value_ = true;
        value_ = 1;
    }
    else if (guid == EAXPROPERTYID_EAX40_FXSlot2 || guid == EAXPROPERTYID_EAX50_FXSlot2)
    {
        has_value_ = true;
        value_ = 2;
    }
    else if (guid == EAXPROPERTYID_EAX40_FXSlot3 || guid == EAXPROPERTYID_EAX50_FXSlot3)
    {
        has_value_ = true;
        value_ = 3;
    }
    else
    {
        fail("Unsupported GUID.");
    }
}

EaxFxSlotIndex::operator EaxFxSlotIndexValue() const
{
    return get();
}

[[noreturn]]
void EaxFxSlotIndex::fail(
    const char* message)
{
    throw EaxFxSlotIndexException{message};
}


bool operator==(
    const EaxFxSlotIndex& lhs,
    const EaxFxSlotIndex& rhs) noexcept
{
    if (lhs.has_value() != rhs.has_value())
    {
        return false;
    }

    if (lhs.has_value())
    {
        return lhs.get() == rhs.get();
    }
    else
    {
        return true;
    }
}

bool operator!=(
    const EaxFxSlotIndex& lhs,
    const EaxFxSlotIndex& rhs) noexcept
{
    return !(lhs == rhs);
}
