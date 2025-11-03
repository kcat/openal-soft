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
#include "alnumeric.h"
#include "core/context.h"
#include "core/voice.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"

#if ALSOFT_EAX
#include "eax/api.h"
#include "eax/call.h"
#include "eax/exception.h"
#include "eax/fx_slot_index.h"
#include "eax/utils.h"
#endif // ALSOFT_EAX

namespace al {
struct Context;
struct Buffer;
struct EffectSlot;
} // namespace al
enum class Resampler : u8;

enum class SourceStereo : bool {
    Normal = AL_NORMAL_SOFT,
    Enhanced = AL_SUPER_STEREO_SOFT
};

inline constexpr auto DefaultSendCount = 2_uz;

inline constexpr auto InvalidVoiceIndex = std::numeric_limits<u32>::max();

inline constinit auto sBufferSubDataCompat = false;

struct ALbufferQueueItem : VoiceBufferItem {
    al::intrusive_ptr<al::Buffer> mBuffer;
};

#if ALSOFT_EAX
/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
class EaxSourceException final : public EaxException {
public:
    explicit EaxSourceException(const std::string_view message)
        : EaxException{"EAX_SOURCE", message}
    { }
};
#endif // ALSOFT_EAX

namespace al {

struct Source {
    f32 mPitch{1.0f};
    f32 mGain{1.0f};
    f32 mOuterGain{0.0f};
    f32 mMinGain{0.0f};
    f32 mMaxGain{1.0f};
    f32 mInnerAngle{360.0f};
    f32 mOuterAngle{360.0f};
    f32 mRefDistance{1.0f};
    f32 mMaxDistance{std::numeric_limits<f32>::max()};
    f32 mRolloffFactor{1.0f};
#if ALSOFT_EAX
    // For EAXSOURCE_ROLLOFFFACTOR, which is distinct from and added to
    // AL_ROLLOFF_FACTOR
    f32 mRolloffFactor2{0.0f};
#endif
    std::array<f32, 3> mPosition{{0.0f, 0.0f, 0.0f}};
    std::array<f32, 3> mVelocity{{0.0f, 0.0f, 0.0f}};
    std::array<f32, 3> mDirection{{0.0f, 0.0f, 0.0f}};
    std::array<f32, 3> mOrientAt{{0.0f, 0.0f, -1.0f}};
    std::array<f32, 3> mOrientUp{{0.0f, 1.0f,  0.0f}};
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
    f32 mOuterGainHF{1.0f};

    f32 mAirAbsorptionFactor{0.0f};
    f32 mRoomRolloffFactor{0.0f};
    f32 mDopplerFactor{1.0f};

    /* NOTE: Stereo pan angles are specified in radians, counter-clockwise
     * rather than clockwise.
     */
    std::array<f32, 2> mStereoPan{{std::numbers::pi_v<f32>/6.0f,
        -std::numbers::pi_v<f32>/6.0f}};

    f32 mRadius{0.0f};
    f32 mEnhWidth{0.593f};
    f32 mPan{0.0f};

    /** Direct filter and auxiliary send info. */
    struct DirectData {
        f32 mGain{};
        f32 mGainHF{};
        f32 mHFReference{};
        f32 mGainLF{};
        f32 mLFReference{};
    };
    DirectData mDirect;

    struct SendData {
        intrusive_ptr<EffectSlot> mSlot;
        f32 mGain{};
        f32 mGainHF{};
        f32 mHFReference{};
        f32 mGainLF{};
        f32 mLFReference{};
    };
    std::array<SendData, MaxSendCount> mSend;

    /**
     * Last user-specified offset, and the offset type (bytes, samples, or
     * seconds).
     */
    f64 mOffset{0.0};
    ALenum mOffsetType{AL_NONE};

    /** Source type (static, streaming, or undetermined) */
    ALenum mSourceType{AL_UNDETERMINED};

