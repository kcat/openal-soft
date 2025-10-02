#ifndef AL_SOURCE_H
#define AL_SOURCE_H

#include "config.h"

#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
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
} // namespace al
struct ALbuffer;
struct ALeffectslot;
enum class Resampler : uint8_t;

enum class SourceStereo : bool {
    Normal = AL_NORMAL_SOFT,
    Enhanced = AL_SUPER_STEREO_SOFT
};

inline constexpr size_t DefaultSendCount{2};

inline constexpr ALuint InvalidVoiceIndex{std::numeric_limits<ALuint>::max()};

inline bool sBufferSubDataCompat{false};

struct ALbufferQueueItem : public VoiceBufferItem {
    al::intrusive_ptr<ALbuffer> mBuffer;
};


#if ALSOFT_EAX
/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
class EaxSourceException : public EaxException {
public:
    explicit EaxSourceException(const std::string_view message)
        : EaxException{"EAX_SOURCE", message}
    { }
};
#endif // ALSOFT_EAX

struct ALsource {
    /** Source properties. */
    float Pitch{1.0f};
    float Gain{1.0f};
    float OuterGain{0.0f};
    float MinGain{0.0f};
    float MaxGain{1.0f};
    float InnerAngle{360.0f};
    float OuterAngle{360.0f};
    float RefDistance{1.0f};
    float MaxDistance{std::numeric_limits<float>::max()};
    float RolloffFactor{1.0f};
#if ALSOFT_EAX
    // For EAXSOURCE_ROLLOFFFACTOR, which is distinct from and added to
    // AL_ROLLOFF_FACTOR
    float RolloffFactor2{0.0f};
#endif
    std::array<float,3> Position{{0.0f, 0.0f, 0.0f}};
    std::array<float,3> Velocity{{0.0f, 0.0f, 0.0f}};
    std::array<float,3> Direction{{0.0f, 0.0f, 0.0f}};
    std::array<float,3> OrientAt{{0.0f, 0.0f, -1.0f}};
    std::array<float,3> OrientUp{{0.0f, 1.0f,  0.0f}};
    bool HeadRelative{false};
    bool Looping{false};
    DistanceModel mDistanceModel{DistanceModel::Default};
    Resampler mResampler{ResamplerDefault};
    DirectMode DirectChannels{DirectMode::Off};
    SpatializeMode mSpatialize{SpatializeMode::Auto};
    SourceStereo mStereoMode{SourceStereo::Normal};
    bool mPanningEnabled{false};

    bool DryGainHFAuto{true};
    bool WetGainAuto{true};
    bool WetGainHFAuto{true};
    float OuterGainHF{1.0f};

    float AirAbsorptionFactor{0.0f};
    float RoomRolloffFactor{0.0f};
    float DopplerFactor{1.0f};

    /* NOTE: Stereo pan angles are specified in radians, counter-clockwise
     * rather than clockwise.
     */
    std::array<float,2> StereoPan{{std::numbers::pi_v<float>/6.0f,
        -std::numbers::pi_v<float>/6.0f}};

    float Radius{0.0f};
    float EnhWidth{0.593f};
    float mPan{0.0f};

    /** Direct filter and auxiliary send info. */
    struct DirectData {
        float Gain{};
        float GainHF{};
        float HFReference{};
        float GainLF{};
        float LFReference{};
    };
    DirectData Direct;

    struct SendData {
        al::intrusive_ptr<ALeffectslot> mSlot;
        float mGain{};
        float mGainHF{};
        float mHFReference{};
        float mGainLF{};
        float mLFReference{};
    };
    std::array<SendData,MaxSendCount> Send;

    /**
     * Last user-specified offset, and the offset type (bytes, samples, or
     * seconds).
     */
    double Offset{0.0};
    ALenum OffsetType{AL_NONE};

    /** Source type (static, streaming, or undetermined) */
    ALenum SourceType{AL_UNDETERMINED};

    /** Source state (initial, playing, paused, or stopped) */
    ALenum state{AL_INITIAL};

    /** Source Buffer Queue head. */
    std::deque<ALbufferQueueItem> mQueue;

    bool mPropsDirty{true};

    /* Index into the context's Voices array. Lazily updated, only checked and
     * reset when looking up the voice.
     */
    ALuint VoiceIdx{InvalidVoiceIndex};

    /** Self ID */
    ALuint id{0};


    ALsource() noexcept;
    ~ALsource();

    ALsource(const ALsource&) = delete;
    ALsource& operator=(const ALsource&) = delete;

    static void SetName(gsl::not_null<al::Context*> context, ALuint id, std::string_view name);

