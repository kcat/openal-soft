#ifndef AL_SOURCE_H
#define AL_SOURCE_H

#include "config.h"

#include <array>
#include <bitset>
#include <concepts>
#include <deque>
#include <functional>
#include <limits>
#include <numbers>
#include <span>
#include <string_view>
#include <utility>

#include "AL/al.h"
#include "AL/alext.h"

#include "almalloc.h"
#include "altypes.hpp"
#include "core/context.h"
#include "core/voice.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"

#if ALSOFT_EAX
#include "eax/api.h"
#include "eax/call.h"
#include "eax/fx_slot_index.h"
#endif // ALSOFT_EAX

namespace al {
struct Context;
struct Buffer;
struct EffectSlot;
} // namespace al
enum class Resampler : u8::value_t;

enum class SourceStereo : bool {
    Normal = AL_NORMAL_SOFT,
    Enhanced = AL_SUPER_STEREO_SOFT
};

inline constexpr auto DefaultSendCount = 2_uz;

inline constexpr auto InvalidVoiceIndex = std::numeric_limits<unsigned>::max();

inline constinit auto sBufferSubDataCompat = false;

#if ALSOFT_EAX
struct EaxAlLowPassParam {
    float gain;
    float gain_hf;
};
#endif // ALSOFT_EAX