    /** Source state (initial, playing, paused, or stopped) */
    ALenum mState{AL_INITIAL};

    /** Source Buffer Queue head. */
    std::deque<ALbufferQueueItem> mQueue;

    bool mPropsDirty{true};

    /* Index into the context's Voices array. Lazily updated, only checked and
     * reset when looking up the voice.
     */
    u32 mVoiceIdx{InvalidVoiceIndex};

    /** Self ID */
    u32 mId{0};


    Source() noexcept;
    ~Source();

    Source(const Source&) = delete;
    auto operator=(const Source&) -> Source& = delete;

    static void SetName(gsl::not_null<Context*> context, u32 id, std::string_view name);

    DISABLE_ALLOC

#if ALSOFT_EAX
public:
    void eaxInitialize(gsl::not_null<Context*> context) noexcept;
    void eaxDispatch(const EaxCall& call) { call.is_get() ? eax_get(call) : eax_set(call); }
    void eaxCommit();
    void eaxMarkAsChanged() noexcept { mEaxChanged = true; }

    static auto EaxLookupSource(gsl::not_null<Context*> al_context LIFETIMEBOUND, u32 source_id)
        noexcept -> Source*;

private:
    using Exception = EaxSourceException;

    static constexpr auto eax_max_speakers = 9_u32;

    using EaxFxSlotIds = std::array<const GUID*, EAX_MAX_FXSLOTS>;

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

    // ----------------------------------------------------------------------
    // Source validators

    struct Eax1SourceReverbMixValidator {
        void operator()(f32 const reverb_mix) const
        {
            if (reverb_mix == EAX_REVERBMIX_USEDISTANCE)
                return;

            eax_validate_range<Exception>(
                "Reverb Mix",
                reverb_mix,
                EAX_BUFFER_MINREVERBMIX,
                EAX_BUFFER_MAXREVERBMIX);
        }
    };

    struct Eax2SourceDirectValidator {
        void operator()(eax_long const lDirect) const
        {
            eax_validate_range<Exception>(
                "Direct",
                lDirect,
                EAXSOURCE_MINDIRECT,
                EAXSOURCE_MAXDIRECT);
        }
    };

    struct Eax2SourceDirectHfValidator {
        void operator()(eax_long const lDirectHF) const
        {
            eax_validate_range<Exception>(
                "Direct HF",
                lDirectHF,
                EAXSOURCE_MINDIRECTHF,
                EAXSOURCE_MAXDIRECTHF);
        }
    };

    struct Eax2SourceRoomValidator {
        void operator()(eax_long const lRoom) const
        {
            eax_validate_range<Exception>(
                "Room",
                lRoom,
                EAXSOURCE_MINROOM,
                EAXSOURCE_MAXROOM);
        }
    };

    struct Eax2SourceRoomHfValidator {
        void operator()(eax_long const lRoomHF) const
        {
            eax_validate_range<Exception>(
                "Room HF",
                lRoomHF,
                EAXSOURCE_MINROOMHF,
                EAXSOURCE_MAXROOMHF);
        }
    };

    struct Eax2SourceRoomRolloffFactorValidator {
        void operator()(f32 const flRoomRolloffFactor) const
        {
            eax_validate_range<Exception>(
                "Room Rolloff Factor",
                flRoomRolloffFactor,
                EAXSOURCE_MINROOMROLLOFFFACTOR,
                EAXSOURCE_MAXROOMROLLOFFFACTOR);
        }
    };

    struct Eax2SourceObstructionValidator {
        void operator()(eax_long const lObstruction) const
        {
            eax_validate_range<Exception>(
                "Obstruction",
                lObstruction,
                EAXSOURCE_MINOBSTRUCTION,
                EAXSOURCE_MAXOBSTRUCTION);
        }
    };

