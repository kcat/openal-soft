#ifndef EAX_EAX_CALL_INCLUDED
#define EAX_EAX_CALL_INCLUDED

#include "AL/al.h"
#include "alnumeric.h"
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

    bool is_get() const noexcept { return mCallType == EaxCallType::get; }
    bool is_deferred() const noexcept { return mIsDeferred; }
    int get_version() const noexcept { return mVersion; }
    EaxCallPropertySetId get_property_set_id() const noexcept { return mPropertySetId; }
    ALuint get_property_id() const noexcept { return mPropertyId; }
    ALuint get_property_al_name() const noexcept { return mPropertySourceId; }
    EaxFxSlotIndex get_fx_slot_index() const noexcept { return mFxSlotIndex; }

    template<typename TException, typename TValue>
    TValue& get_value() const
    {
        if(mPropertyBufferSize < sizeof(TValue))
            fail_too_small();

        return *static_cast<TValue*>(mPropertyBuffer);
    }

    template<typename TValue>
    al::span<TValue> get_values(size_t max_count) const
    {
        if(max_count == 0 || mPropertyBufferSize < sizeof(TValue))
            fail_too_small();

        const auto count = minz(mPropertyBufferSize / sizeof(TValue), max_count);
        return al::as_span(static_cast<TValue*>(mPropertyBuffer), count);
    }

    template<typename TValue>
    al::span<TValue> get_values() const
    {
        return get_values<TValue>(~size_t{});
    }

    template<typename TException, typename TValue>
    void set_value(const TValue& value) const
    {
        get_value<TException, TValue>() = value;
    }

private:
    const EaxCallType mCallType;
    int mVersion;
    EaxFxSlotIndex mFxSlotIndex;
    EaxCallPropertySetId mPropertySetId;
    bool mIsDeferred;

    const ALuint mPropertyId;
    const ALuint mPropertySourceId;
    ALvoid*const mPropertyBuffer;
    const ALuint mPropertyBufferSize;

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
