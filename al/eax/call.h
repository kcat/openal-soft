#ifndef EAX_EAX_CALL_INCLUDED
#define EAX_EAX_CALL_INCLUDED

#include "AL/al.h"
#include "alspan.h"
#include "api.h"
#include "fx_slot_index.h"

enum class EaxCallType {
    none,
    get,
    set,
}; // EaxCallType

enum class EaxCallPropertySetId {
    none,
    context,
    fx_slot,
    source,
    fx_slot_effect,
}; // EaxCallPropertySetId

class EaxCall {
public:
    EaxCall(
        EaxCallType type,
        const GUID& property_set_guid,
        ALuint property_id,
        ALuint property_source_id,
        ALvoid* property_buffer,
        ALuint property_size);

    bool is_get() const noexcept { return type_ == EaxCallType::get; }
    int get_version() const noexcept { return version_; }
    EaxCallPropertySetId get_property_set_id() const noexcept { return property_set_id_; }
    ALuint get_property_id() const noexcept { return property_id_; }
    ALuint get_property_al_name() const noexcept { return property_source_id_; }
    EaxFxSlotIndex get_fx_slot_index() const noexcept { return fx_slot_index_; }

    template<typename TException, typename TValue>
    TValue& get_value() const
    {
        if (property_size_ < static_cast<ALuint>(sizeof(TValue)))
        {
            fail_too_small();
        }

        return *static_cast<TValue*>(property_buffer_);
    }

    template<typename TException, typename TValue>
    al::span<TValue> get_values() const
    {
        if (property_size_ < static_cast<ALuint>(sizeof(TValue)))
        {
            fail_too_small();
        }

        const auto count = property_size_ / sizeof(TValue);
        return al::span<TValue>{static_cast<TValue*>(property_buffer_), count};
    }

    template<typename TException, typename TValue>
    void set_value(const TValue& value) const
    {
        get_value<TException, TValue>() = value;
    }

private:
    EaxCallType type_;
    int version_;
    EaxFxSlotIndex fx_slot_index_;
    EaxCallPropertySetId property_set_id_;

    ALuint property_id_;
    ALuint property_source_id_;
    ALvoid*property_buffer_;
    ALuint property_size_;

    [[noreturn]] static void fail(const char* message);
    [[noreturn]] static void fail_too_small();
}; // EaxCall

EaxCall create_eax_call(
    EaxCallType type,
    const GUID* property_set_id,
    ALuint property_id,
    ALuint property_source_id,
    ALvoid* property_buffer,
    ALuint property_size);

#endif // !EAX_EAX_CALL_INCLUDED