    struct Eax2SourceObstructionLfRatioValidator {
        void operator()(f32 const flObstructionLFRatio) const
        {
            eax_validate_range<Exception>(
                "Obstruction LF Ratio",
                flObstructionLFRatio,
                EAXSOURCE_MINOBSTRUCTIONLFRATIO,
                EAXSOURCE_MAXOBSTRUCTIONLFRATIO);
        }
    };

    struct Eax2SourceOcclusionValidator {
        void operator()(eax_long const lOcclusion) const
        {
            eax_validate_range<Exception>(
                "Occlusion",
                lOcclusion,
                EAXSOURCE_MINOCCLUSION,
                EAXSOURCE_MAXOCCLUSION);
        }
    };

    struct Eax2SourceOcclusionLfRatioValidator {
        void operator()(f32 const flOcclusionLFRatio) const
        {
            eax_validate_range<Exception>(
                "Occlusion LF Ratio",
                flOcclusionLFRatio,
                EAXSOURCE_MINOCCLUSIONLFRATIO,
                EAXSOURCE_MAXOCCLUSIONLFRATIO);
        }
    };

    struct Eax2SourceOcclusionRoomRatioValidator {
        void operator()(f32 const flOcclusionRoomRatio) const
        {
            eax_validate_range<Exception>(
                "Occlusion Room Ratio",
                flOcclusionRoomRatio,
                EAXSOURCE_MINOCCLUSIONROOMRATIO,
                EAXSOURCE_MAXOCCLUSIONROOMRATIO);
        }
    };

    struct Eax2SourceOutsideVolumeHfValidator {
        void operator()(eax_long const lOutsideVolumeHF) const
        {
            eax_validate_range<Exception>(
                "Outside Volume HF",
                lOutsideVolumeHF,
                EAXSOURCE_MINOUTSIDEVOLUMEHF,
                EAXSOURCE_MAXOUTSIDEVOLUMEHF);
        }
    };

    struct Eax2SourceAirAbsorptionFactorValidator {
        void operator()(f32 const flAirAbsorptionFactor) const
        {
            eax_validate_range<Exception>(
                "Air Absorption Factor",
                flAirAbsorptionFactor,
                EAXSOURCE_MINAIRABSORPTIONFACTOR,
                EAXSOURCE_MAXAIRABSORPTIONFACTOR);
        }
    };

    struct Eax2SourceFlagsValidator {
        void operator()(eax_ulong const dwFlags) const
        {
            eax_validate_range<Exception>(
                "Flags",
                dwFlags,
                0_eax_ulong,
                ~EAX20SOURCEFLAGS_RESERVED);
        }
    };

    struct Eax3SourceOcclusionDirectRatioValidator {
        void operator()(f32 const flOcclusionDirectRatio) const
        {
            eax_validate_range<Exception>(
                "Occlusion Direct Ratio",
                flOcclusionDirectRatio,
                EAXSOURCE_MINOCCLUSIONDIRECTRATIO,
                EAXSOURCE_MAXOCCLUSIONDIRECTRATIO);
        }
    };

    struct Eax3SourceExclusionValidator {
        void operator()(eax_long const lExclusion) const
        {
            eax_validate_range<Exception>(
                "Exclusion",
                lExclusion,
                EAXSOURCE_MINEXCLUSION,
                EAXSOURCE_MAXEXCLUSION);
        }
    };

    struct Eax3SourceExclusionLfRatioValidator {
        void operator()(f32 const flExclusionLFRatio) const
        {
            eax_validate_range<Exception>(
                "Exclusion LF Ratio",
                flExclusionLFRatio,
                EAXSOURCE_MINEXCLUSIONLFRATIO,
                EAXSOURCE_MAXEXCLUSIONLFRATIO);
        }
    };

    struct Eax3SourceDopplerFactorValidator {
        void operator()(f32 const flDopplerFactor) const
        {
            eax_validate_range<Exception>(
                "Doppler Factor",
                flDopplerFactor,
                EAXSOURCE_MINDOPPLERFACTOR,
                EAXSOURCE_MAXDOPPLERFACTOR);
        }
    };