namespace al {

struct BufferQueueItem : VoiceBufferItem {
    al::intrusive_ptr<Buffer> mBuffer;
};

struct Source {
    float mPitch{1.0f};
    float mGain{1.0f};
    float mOuterGain{0.0f};
    float mMinGain{0.0f};
    float mMaxGain{1.0f};
    float mInnerAngle{360.0f};
    float mOuterAngle{360.0f};
    float mRefDistance{1.0f};
    float mMaxDistance{std::numeric_limits<float>::max()};
    float mRolloffFactor{1.0f};
#if ALSOFT_EAX
    // For EAXSOURCE_ROLLOFFFACTOR, which is distinct from and added to
    // AL_ROLLOFF_FACTOR
    float mRolloffFactor2{0.0f};
#endif
    std::array<float, 3> mPosition{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> mVelocity{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> mDirection{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> mOrientAt{{0.0f, 0.0f, -1.0f}};
    std::array<float, 3> mOrientUp{{0.0f, 1.0f,  0.0f}};
    bool mHeadRelative{false};
    bool mLooping{false};
    DistanceModel mDistanceModel{DistanceModel::Default};
    Resampler mResampler{ResamplerDefault};
    DirectMode DirectChannels{DirectMode::Off};
    SpatializeMode mSpatialize{SpatializeMode::Auto};
    SourceStereo mStereoMode{SourceStereo::Normal};
    bool mPanningEnabled{false};

    bool mDryGainHFAuto{true};
    bool mWetGainAuto{true};
    bool mWetGainHFAuto{true};
    float mOuterGainHF{1.0f};

    float mAirAbsorptionFactor{0.0f};
    float mRoomRolloffFactor{0.0f};
    float mDopplerFactor{1.0f};

    /* NOTE: Stereo pan angles are specified in radians, counter-clockwise
     * rather than clockwise.
     */
    std::array<float, 2> mStereoPan{{std::numbers::pi_v<float>/6.0f,
        -std::numbers::pi_v<float>/6.0f}};

    float mRadius{0.0f};
    float mEnhWidth{0.46f};
    float mPan{0.0f};

    /** Direct filter and auxiliary send info. */
    struct DirectData {
        float mGain{};
        float mGainHF{};
        float mHFReference{};
        float mGainLF{};
        float mLFReference{};
    };
    DirectData mDirect;

    struct SendData {
        intrusive_ptr<EffectSlot> mSlot;
        float mGain{};
        float mGainHF{};
        float mHFReference{};
        float mGainLF{};
        float mLFReference{};
    };
    std::array<SendData, MaxSendCount> mSend;

    /**
     * Last user-specified offset, and the offset type (bytes, samples, or
     * seconds).
     */
    double mOffset{0.0};
    ALenum mOffsetType{AL_NONE};

    /** Source type (static, streaming, or undetermined) */
    ALenum mSourceType{AL_UNDETERMINED};

    /** Source state (initial, playing, paused, or stopped) */
    ALenum mState{AL_INITIAL};

    /** Source Buffer Queue head. */
    std::deque<BufferQueueItem> mQueue;

    bool mPropsDirty{true};

    /* Index into the context's Voices array. Lazily updated, only checked and
     * reset when looking up the voice.
     */
    unsigned mVoiceIdx{InvalidVoiceIndex};

    /** Self ID */
    ALuint mId{0};


    Source() noexcept;
    ~Source();

    Source(const Source&) = delete;
    auto operator=(const Source&) -> Source& = delete;

    static void SetName(gsl::not_null<Context*> context, ALuint id, std::string_view name);

    DISABLE_ALLOC

#if ALSOFT_EAX
public:
    void eaxInitialize(gsl::not_null<Context*> context) noexcept;
    void eaxDispatch(const EaxCall& call) { call.is_get() ? eax_get(call) : eax_set(call); }
    void eaxCommit();
    void eaxMarkAsChanged() noexcept { mEaxChanged = true; }

    static auto EaxLookupSource(gsl::not_null<Context*> al_context LIFETIMEBOUND, ALuint source_id)
        noexcept -> Source*;

private:
    static constexpr auto eax_max_speakers = 9u;

    using EaxFxSlotIds = std::array<AL_GUID const*, EAX_MAX_FXSLOTS>;

    static constexpr auto eax4_fx_slot_ids = EaxFxSlotIds{
        &EAXPROPERTYID_EAX40_FXSlot0,
        &EAXPROPERTYID_EAX40_FXSlot1,
        &EAXPROPERTYID_EAX40_FXSlot2,
        &EAXPROPERTYID_EAX40_FXSlot3,
    };

    static constexpr auto eax5_fx_slot_ids = EaxFxSlotIds{
        &EAXPROPERTYID_EAX50_FXSlot0,
        &EAXPROPERTYID_EAX50_FXSlot1,
        &EAXPROPERTYID_EAX50_FXSlot2,
        &EAXPROPERTYID_EAX50_FXSlot3,
    };

    using EaxActiveFxSlots = std::bitset<EAX_MAX_FXSLOTS>;
    using EaxSpeakerLevels = std::array<EAXSPEAKERLEVELPROPERTIES, eax_max_speakers>;
    using EaxSends = std::array<EAXSOURCEALLSENDPROPERTIES, EAX_MAX_FXSLOTS>;

    struct Eax1State {
        EAXBUFFER_REVERBPROPERTIES i; // Immediate.
        EAXBUFFER_REVERBPROPERTIES d; // Deferred.
    };

    struct Eax2State {
        EAX20BUFFERPROPERTIES i; // Immediate.
        EAX20BUFFERPROPERTIES d; // Deferred.
    };

    struct Eax3State {
        EAX30SOURCEPROPERTIES i; // Immediate.
        EAX30SOURCEPROPERTIES d; // Deferred.
    };

    struct Eax4Props {
        EAX30SOURCEPROPERTIES source;
        EaxSends sends;
        EAX40ACTIVEFXSLOTS active_fx_slots;
    };

    struct Eax4State {
        Eax4Props i; // Immediate.
        Eax4Props d; // Deferred.
    };

    struct Eax5Props {
        EAX50SOURCEPROPERTIES source;
        EaxSends sends;
        EAX50ACTIVEFXSLOTS active_fx_slots;
        EaxSpeakerLevels speaker_levels;
    };

    struct Eax5State {
        Eax5Props i; // Immediate.
        Eax5Props d; // Deferred.
    };

    Context *mEaxAlContext{};
    EaxFxSlotIndex mEaxPrimaryFxSlotId{};
    EaxActiveFxSlots mEaxActiveFxSlots;
    int mEaxVersion{};
    bool mEaxChanged{};
    Eax1State mEax1{};
    Eax2State mEax2{};
    Eax3State mEax3{};
    Eax4State mEax4{};
    Eax5State mEax5{};
    Eax5Props mEax{};

    static void eax_set_sends_defaults(EaxSends& sends, const EaxFxSlotIds& ids) noexcept;
    static void eax1_set_defaults(EAXBUFFER_REVERBPROPERTIES& props) noexcept;
    void eax1_set_defaults() noexcept;
    static void eax2_set_defaults(EAX20BUFFERPROPERTIES& props) noexcept;
    void eax2_set_defaults() noexcept;
    static void eax3_set_defaults(EAX30SOURCEPROPERTIES& props) noexcept;
    void eax3_set_defaults() noexcept;
    static void eax4_set_sends_defaults(EaxSends& sends) noexcept;
    static void eax4_set_active_fx_slots_defaults(EAX40ACTIVEFXSLOTS& slots) noexcept;
    void eax4_set_defaults() noexcept;
    static void eax5_set_source_defaults(EAX50SOURCEPROPERTIES& props) noexcept;
    static void eax5_set_sends_defaults(EaxSends& sends) noexcept;
    static void eax5_set_active_fx_slots_defaults(EAX50ACTIVEFXSLOTS& slots) noexcept;
    static void eax5_set_speaker_levels_defaults(EaxSpeakerLevels& speaker_levels) noexcept;
    static void eax5_set_defaults(Eax5Props& props) noexcept;
    void eax5_set_defaults() noexcept;
    void eax_set_defaults() noexcept;

    static void eax1_translate(const EAXBUFFER_REVERBPROPERTIES& src, Eax5Props& dst) noexcept;
    static void eax2_translate(const EAX20BUFFERPROPERTIES& src, Eax5Props& dst) noexcept;
    static void eax3_translate(const EAX30SOURCEPROPERTIES& src, Eax5Props& dst) noexcept;
    static void eax4_translate(const Eax4Props& src, Eax5Props& dst) noexcept;

    static auto eax_calculate_dst_occlusion_mb(eax_long src_occlusion_mb, float path_ratio,
        float lf_ratio) noexcept -> float;

    [[nodiscard]] auto eax_create_direct_filter_param() const noexcept -> EaxAlLowPassParam;

    [[nodiscard]] auto eax_create_room_filter_param(al::EffectSlot const& fx_slot,
        const EAXSOURCEALLSENDPROPERTIES& send) const noexcept -> EaxAlLowPassParam;

    void eax_update_direct_filter();
    void eax_update_room_filters();
    void eax_commit_filters();

    static void eax_copy_send_for_get(const EAXSOURCEALLSENDPROPERTIES& src,
        EAXSOURCESENDPROPERTIES& dst) noexcept
    {
        dst.guidReceivingFXSlotID = src.guidReceivingFXSlotID;
        dst.mSend = src.mSend;
    }

    static void eax_copy_send_for_get(const EAXSOURCEALLSENDPROPERTIES& src,
        EAXSOURCEALLSENDPROPERTIES& dst) noexcept
    {
        dst = src;
    }

    static void eax_copy_send_for_get(const EAXSOURCEALLSENDPROPERTIES& src,
        EAXSOURCEOCCLUSIONSENDPROPERTIES& dst) noexcept
    {
        dst.guidReceivingFXSlotID = src.guidReceivingFXSlotID;
        dst.mOcclusion = src.mOcclusion;
    }

    static void eax_copy_send_for_get(const EAXSOURCEALLSENDPROPERTIES& src,
        EAXSOURCEEXCLUSIONSENDPROPERTIES& dst) noexcept
    {
        dst.guidReceivingFXSlotID = src.guidReceivingFXSlotID;
        dst.mExclusion = src.mExclusion;
    }

    template<typename TDstSend>
    static void eax_get_sends(const EaxCall &call, const EaxSends &src_sends)
    {
        const auto dst_sends = call.as_span<TDstSend>(EAX_MAX_FXSLOTS);
        for(const auto i : std::views::iota(0_uz, dst_sends.size()))
        {
            const auto &src_send = src_sends[i];
            auto &dst_send = dst_sends[i];
            eax_copy_send_for_get(src_send, dst_send);
        }
    }

    static void eax_get_active_fx_slot_id(const EaxCall &call, std::span<AL_GUID const> srcids);
    static void eax1_get(const EaxCall &call, const EAXBUFFER_REVERBPROPERTIES &props);
    static void eax2_get(const EaxCall &call, const EAX20BUFFERPROPERTIES &props);
    static void eax3_get(const EaxCall &call, const EAX30SOURCEPROPERTIES &props);
    static void eax4_get(const EaxCall &call, const Eax4Props &props);
    static void eax5_get_all_2d(const EaxCall &call, const EAX50SOURCEPROPERTIES &props);
    static void eax5_get_speaker_levels(const EaxCall &call, const EaxSpeakerLevels &props);
    static void eax5_get(const EaxCall &call, const Eax5Props &props);
    void eax_get(const EaxCall &call) const;

    static void eax_copy_send_for_set(const EAXSOURCEALLSENDPROPERTIES &src,
        EAXSOURCEALLSENDPROPERTIES &dst) noexcept
    {
        dst.mSend = src.mSend;
        dst.mOcclusion = src.mOcclusion;
        dst.mExclusion = src.mExclusion;
    }

    static void eax_copy_send_for_set(const EAXSOURCESENDPROPERTIES &src,
        EAXSOURCEALLSENDPROPERTIES &dst) noexcept
    {
        dst.mSend = src.mSend;
    }

    static void eax_copy_send_for_set(const EAXSOURCEOCCLUSIONSENDPROPERTIES &src,
        EAXSOURCEALLSENDPROPERTIES &dst) noexcept
    {
        dst.mOcclusion = src.mOcclusion;
    }

    static void eax_copy_send_for_set(const EAXSOURCEEXCLUSIONSENDPROPERTIES &src,
        EAXSOURCEALLSENDPROPERTIES &dst) noexcept
    {
        dst.mExclusion = src.mExclusion;
    }

    template<std::invocable<AL_GUID const&> TIndexGetter, typename TSrcSend>
    static void eax_defer_sends(const EaxCall &call, EaxSends &dst_sends,
        std::invocable<TSrcSend> auto&& validator)
    {
        const auto src_sends = call.as_span<const TSrcSend>(EAX_MAX_FXSLOTS);
        std::ranges::for_each(src_sends, std::forward<decltype(validator)>(validator));

        std::ranges::for_each(src_sends, [&dst_sends](const TSrcSend &src_send)
        {
            const auto dst_index = std::invoke(TIndexGetter{}, src_send.guidReceivingFXSlotID);
            eax_copy_send_for_set(src_send, dst_sends[dst_index]);
        });
    }

    template<typename TSrcSend> static
    auto eax4_defer_sends(const EaxCall &call, EaxSends &dst_sends,
        std::invocable<TSrcSend> auto validator) -> void;
    template<typename TSrcSend> static
    auto eax5_defer_sends(const EaxCall &call, EaxSends &dst_sends,
        std::invocable<TSrcSend> auto validator) -> void;

    template<std::invocable<AL_GUID const&> TValidator>
    static void eax_defer_active_fx_slot_id(const EaxCall &call, const std::span<AL_GUID> dst_ids)
    {
        const auto src_ids = call.as_span<AL_GUID const>(dst_ids.size());
        std::ranges::for_each(src_ids, TValidator{});
        std::ranges::uninitialized_copy(src_ids, dst_ids);
    }

    static void eax4_defer_active_fx_slot_id(EaxCall const& call, std::span<AL_GUID> dst_ids);
    static void eax5_defer_active_fx_slot_id(EaxCall const& call, std::span<AL_GUID> dst_ids);

    template<typename TProperty>
    static void eax_defer(const EaxCall &call, TProperty &property,
        std::invocable<TProperty> auto&& validator)
    {
        const auto& value = call.load<const TProperty>();
        std::forward<decltype(validator)>(validator)(value);
        property = value;
    }

    void eax_set_efx_outer_gain_hf();
    void eax_set_efx_doppler_factor();
    void eax_set_efx_rolloff_factor();
    void eax_set_efx_room_rolloff_factor();
    void eax_set_efx_air_absorption_factor();
    void eax_set_efx_dry_gain_hf_auto();
    void eax_set_efx_wet_gain_auto();
    void eax_set_efx_wet_gain_hf_auto();

    static void eax1_set(const EaxCall& call, EAXBUFFER_REVERBPROPERTIES& props);
    static void eax2_set(const EaxCall& call, EAX20BUFFERPROPERTIES& props);
    static void eax3_set(const EaxCall& call, EAX30SOURCEPROPERTIES& props);
    static void eax4_set(const EaxCall& call, Eax4Props& props);
    static void eax5_defer_all_2d(const EaxCall& call, EAX50SOURCEPROPERTIES& props);
    static void eax5_defer_speaker_levels(const EaxCall& call, EaxSpeakerLevels& props);
    static void eax5_set(const EaxCall& call, Eax5Props& props);
    void eax_set(const EaxCall& call);

    // `alSource3i(source, AL_AUXILIARY_SEND_FILTER, ...)`
    void eax_set_al_source_send(intrusive_ptr<EffectSlot> slot, std::size_t sendidx,
        EaxAlLowPassParam const &filter);

    void eax_commit_active_fx_slots();
#endif // ALSOFT_EAX
};

} /* namespace al */

void UpdateAllSourceProps(gsl::not_null<al::Context*> context);

struct SourceSubList {
    u64 mFreeMask{~0_u64};
    gsl::owner<std::array<al::Source,64>*> mSources{nullptr};

    SourceSubList() noexcept = default;
    SourceSubList(const SourceSubList&) = delete;
    SourceSubList(SourceSubList&& rhs) noexcept : mFreeMask{rhs.mFreeMask}, mSources{rhs.mSources}
    { rhs.mFreeMask = ~0_u64; rhs.mSources = nullptr; }
    ~SourceSubList();

    SourceSubList& operator=(const SourceSubList&) = delete;
    SourceSubList& operator=(SourceSubList&& rhs) & noexcept
    { std::swap(mFreeMask, rhs.mFreeMask); std::swap(mSources, rhs.mSources); return *this; }
};

#endif