    DISABLE_ALLOC

#if ALSOFT_EAX
public:
    void eaxInitialize(gsl::not_null<al::Context*> context) noexcept;
    void eaxDispatch(const EaxCall& call) { call.is_get() ? eax_get(call) : eax_set(call); }
    void eaxCommit();
    void eaxMarkAsChanged() noexcept { mEaxChanged = true; }

    static auto EaxLookupSource(gsl::not_null<al::Context*> al_context LIFETIMEBOUND,
        ALuint source_id) noexcept -> ALsource*;

private:
    using Exception = EaxSourceException;

    static constexpr auto eax_max_speakers{9u};

    using EaxFxSlotIds = std::array<const GUID*,EAX_MAX_FXSLOTS>;

    static constexpr const EaxFxSlotIds eax4_fx_slot_ids{
        &EAXPROPERTYID_EAX40_FXSlot0,
        &EAXPROPERTYID_EAX40_FXSlot1,
        &EAXPROPERTYID_EAX40_FXSlot2,
        &EAXPROPERTYID_EAX40_FXSlot3,
    };

    static constexpr const EaxFxSlotIds eax5_fx_slot_ids{
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

    al::Context *mEaxAlContext{};
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
        void operator()(float reverb_mix) const
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
        void operator()(long lDirect) const
        {
            eax_validate_range<Exception>(
                "Direct",
                lDirect,
                EAXSOURCE_MINDIRECT,
                EAXSOURCE_MAXDIRECT);
        }
    };

    struct Eax2SourceDirectHfValidator {
        void operator()(long lDirectHF) const
        {
            eax_validate_range<Exception>(
                "Direct HF",
                lDirectHF,
                EAXSOURCE_MINDIRECTHF,
                EAXSOURCE_MAXDIRECTHF);
        }
    };

    struct Eax2SourceRoomValidator {
        void operator()(long lRoom) const
        {
            eax_validate_range<Exception>(
                "Room",
                lRoom,
                EAXSOURCE_MINROOM,
                EAXSOURCE_MAXROOM);
        }
    };

    struct Eax2SourceRoomHfValidator {
        void operator()(long lRoomHF) const
        {
            eax_validate_range<Exception>(
                "Room HF",
                lRoomHF,
                EAXSOURCE_MINROOMHF,
                EAXSOURCE_MAXROOMHF);
        }
    };

    struct Eax2SourceRoomRolloffFactorValidator {
        void operator()(float flRoomRolloffFactor) const
        {
            eax_validate_range<Exception>(
                "Room Rolloff Factor",
                flRoomRolloffFactor,
                EAXSOURCE_MINROOMROLLOFFFACTOR,
                EAXSOURCE_MAXROOMROLLOFFFACTOR);
        }
    };

    struct Eax2SourceObstructionValidator {
        void operator()(long lObstruction) const
        {
            eax_validate_range<Exception>(
                "Obstruction",
                lObstruction,
                EAXSOURCE_MINOBSTRUCTION,
                EAXSOURCE_MAXOBSTRUCTION);
        }
    };

    struct Eax2SourceObstructionLfRatioValidator {
        void operator()(float flObstructionLFRatio) const
        {
            eax_validate_range<Exception>(
                "Obstruction LF Ratio",
                flObstructionLFRatio,
                EAXSOURCE_MINOBSTRUCTIONLFRATIO,
                EAXSOURCE_MAXOBSTRUCTIONLFRATIO);
        }
    };

    struct Eax2SourceOcclusionValidator {
        void operator()(long lOcclusion) const
        {
            eax_validate_range<Exception>(
                "Occlusion",
                lOcclusion,
                EAXSOURCE_MINOCCLUSION,
                EAXSOURCE_MAXOCCLUSION);
        }
    };

    struct Eax2SourceOcclusionLfRatioValidator {
        void operator()(float flOcclusionLFRatio) const
        {
            eax_validate_range<Exception>(
                "Occlusion LF Ratio",
                flOcclusionLFRatio,
                EAXSOURCE_MINOCCLUSIONLFRATIO,
                EAXSOURCE_MAXOCCLUSIONLFRATIO);
        }
    };

    struct Eax2SourceOcclusionRoomRatioValidator {
        void operator()(float flOcclusionRoomRatio) const
        {
            eax_validate_range<Exception>(
                "Occlusion Room Ratio",
                flOcclusionRoomRatio,
                EAXSOURCE_MINOCCLUSIONROOMRATIO,
                EAXSOURCE_MAXOCCLUSIONROOMRATIO);
        }
    };

    struct Eax2SourceOutsideVolumeHfValidator {
        void operator()(long lOutsideVolumeHF) const
        {
            eax_validate_range<Exception>(
                "Outside Volume HF",
                lOutsideVolumeHF,
                EAXSOURCE_MINOUTSIDEVOLUMEHF,
                EAXSOURCE_MAXOUTSIDEVOLUMEHF);
        }
    };