    struct Eax3SourceRolloffFactorValidator {
        void operator()(f32 const flRolloffFactor) const
        {
            eax_validate_range<Exception>(
                "Rolloff Factor",
                flRolloffFactor,
                EAXSOURCE_MINROLLOFFFACTOR,
                EAXSOURCE_MAXROLLOFFFACTOR);
        }
    };

    struct Eax5SourceMacroFXFactorValidator {
        void operator()(f32 const flMacroFXFactor) const
        {
            eax_validate_range<Exception>(
                "Macro FX Factor",
                flMacroFXFactor,
                EAXSOURCE_MINMACROFXFACTOR,
                EAXSOURCE_MAXMACROFXFACTOR);
        }
    };

    struct Eax5SourceFlagsValidator {
        void operator()(eax_ulong const dwFlags) const
        {
            eax_validate_range<Exception>(
                "Flags",
                dwFlags,
                0_eax_ulong,
                ~EAX50SOURCEFLAGS_RESERVED);
        }
    };

    struct Eax1SourceAllValidator {
        void operator()(const EAXBUFFER_REVERBPROPERTIES& props) const
        {
            Eax1SourceReverbMixValidator{}(props.fMix);
        }
    };

    struct Eax2SourceAllValidator {
        void operator()(const EAX20BUFFERPROPERTIES& props) const
        {
            Eax2SourceDirectValidator{}(props.lDirect);
            Eax2SourceDirectHfValidator{}(props.lDirectHF);
            Eax2SourceRoomValidator{}(props.lRoom);
            Eax2SourceRoomHfValidator{}(props.lRoomHF);
            Eax2SourceRoomRolloffFactorValidator{}(props.flRoomRolloffFactor);
            Eax2SourceObstructionValidator{}(props.lObstruction);
            Eax2SourceObstructionLfRatioValidator{}(props.flObstructionLFRatio);
            Eax2SourceOcclusionValidator{}(props.lOcclusion);
            Eax2SourceOcclusionLfRatioValidator{}(props.flOcclusionLFRatio);
            Eax2SourceOcclusionRoomRatioValidator{}(props.flOcclusionRoomRatio);
            Eax2SourceOutsideVolumeHfValidator{}(props.lOutsideVolumeHF);
            Eax2SourceAirAbsorptionFactorValidator{}(props.flAirAbsorptionFactor);
            Eax2SourceFlagsValidator{}(props.dwFlags);
        }
    };

    struct Eax3SourceAllValidator {
        void operator()(const EAX30SOURCEPROPERTIES& props) const
        {
            Eax2SourceDirectValidator{}(props.lDirect);
            Eax2SourceDirectHfValidator{}(props.lDirectHF);
            Eax2SourceRoomValidator{}(props.lRoom);
            Eax2SourceRoomHfValidator{}(props.lRoomHF);
            Eax2SourceObstructionValidator{}(props.mObstruction.lObstruction);
            Eax2SourceObstructionLfRatioValidator{}(props.mObstruction.flObstructionLFRatio);
            Eax2SourceOcclusionValidator{}(props.mOcclusion.lOcclusion);
            Eax2SourceOcclusionLfRatioValidator{}(props.mOcclusion.flOcclusionLFRatio);
            Eax2SourceOcclusionRoomRatioValidator{}(props.mOcclusion.flOcclusionRoomRatio);
            Eax3SourceOcclusionDirectRatioValidator{}(props.mOcclusion.flOcclusionDirectRatio);
            Eax3SourceExclusionValidator{}(props.mExclusion.lExclusion);
            Eax3SourceExclusionLfRatioValidator{}(props.mExclusion.flExclusionLFRatio);
            Eax2SourceOutsideVolumeHfValidator{}(props.lOutsideVolumeHF);
            Eax3SourceDopplerFactorValidator{}(props.flDopplerFactor);
            Eax3SourceRolloffFactorValidator{}(props.flRolloffFactor);
            Eax2SourceRoomRolloffFactorValidator{}(props.flRoomRolloffFactor);
            Eax2SourceAirAbsorptionFactorValidator{}(props.flAirAbsorptionFactor);
            Eax2SourceFlagsValidator{}(props.ulFlags);
        }
    };

