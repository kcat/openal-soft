#include "config.h"
#include "call.h"
#include "exception.h"

namespace {

constexpr auto deferred_flag = 0x80000000U;

class EaxCallException : public EaxException {
public:
    explicit EaxCallException(const char* message)
        : EaxException{"EAX_CALL", message}
    {}
}; // EaxCallException

} // namespace

EaxCall::EaxCall(
    EaxCallType type,
    const GUID& property_set_guid,
    ALuint property_id,
    ALuint property_source_id,
    ALvoid* property_buffer,
    ALuint property_size)
    : type_{type}, version_{0}, property_set_id_{EaxCallPropertySetId::none}
    , property_id_{property_id & ~deferred_flag}, property_source_id_{property_source_id}
    , property_buffer_{property_buffer}, property_size_{property_size}
{
    switch (type_)
    {
        case EaxCallType::get:
        case EaxCallType::set:
            break;

        default:
            fail("Invalid type.");
    }

    if (false)
    {
    }
    else if (property_set_guid == EAXPROPERTYID_EAX40_Context)
    {
        version_ = 4;
        property_set_id_ = EaxCallPropertySetId::context;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX50_Context)
    {
        version_ = 5;
        property_set_id_ = EaxCallPropertySetId::context;
    }
    else if (property_set_guid == DSPROPSETID_EAX20_ListenerProperties)
    {
        version_ = 2;
        fx_slot_index_ = 0u;
        property_set_id_ = EaxCallPropertySetId::fx_slot_effect;
    }
    else if (property_set_guid == DSPROPSETID_EAX30_ListenerProperties)
    {
        version_ = 3;
        fx_slot_index_ = 0u;
        property_set_id_ = EaxCallPropertySetId::fx_slot_effect;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX40_FXSlot0)
    {
        version_ = 4;
        fx_slot_index_ = 0u;
        property_set_id_ = EaxCallPropertySetId::fx_slot;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX50_FXSlot0)
    {
        version_ = 5;
        fx_slot_index_ = 0u;
        property_set_id_ = EaxCallPropertySetId::fx_slot;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX40_FXSlot1)
    {
        version_ = 4;
        fx_slot_index_ = 1u;
        property_set_id_ = EaxCallPropertySetId::fx_slot;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX50_FXSlot1)
    {
        version_ = 5;
        fx_slot_index_ = 1u;
        property_set_id_ = EaxCallPropertySetId::fx_slot;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX40_FXSlot2)
    {
        version_ = 4;
        fx_slot_index_ = 2u;
        property_set_id_ = EaxCallPropertySetId::fx_slot;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX50_FXSlot2)
    {
        version_ = 5;
        fx_slot_index_ = 2u;
        property_set_id_ = EaxCallPropertySetId::fx_slot;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX40_FXSlot3)
    {
        version_ = 4;
        fx_slot_index_ = 3u;
        property_set_id_ = EaxCallPropertySetId::fx_slot;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX50_FXSlot3)
    {
        version_ = 5;
        fx_slot_index_ = 3u;
        property_set_id_ = EaxCallPropertySetId::fx_slot;
    }
    else if (property_set_guid == DSPROPSETID_EAX20_BufferProperties)
    {
        version_ = 2;
        property_set_id_ = EaxCallPropertySetId::source;
    }
    else if (property_set_guid == DSPROPSETID_EAX30_BufferProperties)
    {
        version_ = 3;
        property_set_id_ = EaxCallPropertySetId::source;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX40_Source)
    {
        version_ = 4;
        property_set_id_ = EaxCallPropertySetId::source;
    }
    else if (property_set_guid == EAXPROPERTYID_EAX50_Source)
    {
        version_ = 5;
        property_set_id_ = EaxCallPropertySetId::source;
    }
    else if (property_set_guid == DSPROPSETID_EAX_ReverbProperties)
    {
        version_ = 1;
        fx_slot_index_ = 0u;
        property_set_id_ = EaxCallPropertySetId::fx_slot_effect;
    }
    else if (property_set_guid == DSPROPSETID_EAXBUFFER_ReverbProperties)
    {
        version_ = 1;
        property_set_id_ = EaxCallPropertySetId::source;
    }
    else
    {
        fail("Unsupported property set id.");
    }

    if (version_ < 1 || version_ > 5)
    {
        fail("EAX version out of range.");
    }

    if(!(property_id&deferred_flag))
    {
        if(property_set_id_ != EaxCallPropertySetId::fx_slot && property_id_ != 0)
        {
            if (property_buffer == nullptr)
            {
                fail("Null property buffer.");
            }

            if (property_size == 0)
            {
                fail("Empty property.");
            }
        }
    }

    if(property_set_id_ == EaxCallPropertySetId::source && property_source_id_ == 0)
    {
        fail("Null AL source id.");
    }

    if (property_set_id_ == EaxCallPropertySetId::fx_slot)
    {
        if (property_id_ < EAXFXSLOT_NONE)
        {
            property_set_id_ = EaxCallPropertySetId::fx_slot_effect;
        }
    }
}

[[noreturn]] void EaxCall::fail(const char* message)
{
    throw EaxCallException{message};
}

[[noreturn]] void EaxCall::fail_too_small()
{
    fail("Property buffer too small.");
}

EaxCall create_eax_call(
    EaxCallType type,
    const GUID* property_set_id,
    ALuint property_id,
    ALuint property_source_id,
    ALvoid* property_buffer,
    ALuint property_size)
{
    if(!property_set_id)
        throw EaxCallException{"Null property set ID."};

    return EaxCall{
        type,
        *property_set_id,
        property_id,
        property_source_id,
        property_buffer,
        property_size
    };
}