    struct Eax2SourceAirAbsorptionFactorValidator {
        void operator()(float flAirAbsorptionFactor) const
        {
            eax_validate_range<Exception>(
                "Air Absorption Factor",
                flAirAbsorptionFactor,
                EAXSOURCE_MINAIRABSORPTIONFACTOR,
                EAXSOURCE_MAXAIRABSORPTIONFACTOR);
        }
    };

    struct Eax2SourceFlagsValidator {
        void operator()(unsigned long dwFlags) const
        {
            eax_validate_range<Exception>(
                "Flags",
                dwFlags,
                0UL,
                ~EAX20SOURCEFLAGS_RESERVED);
        }
    };

    struct Eax3SourceOcclusionDirectRatioValidator {
        void operator()(float flOcclusionDirectRatio) const
        {
            eax_validate_range<Exception>(
                "Occlusion Direct Ratio",
                flOcclusionDirectRatio,
                EAXSOURCE_MINOCCLUSIONDIRECTRATIO,
                EAXSOURCE_MAXOCCLUSIONDIRECTRATIO);
        }
    };

    struct Eax3SourceExclusionValidator {
        void operator()(long lExclusion) const
        {
            eax_validate_range<Exception>(
                "Exclusion",
                lExclusion,
                EAXSOURCE_MINEXCLUSION,
                EAXSOURCE_MAXEXCLUSION);
        }
    };

    struct Eax3SourceExclusionLfRatioValidator {
        void operator()(float flExclusionLFRatio) const
        {
            eax_validate_range<Exception>(
                "Exclusion LF Ratio",
                flExclusionLFRatio,
                EAXSOURCE_MINEXCLUSIONLFRATIO,
                EAXSOURCE_MAXEXCLUSIONLFRATIO);
        }
    };

    struct Eax3SourceDopplerFactorValidator {
        void operator()(float flDopplerFactor) const
        {
            eax_validate_range<Exception>(
                "Doppler Factor",
                flDopplerFactor,
                EAXSOURCE_MINDOPPLERFACTOR,
                EAXSOURCE_MAXDOPPLERFACTOR);
        }
    };

    struct Eax3SourceRolloffFactorValidator {
        void operator()(float flRolloffFactor) const
        {
            eax_validate_range<Exception>(
                "Rolloff Factor",
                flRolloffFactor,
                EAXSOURCE_MINROLLOFFFACTOR,
                EAXSOURCE_MAXROLLOFFFACTOR);
        }
    };

    struct Eax5SourceMacroFXFactorValidator {
        void operator()(float flMacroFXFactor) const
        {
            eax_validate_range<Exception>(
                "Macro FX Factor",
                flMacroFXFactor,
                EAXSOURCE_MINMACROFXFACTOR,
                EAXSOURCE_MAXMACROFXFACTOR);
        }
    };

    struct Eax5SourceFlagsValidator {
        void operator()(unsigned long dwFlags) const
        {
            eax_validate_range<Exception>(
                "Flags",
                dwFlags,
                0UL,
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
        void operator()(long lSend) const
        {
            eax_validate_range<Exception>(
                "Send",
                lSend,
                EAXSOURCE_MINSEND,
                EAXSOURCE_MAXSEND);
        }
    };

    struct Eax4SendSendHfValidator {
        void operator()(long lSendHF) const
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
        void operator()(long lSpeakerID) const
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
        void operator()(long lLevel) const
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

    [[noreturn]] static void eax_fail(const std::string_view message);
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

    static auto eax_calculate_dst_occlusion_mb(long src_occlusion_mb, float path_ratio,
        float lf_ratio) noexcept -> float;

    [[nodiscard]] auto eax_create_direct_filter_param() const noexcept -> EaxAlLowPassParam;

    [[nodiscard]] auto eax_create_room_filter_param(const ALeffectslot& fx_slot,
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
    void eax_set_al_source_send(al::intrusive_ptr<ALeffectslot> slot, size_t sendidx,
        const EaxAlLowPassParam &filter);

    void eax_commit_active_fx_slots();
#endif // ALSOFT_EAX
};

void UpdateAllSourceProps(gsl::not_null<al::Context*> context);

struct SourceSubList {
    uint64_t FreeMask{~0_u64};
    gsl::owner<std::array<ALsource,64>*> Sources{nullptr};

    SourceSubList() noexcept = default;
    SourceSubList(const SourceSubList&) = delete;
    SourceSubList(SourceSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Sources{rhs.Sources}
    { rhs.FreeMask = ~0_u64; rhs.Sources = nullptr; }
    ~SourceSubList();

    SourceSubList& operator=(const SourceSubList&) = delete;
    SourceSubList& operator=(SourceSubList&& rhs) & noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Sources, rhs.Sources); return *this; }
};

#endif