    struct Eax5SourceAllValidator {
        void operator()(const EAX50SOURCEPROPERTIES& props) const
        {
            Eax2SourceDirectValidator{}(props.lDirect);
            Eax2SourceDirectHfValidator{}(props.lDirectHF);
            Eax2SourceRoomValidator{}(props.lRoom);
            Eax2SourceRoomHfValidator{}(props.lRoomHF);
            Eax2SourceObstructionValidator{}(props.mObstruction.lObstruction);
            Eax2SourceObstructionLfRatioValidator{}(props.mObstruction.flObstructionLFRatio);
            Eax2SourceOcclusionValidator{}(props.mOcclusion.lOcclusion);
            Eax2SourceOcclusionLfRatioValidator{}(props.mOcclusion.flOcclusionLFRatio);
            Eax2SourceOcclusionRoomRatioValidator{}(props.mOcclusion.flOcclusionRoomRatio);
            Eax3SourceOcclusionDirectRatioValidator{}(props.mOcclusion.flOcclusionDirectRatio);
            Eax3SourceExclusionValidator{}(props.mExclusion.lExclusion);
            Eax3SourceExclusionLfRatioValidator{}(props.mExclusion.flExclusionLFRatio);
            Eax2SourceOutsideVolumeHfValidator{}(props.lOutsideVolumeHF);
            Eax3SourceDopplerFactorValidator{}(props.flDopplerFactor);
            Eax3SourceRolloffFactorValidator{}(props.flRolloffFactor);
            Eax2SourceRoomRolloffFactorValidator{}(props.flRoomRolloffFactor);
            Eax2SourceAirAbsorptionFactorValidator{}(props.flAirAbsorptionFactor);
            Eax5SourceFlagsValidator{}(props.ulFlags);
            Eax5SourceMacroFXFactorValidator{}(props.flMacroFXFactor);
        }
    };

    struct Eax5SourceAll2dValidator {
        void operator()(const EAXSOURCE2DPROPERTIES& props) const
        {
            Eax2SourceDirectValidator{}(props.lDirect);
            Eax2SourceDirectHfValidator{}(props.lDirectHF);
            Eax2SourceRoomValidator{}(props.lRoom);
            Eax2SourceRoomHfValidator{}(props.lRoomHF);
            Eax5SourceFlagsValidator{}(props.ulFlags);
        }
    };

    struct Eax4ObstructionValidator {
        void operator()(const EAXOBSTRUCTIONPROPERTIES& props) const
        {
            Eax2SourceObstructionValidator{}(props.lObstruction);
            Eax2SourceObstructionLfRatioValidator{}(props.flObstructionLFRatio);
        }
    };

    struct Eax4OcclusionValidator {
        void operator()(const EAXOCCLUSIONPROPERTIES& props) const
        {
            Eax2SourceOcclusionValidator{}(props.lOcclusion);
            Eax2SourceOcclusionLfRatioValidator{}(props.flOcclusionLFRatio);
            Eax2SourceOcclusionRoomRatioValidator{}(props.flOcclusionRoomRatio);
            Eax3SourceOcclusionDirectRatioValidator{}(props.flOcclusionDirectRatio);
        }
    };

    struct Eax4ExclusionValidator {
        void operator()(const EAXEXCLUSIONPROPERTIES& props) const
        {
            Eax3SourceExclusionValidator{}(props.lExclusion);
            Eax3SourceExclusionLfRatioValidator{}(props.flExclusionLFRatio);
        }
    };

