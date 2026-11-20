#include "config.h"

#include "fx_slot_index.h"

#include "api.h"

#if HAVE_CXXMODULES
import eax.exception;
#else
#include "al/eax/exception.hpp"
#endif

namespace {

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
class EaxFxSlotIndexException final : public EaxException {
public:
    explicit EaxFxSlotIndexException(std::string_view const message)
        : EaxException{"EAX_FX_SLOT_INDEX", message}
    { }
};

} // namespace

[[noreturn]]
void EaxFxSlotIndex::fail(std::string_view const message)
{
    throw EaxFxSlotIndexException{message};
}

void EaxFxSlotIndex::set(EaxFxSlotIndexValue index)
{
    if(index >= EaxFxSlotIndexValue{EAX_MAX_FXSLOTS})
        fail("Index out of range.");

    emplace(index);
}

void EaxFxSlotIndex::set(AL_GUID const& guid)
{
    if(guid == EAX_NULL_GUID)
        reset();
    else if(guid == EAXPROPERTYID_EAX40_FXSlot0 || guid == EAXPROPERTYID_EAX50_FXSlot0)
        emplace(EaxFxSlotIndexValue{0});
    else if(guid == EAXPROPERTYID_EAX40_FXSlot1 || guid == EAXPROPERTYID_EAX50_FXSlot1)
        emplace(EaxFxSlotIndexValue{1});
    else if(guid == EAXPROPERTYID_EAX40_FXSlot2 || guid == EAXPROPERTYID_EAX50_FXSlot2)
        emplace(EaxFxSlotIndexValue{2});
    else if(guid == EAXPROPERTYID_EAX40_FXSlot3 || guid == EAXPROPERTYID_EAX50_FXSlot3)
        emplace(EaxFxSlotIndexValue{3});
    else
        fail("Unsupported GUID.");
}
