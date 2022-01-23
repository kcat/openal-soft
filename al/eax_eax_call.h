#ifndef EAX_EAX_CALL_INCLUDED
#define EAX_EAX_CALL_INCLUDED


#include "AL/al.h"

#include "alspan.h"

#include "eax_api.h"
#include "eax_fx_slot_index.h"


enum class EaxEaxCallPropertySetId
{
    none,

    context,
    fx_slot,
    source,
    fx_slot_effect,
}; // EaxEaxCallPropertySetId


class EaxEaxCall
{
public:
    EaxEaxCall(
        bool is_get,
        const GUID* property_set_id,
        ALuint property_id,
        ALuint property_source_id,
        ALvoid* property_buffer,
        ALuint property_size);


    bool is_get() const noexcept;

    bool is_deferred() const noexcept;

    int get_version() const noexcept;

    EaxEaxCallPropertySetId get_property_set_id() const noexcept;

    ALuint get_property_id() const noexcept;

    ALuint get_property_al_name() const noexcept;

    EaxFxSlotIndex get_fx_slot_index() const noexcept;


    template<
        typename TException,
        typename TValue
    >
    TValue& get_value() const
    {
        if (property_size_ < static_cast<ALuint>(sizeof(TValue)))
        {
            throw TException{"Property buffer too small."};
        }

        return *static_cast<TValue*>(property_buffer_);
    }

    template<
        typename TException,
        typename TValue
    >
    al::span<TValue> get_values() const
    {
        if (property_size_ < static_cast<ALuint>(sizeof(TValue)))
        {
            throw TException{"Property buffer too small."};
        }

        const auto count = property_size_ / sizeof(TValue);

        return al::span<TValue>{static_cast<TValue*>(property_buffer_), count};
    }

    template<
        typename TException,
        typename TValue
    >
    void set_value(
        const TValue& value) const
    {
        get_value<TException, TValue>() = value;
    }


private:
    bool is_get_;
    bool is_deferred_;
    int version_;
    EaxFxSlotIndex fx_slot_index_;
    EaxEaxCallPropertySetId property_set_id_;

    GUID property_set_guid_;
    ALuint property_id_;
    ALuint property_source_id_;
    ALvoid* property_buffer_;
    ALuint property_size_;


    [[noreturn]]
    static void fail(
        const char* message);


    static ALuint convert_eax_v2_0_listener_property_id(
        ALuint property_id);

    static ALuint convert_eax_v2_0_buffer_property_id(
        ALuint property_id);
}; // EaxEaxCall


EaxEaxCall create_eax_call(
    bool is_get,
    const GUID* property_set_id,
    ALuint property_id,
    ALuint property_source_id,
    ALvoid* property_buffer,
    ALuint property_size);


#endif // !EAX_EAX_CALL_INCLUDED