    // Source validators
    // ----------------------------------------------------------------------
    // Send validators

    struct Eax4SendReceivingFxSlotIdValidator {
        void operator()(const GUID& guidReceivingFXSlotID) const
        {
            if (guidReceivingFXSlotID != EAXPROPERTYID_EAX40_FXSlot0 &&
                guidReceivingFXSlotID != EAXPROPERTYID_EAX40_FXSlot1 &&
                guidReceivingFXSlotID != EAXPROPERTYID_EAX40_FXSlot2 &&
                guidReceivingFXSlotID != EAXPROPERTYID_EAX40_FXSlot3)
            {
                eax_fail_unknown_receiving_fx_slot_id();
            }
        }
    };

    struct Eax5SendReceivingFxSlotIdValidator {
        void operator()(const GUID& guidReceivingFXSlotID) const
        {
            if (guidReceivingFXSlotID != EAXPROPERTYID_EAX50_FXSlot0 &&
                guidReceivingFXSlotID != EAXPROPERTYID_EAX50_FXSlot1 &&
                guidReceivingFXSlotID != EAXPROPERTYID_EAX50_FXSlot2 &&
                guidReceivingFXSlotID != EAXPROPERTYID_EAX50_FXSlot3)
            {
                eax_fail_unknown_receiving_fx_slot_id();
            }
        }
    };

    struct Eax4SendSendValidator {
        void operator()(eax_long const lSend) const
        {
            eax_validate_range<Exception>(
                "Send",
                lSend,
                EAXSOURCE_MINSEND,
                EAXSOURCE_MAXSEND);
        }
    };

    struct Eax4SendSendHfValidator {
        void operator()(eax_long const lSendHF) const
        {
            eax_validate_range<Exception>(
                "Send HF",
                lSendHF,
                EAXSOURCE_MINSENDHF,
                EAXSOURCE_MAXSENDHF);
        }
    };

    template<typename TIdValidator>
    struct EaxSendValidator {
        void operator()(const EAXSOURCESENDPROPERTIES& props) const
        {
            TIdValidator{}(props.guidReceivingFXSlotID);
            Eax4SendSendValidator{}(props.mSend.lSend);
            Eax4SendSendHfValidator{}(props.mSend.lSendHF);
        }
    };

    using Eax4SendValidator = EaxSendValidator<Eax4SendReceivingFxSlotIdValidator>;
    using Eax5SendValidator = EaxSendValidator<Eax5SendReceivingFxSlotIdValidator>;

    template<typename TIdValidator>
    struct EaxOcclusionSendValidator {
        void operator()(const EAXSOURCEOCCLUSIONSENDPROPERTIES& props) const
        {
            TIdValidator{}(props.guidReceivingFXSlotID);
            Eax2SourceOcclusionValidator{}(props.mOcclusion.lOcclusion);
            Eax2SourceOcclusionLfRatioValidator{}(props.mOcclusion.flOcclusionLFRatio);
            Eax2SourceOcclusionRoomRatioValidator{}(props.mOcclusion.flOcclusionRoomRatio);
            Eax3SourceOcclusionDirectRatioValidator{}(props.mOcclusion.flOcclusionDirectRatio);
        }
    };

    using Eax4OcclusionSendValidator = EaxOcclusionSendValidator<Eax4SendReceivingFxSlotIdValidator>;
    using Eax5OcclusionSendValidator = EaxOcclusionSendValidator<Eax5SendReceivingFxSlotIdValidator>;

    template<typename TIdValidator>
    struct EaxExclusionSendValidator {
        void operator()(const EAXSOURCEEXCLUSIONSENDPROPERTIES& props) const
        {
            TIdValidator{}(props.guidReceivingFXSlotID);
            Eax3SourceExclusionValidator{}(props.mExclusion.lExclusion);
            Eax3SourceExclusionLfRatioValidator{}(props.mExclusion.flExclusionLFRatio);
        }
    };

