#ifndef EAX_EAX_CALL_INCLUDED
#define EAX_EAX_CALL_INCLUDED

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "AL/al.h"
#include "fx_slot_index.h"

struct AL_GUID;


enum class EaxCallType : bool { get, set };

enum class EaxCallPropertySetId : std::uint8_t {
    context,
    fx_slot,
    source,
    fx_slot_effect,
};

class EaxCall {
    EaxCallType const mCallType;
    std::uint8_t mVersion{};
    EaxFxSlotIndex mFxSlotIndex{};
    EaxCallPropertySetId mPropertySetId{};
    bool mIsDeferred{};

    ALuint const mPropertyId;
    ALuint const mPropertySourceId;
    std::span<std::byte> const mPropertyBuffer;

    [[noreturn]] static void fail(std::string_view message);
    [[noreturn]] static void fail_too_small();

public:
    EaxCall(EaxCallType type, AL_GUID const& property_set_guid, ALuint property_id,
        ALuint property_source_id, ALvoid* property_buffer, ALuint property_size);

    [[nodiscard]] auto is_get() const noexcept -> bool { return mCallType == EaxCallType::get; }
    [[nodiscard]] auto is_deferred() const noexcept -> bool { return mIsDeferred; }
    [[nodiscard]] auto get_version() const noexcept -> int { return mVersion; }
    [[nodiscard]] auto get_property_set_id() const noexcept -> EaxCallPropertySetId { return mPropertySetId; }
    [[nodiscard]] auto get_property_id() const noexcept -> ALuint { return mPropertyId; }
    [[nodiscard]] auto get_property_al_name() const noexcept -> ALuint { return mPropertySourceId; }
    [[nodiscard]] auto get_fx_slot_index() const noexcept -> EaxFxSlotIndex { return mFxSlotIndex; }

    template<typename TValue> [[nodiscard]]
    auto as_span(std::size_t const max_count=std::numeric_limits<std::size_t>::max()) const
        -> std::span<TValue>
    {
        auto const count = std::min(mPropertyBuffer.size()/sizeof(TValue), max_count);
        if(count == 0)
            fail_too_small();

        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        return std::span{reinterpret_cast<TValue*>(mPropertyBuffer.data()), count};
    }

    template<typename TValue> [[nodiscard]]
    auto load() const -> TValue& { return as_span<TValue>(1)[0]; }

    template<typename TValue>
    auto store(const TValue &value) const -> void { load<TValue>() = value; }
};

#endif // !EAX_EAX_CALL_INCLUDED