    using Eax4ExclusionSendValidator = EaxExclusionSendValidator<Eax4SendReceivingFxSlotIdValidator>;
    using Eax5ExclusionSendValidator = EaxExclusionSendValidator<Eax5SendReceivingFxSlotIdValidator>;

    template<typename TIdValidator>
    struct EaxAllSendValidator {
        void operator()(const EAXSOURCEALLSENDPROPERTIES& props) const
        {
            TIdValidator{}(props.guidReceivingFXSlotID);
            Eax4SendSendValidator{}(props.mSend.lSend);
            Eax4SendSendHfValidator{}(props.mSend.lSendHF);
            Eax2SourceOcclusionValidator{}(props.mOcclusion.lOcclusion);
            Eax2SourceOcclusionLfRatioValidator{}(props.mOcclusion.flOcclusionLFRatio);
            Eax2SourceOcclusionRoomRatioValidator{}(props.mOcclusion.flOcclusionRoomRatio);
            Eax3SourceOcclusionDirectRatioValidator{}(props.mOcclusion.flOcclusionDirectRatio);
            Eax3SourceExclusionValidator{}(props.mExclusion.lExclusion);
            Eax3SourceExclusionLfRatioValidator{}(props.mExclusion.flExclusionLFRatio);
        }
    };

    using Eax4AllSendValidator = EaxAllSendValidator<Eax4SendReceivingFxSlotIdValidator>;
    using Eax5AllSendValidator = EaxAllSendValidator<Eax5SendReceivingFxSlotIdValidator>;

    // Send validators
    // ----------------------------------------------------------------------
    // Active FX slot ID validators

    struct Eax4ActiveFxSlotIdValidator {
        void operator()(const GUID &guid) const
        {
            if(guid != EAX_NULL_GUID && guid != EAX_PrimaryFXSlotID
                && guid != EAXPROPERTYID_EAX40_FXSlot0 && guid != EAXPROPERTYID_EAX40_FXSlot1
                && guid != EAXPROPERTYID_EAX40_FXSlot2 && guid != EAXPROPERTYID_EAX40_FXSlot3)
            {
                eax_fail_unknown_active_fx_slot_id();
            }
        }
    };

    struct Eax5ActiveFxSlotIdValidator {
        void operator()(const GUID &guid) const
        {
            if(guid != EAX_NULL_GUID && guid != EAX_PrimaryFXSlotID
                && guid != EAXPROPERTYID_EAX50_FXSlot0 && guid != EAXPROPERTYID_EAX50_FXSlot1
                && guid != EAXPROPERTYID_EAX50_FXSlot2 && guid != EAXPROPERTYID_EAX50_FXSlot3)
            {
                eax_fail_unknown_active_fx_slot_id();
            }
        }
    };

    // Active FX slot ID validators
    // ----------------------------------------------------------------------
    // Speaker level validators.

    struct Eax5SpeakerIdValidator {
        void operator()(eax_long const lSpeakerID) const
        {
            switch (lSpeakerID) {
                case EAXSPEAKER_FRONT_LEFT:
                case EAXSPEAKER_FRONT_CENTER:
                case EAXSPEAKER_FRONT_RIGHT:
                case EAXSPEAKER_SIDE_RIGHT:
                case EAXSPEAKER_REAR_RIGHT:
                case EAXSPEAKER_REAR_CENTER:
                case EAXSPEAKER_REAR_LEFT:
                case EAXSPEAKER_SIDE_LEFT:
                case EAXSPEAKER_LOW_FREQUENCY:
                    break;

                default:
                    eax_fail("Unknown speaker ID.");
            }
        }
    };

    struct Eax5SpeakerLevelValidator {
        void operator()(eax_long const lLevel) const
        {
            // TODO Use a range when the feature will be implemented.
            if (lLevel != EAXSOURCE_DEFAULTSPEAKERLEVEL)
                eax_fail("Speaker level out of range.");
        }
    };

    struct Eax5SpeakerAllValidator {
        void operator()(const EAXSPEAKERLEVELPROPERTIES& all) const
        {
            Eax5SpeakerIdValidator{}(all.lSpeakerID);
            Eax5SpeakerLevelValidator{}(all.lLevel);
        }
    };

    // Speaker level validators.
    // ----------------------------------------------------------------------

    struct Eax4SendIndexGetter {
        EaxFxSlotIndexValue operator()(const GUID &guid) const
        {
            if(guid == EAXPROPERTYID_EAX40_FXSlot0)
                return 0;
            if(guid == EAXPROPERTYID_EAX40_FXSlot1)
                return 1;
            if(guid == EAXPROPERTYID_EAX40_FXSlot2)
                return 2;
            if(guid == EAXPROPERTYID_EAX40_FXSlot3)
                return 3;
            eax_fail_unknown_receiving_fx_slot_id();
        }
    };

    struct Eax5SendIndexGetter {
        EaxFxSlotIndexValue operator()(const GUID &guid) const
        {
            if(guid == EAXPROPERTYID_EAX50_FXSlot0)
                return 0;
            if(guid == EAXPROPERTYID_EAX50_FXSlot1)
                return 1;
            if(guid == EAXPROPERTYID_EAX50_FXSlot2)
                return 2;
            if(guid == EAXPROPERTYID_EAX50_FXSlot3)
                return 3;
            eax_fail_unknown_receiving_fx_slot_id();
        }
    };

    [[noreturn]] static void eax_fail(std::string_view message);
    [[noreturn]] static void eax_fail_unknown_property_id();
    [[noreturn]] static void eax_fail_unknown_version();
    [[noreturn]] static void eax_fail_unknown_active_fx_slot_id();
    [[noreturn]] static void eax_fail_unknown_receiving_fx_slot_id();

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

    static auto eax_calculate_dst_occlusion_mb(eax_long src_occlusion_mb, f32 path_ratio,
        f32 lf_ratio) noexcept -> f32;

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

    static void eax_get_active_fx_slot_id(const EaxCall &call, const std::span<const GUID> srcids);
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

    template<std::invocable<const GUID&> TIndexGetter, typename TSrcSend>
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

    template<typename TSrcSend>
    static void eax4_defer_sends(const EaxCall &call, EaxSends &dst_sends,
        std::invocable<TSrcSend> auto validator)
    { eax_defer_sends<Eax4SendIndexGetter, TSrcSend>(call, dst_sends, std::move(validator)); }

    template<typename TSrcSend>
    static void eax5_defer_sends(const EaxCall &call, EaxSends &dst_sends,
        std::invocable<TSrcSend> auto validator)
    { eax_defer_sends<Eax5SendIndexGetter, TSrcSend>(call, dst_sends, std::move(validator)); }

    template<std::invocable<const GUID&> TValidator>
    static void eax_defer_active_fx_slot_id(const EaxCall &call, const std::span<GUID> dst_ids)
    {
        const auto src_ids = call.as_span<const GUID>(dst_ids.size());
        std::ranges::for_each(src_ids, TValidator{});
        std::ranges::uninitialized_copy(src_ids, dst_ids);
    }

    static void eax4_defer_active_fx_slot_id(const EaxCall &call, const std::span<GUID> dst_ids)
    {
        eax_defer_active_fx_slot_id<Eax4ActiveFxSlotIdValidator>(call, dst_ids);
    }

    static void eax5_defer_active_fx_slot_id(const EaxCall &call, const std::span<GUID> dst_ids)
    {
        eax_defer_active_fx_slot_id<Eax5ActiveFxSlotIdValidator>(call, dst_ids);
    }

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
    void eax_set_al_source_send(intrusive_ptr<EffectSlot> slot, usize sendidx,
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
