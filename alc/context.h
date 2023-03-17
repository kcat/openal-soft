#ifndef ALC_CONTEXT_H
#define ALC_CONTEXT_H

#include <atomic>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "al/listener.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "atomic.h"
#include "core/context.h"
#include "intrusive_ptr.h"
#include "vector.h"

#ifdef ALSOFT_EAX
#include "al/eax/call.h"
#include "al/eax/exception.h"
#include "al/eax/fx_slot_index.h"
#include "al/eax/fx_slots.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX

struct ALeffect;
struct ALeffectslot;
struct ALsource;

using uint = unsigned int;


struct SourceSubList {
    uint64_t FreeMask{~0_u64};
    ALsource *Sources{nullptr}; /* 64 */

    SourceSubList() noexcept = default;
    SourceSubList(const SourceSubList&) = delete;
    SourceSubList(SourceSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Sources{rhs.Sources}
    { rhs.FreeMask = ~0_u64; rhs.Sources = nullptr; }
    ~SourceSubList();

    SourceSubList& operator=(const SourceSubList&) = delete;
    SourceSubList& operator=(SourceSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Sources, rhs.Sources); return *this; }
};

struct EffectSlotSubList {
    uint64_t FreeMask{~0_u64};
    ALeffectslot *EffectSlots{nullptr}; /* 64 */

    EffectSlotSubList() noexcept = default;
    EffectSlotSubList(const EffectSlotSubList&) = delete;
    EffectSlotSubList(EffectSlotSubList&& rhs) noexcept
      : FreeMask{rhs.FreeMask}, EffectSlots{rhs.EffectSlots}
    { rhs.FreeMask = ~0_u64; rhs.EffectSlots = nullptr; }
    ~EffectSlotSubList();

    EffectSlotSubList& operator=(const EffectSlotSubList&) = delete;
    EffectSlotSubList& operator=(EffectSlotSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(EffectSlots, rhs.EffectSlots); return *this; }
};

struct ALCcontext : public al::intrusive_ref<ALCcontext>, ContextBase {
    const al::intrusive_ptr<ALCdevice> mALDevice;


    bool mPropsDirty{true};
    bool mDeferUpdates{false};

    std::mutex mPropLock;

    std::atomic<ALenum> mLastError{AL_NO_ERROR};

    DistanceModel mDistanceModel{DistanceModel::Default};
    bool mSourceDistanceModel{false};

    float mDopplerFactor{1.0f};
    float mDopplerVelocity{1.0f};
    float mSpeedOfSound{SpeedOfSoundMetersPerSec};
    float mAirAbsorptionGainHF{AirAbsorbGainHF};

    std::mutex mEventCbLock;
    ALEVENTPROCSOFT mEventCb{};
    void *mEventParam{nullptr};

    ALlistener mListener{};

    al::vector<SourceSubList> mSourceList;
    ALuint mNumSources{0};
    std::mutex mSourceLock;

    al::vector<EffectSlotSubList> mEffectSlotList;
    ALuint mNumEffectSlots{0u};
    std::mutex mEffectSlotLock;

    /* Default effect slot */
    std::unique_ptr<ALeffectslot> mDefaultSlot;

    const char *mExtensionList{nullptr};

    std::string mExtensionListOverride{};


    ALCcontext(al::intrusive_ptr<ALCdevice> device);
    ALCcontext(const ALCcontext&) = delete;
    ALCcontext& operator=(const ALCcontext&) = delete;
    ~ALCcontext();

    void init();
    /**
     * Removes the context from its device and removes it from being current on
     * the running thread or globally. Returns true if other contexts still
     * exist on the device.
     */
    bool deinit();

    /**
     * Defers/suspends updates for the given context's listener and sources.
     * This does *NOT* stop mixing, but rather prevents certain property
     * changes from taking effect. mPropLock must be held when called.
     */
    void deferUpdates() noexcept { mDeferUpdates = true; }

    /**
     * Resumes update processing after being deferred. mPropLock must be held
     * when called.
     */
    void processUpdates()
    {
        if(std::exchange(mDeferUpdates, false))
            applyAllUpdates();
    }

    /**
     * Applies all pending updates for the context, listener, effect slots, and
     * sources.
     */
    void applyAllUpdates();

#ifdef __USE_MINGW_ANSI_STDIO
    [[gnu::format(gnu_printf, 3, 4)]]
#else
    [[gnu::format(printf, 3, 4)]]
#endif
    void setError(ALenum errorCode, const char *msg, ...);

    /* Process-wide current context */
    static std::atomic<bool> sGlobalContextLock;
    static std::atomic<ALCcontext*> sGlobalContext;

private:
    /* Thread-local current context. */
    static thread_local ALCcontext *sLocalContext;

    /* Thread-local context handling. This handles attempting to release the
     * context which may have been left current when the thread is destroyed.
     */
    class ThreadCtx {
    public:
        ~ThreadCtx();
        void set(ALCcontext *ctx) const noexcept { sLocalContext = ctx; }
    };
    static thread_local ThreadCtx sThreadContext;

public:
    /* HACK: MinGW generates bad code when accessing an extern thread_local
     * object. Add a wrapper function for it that only accesses it where it's
     * defined.
     */
#ifdef __MINGW32__
    static ALCcontext *getThreadContext() noexcept;
    static void setThreadContext(ALCcontext *context) noexcept;
#else
    static ALCcontext *getThreadContext() noexcept { return sLocalContext; }
    static void setThreadContext(ALCcontext *context) noexcept { sThreadContext.set(context); }
#endif

    /* Default effect that applies to sources that don't have an effect on send 0. */
    static ALeffect sDefaultEffect;

    DEF_NEWDEL(ALCcontext)

#ifdef ALSOFT_EAX
public:
    bool hasEax() const noexcept { return mEaxIsInitialized; }
    bool eaxIsCapable() const noexcept;

    void eaxUninitialize() noexcept;

    ALenum eax_eax_set(
        const GUID* property_set_id,
        ALuint property_id,
        ALuint property_source_id,
        ALvoid* property_value,
        ALuint property_value_size);

    ALenum eax_eax_get(
        const GUID* property_set_id,
        ALuint property_id,
        ALuint property_source_id,
        ALvoid* property_value,
        ALuint property_value_size);

    void eaxSetLastError() noexcept;

    EaxFxSlotIndex eaxGetPrimaryFxSlotIndex() const noexcept
    { return mEaxPrimaryFxSlotIndex; }

    const ALeffectslot& eaxGetFxSlot(EaxFxSlotIndexValue fx_slot_index) const
    { return mEaxFxSlots.get(fx_slot_index); }
    ALeffectslot& eaxGetFxSlot(EaxFxSlotIndexValue fx_slot_index)
    { return mEaxFxSlots.get(fx_slot_index); }

    bool eaxNeedsCommit() const noexcept { return mEaxNeedsCommit; }
    void eaxCommit();

    void eaxCommitFxSlots()
    { mEaxFxSlots.commit(); }

private:
    static constexpr auto eax_primary_fx_slot_id_dirty_bit = EaxDirtyFlags{1} << 0;
    static constexpr auto eax_distance_factor_dirty_bit = EaxDirtyFlags{1} << 1;
    static constexpr auto eax_air_absorption_hf_dirty_bit = EaxDirtyFlags{1} << 2;
    static constexpr auto eax_hf_reference_dirty_bit = EaxDirtyFlags{1} << 3;
    static constexpr auto eax_macro_fx_factor_dirty_bit = EaxDirtyFlags{1} << 4;

    using Eax4Props = EAX40CONTEXTPROPERTIES;

    struct Eax4State {
        Eax4Props i; // Immediate.
        Eax4Props d; // Deferred.
    };

    using Eax5Props = EAX50CONTEXTPROPERTIES;

    struct Eax5State {
        Eax5Props i; // Immediate.
        Eax5Props d; // Deferred.
    };

    class ContextException : public EaxException
    {
    public:
        explicit ContextException(const char* message)
            : EaxException{"EAX_CONTEXT", message}
        {}
    };

    struct Eax4PrimaryFxSlotIdValidator {
        void operator()(const GUID& guidPrimaryFXSlotID) const
        {
            if(guidPrimaryFXSlotID != EAX_NULL_GUID &&
                guidPrimaryFXSlotID != EAXPROPERTYID_EAX40_FXSlot0 &&
                guidPrimaryFXSlotID != EAXPROPERTYID_EAX40_FXSlot1 &&
                guidPrimaryFXSlotID != EAXPROPERTYID_EAX40_FXSlot2 &&
                guidPrimaryFXSlotID != EAXPROPERTYID_EAX40_FXSlot3)
            {
                eax_fail_unknown_primary_fx_slot_id();
            }
        }
    };

    struct Eax4DistanceFactorValidator {
        void operator()(float flDistanceFactor) const
        {
            eax_validate_range<ContextException>(
                "Distance Factor",
                flDistanceFactor,
                EAXCONTEXT_MINDISTANCEFACTOR,
                EAXCONTEXT_MAXDISTANCEFACTOR);
        }
    };

    struct Eax4AirAbsorptionHfValidator {
        void operator()(float flAirAbsorptionHF) const
        {
            eax_validate_range<ContextException>(
                "Air Absorption HF",
                flAirAbsorptionHF,
                EAXCONTEXT_MINAIRABSORPTIONHF,
                EAXCONTEXT_MAXAIRABSORPTIONHF);
        }
    };

    struct Eax4HfReferenceValidator {
        void operator()(float flHFReference) const
        {
            eax_validate_range<ContextException>(
                "HF Reference",
                flHFReference,
                EAXCONTEXT_MINHFREFERENCE,
                EAXCONTEXT_MAXHFREFERENCE);
        }
    };

    struct Eax4AllValidator {
        void operator()(const EAX40CONTEXTPROPERTIES& all) const
        {
            Eax4PrimaryFxSlotIdValidator{}(all.guidPrimaryFXSlotID);
            Eax4DistanceFactorValidator{}(all.flDistanceFactor);
            Eax4AirAbsorptionHfValidator{}(all.flAirAbsorptionHF);
            Eax4HfReferenceValidator{}(all.flHFReference);
        }
    };

    struct Eax5PrimaryFxSlotIdValidator {
        void operator()(const GUID& guidPrimaryFXSlotID) const
        {
            if(guidPrimaryFXSlotID != EAX_NULL_GUID &&
                guidPrimaryFXSlotID != EAXPROPERTYID_EAX50_FXSlot0 &&
                guidPrimaryFXSlotID != EAXPROPERTYID_EAX50_FXSlot1 &&
                guidPrimaryFXSlotID != EAXPROPERTYID_EAX50_FXSlot2 &&
                guidPrimaryFXSlotID != EAXPROPERTYID_EAX50_FXSlot3)
            {
                eax_fail_unknown_primary_fx_slot_id();
            }
        }
    };

    struct Eax5MacroFxFactorValidator {
        void operator()(float flMacroFXFactor) const
        {
            eax_validate_range<ContextException>(
                "Macro FX Factor",
                flMacroFXFactor,
                EAXCONTEXT_MINMACROFXFACTOR,
                EAXCONTEXT_MAXMACROFXFACTOR);
        }
    };

    struct Eax5AllValidator {
        void operator()(const EAX50CONTEXTPROPERTIES& all) const
        {
            Eax5PrimaryFxSlotIdValidator{}(all.guidPrimaryFXSlotID);
            Eax4DistanceFactorValidator{}(all.flDistanceFactor);
            Eax4AirAbsorptionHfValidator{}(all.flAirAbsorptionHF);
            Eax4HfReferenceValidator{}(all.flHFReference);
            Eax5MacroFxFactorValidator{}(all.flMacroFXFactor);
        }
    };

    struct Eax5EaxVersionValidator {
        void operator()(unsigned long ulEAXVersion) const
        {
            eax_validate_range<ContextException>(
                "EAX version",
                ulEAXVersion,
                EAXCONTEXT_MINEAXSESSION,
                EAXCONTEXT_MAXEAXSESSION);
        }
    };

    struct Eax5MaxActiveSendsValidator {
        void operator()(unsigned long ulMaxActiveSends) const
        {
            eax_validate_range<ContextException>(
                "Max Active Sends",
                ulMaxActiveSends,
                EAXCONTEXT_MINMAXACTIVESENDS,
                EAXCONTEXT_MAXMAXACTIVESENDS);
        }
    };

    struct Eax5SessionAllValidator {
        void operator()(const EAXSESSIONPROPERTIES& all) const
        {
            Eax5EaxVersionValidator{}(all.ulEAXVersion);
            Eax5MaxActiveSendsValidator{}(all.ulMaxActiveSends);
        }
    };

    struct Eax5SpeakerConfigValidator {
        void operator()(unsigned long ulSpeakerConfig) const
        {
            eax_validate_range<ContextException>(
                "Speaker Config",
                ulSpeakerConfig,
                EAXCONTEXT_MINSPEAKERCONFIG,
                EAXCONTEXT_MAXSPEAKERCONFIG);
        }
    };

    bool mEaxIsInitialized{};
    bool mEaxIsTried{};

    long mEaxLastError{};
    unsigned long mEaxSpeakerConfig{};

    EaxFxSlotIndex mEaxPrimaryFxSlotIndex{};
    EaxFxSlots mEaxFxSlots{};

    int mEaxVersion{}; // Current EAX version.
    bool mEaxNeedsCommit{};
    EaxDirtyFlags mEaxDf{}; // Dirty flags for the current EAX version.
    Eax5State mEax123{}; // EAX1/EAX2/EAX3 state.
    Eax4State mEax4{}; // EAX4 state.
    Eax5State mEax5{}; // EAX5 state.
    Eax5Props mEax{}; // Current EAX state.
    EAXSESSIONPROPERTIES mEaxSession{};

    [[noreturn]] static void eax_fail(const char* message);
    [[noreturn]] static void eax_fail_unknown_property_set_id();
    [[noreturn]] static void eax_fail_unknown_primary_fx_slot_id();
    [[noreturn]] static void eax_fail_unknown_property_id();
    [[noreturn]] static void eax_fail_unknown_version();

    // Gets a value from EAX call,
    // validates it,
    // and updates the current value.
    template<typename TValidator, typename TProperty>
    static void eax_set(const EaxCall& call, TProperty& property)
    {
        const auto& value = call.get_value<ContextException, const TProperty>();
        TValidator{}(value);
        property = value;
    }

    // Gets a new value from EAX call,
    // validates it,
    // updates the deferred value,
    // updates a dirty flag.
    template<
        typename TValidator,
        EaxDirtyFlags TDirtyBit,
        typename TMemberResult,
        typename TProps,
        typename TState>
    void eax_defer(const EaxCall& call, TState& state, TMemberResult TProps::*member) noexcept
    {
        const auto& src = call.get_value<ContextException, const TMemberResult>();
        TValidator{}(src);
        const auto& dst_i = state.i.*member;
        auto& dst_d = state.d.*member;
        dst_d = src;

        if(dst_i != dst_d)
            mEaxDf |= TDirtyBit;
    }

    template<
        EaxDirtyFlags TDirtyBit,
        typename TMemberResult,
        typename TProps,
        typename TState>
    void eax_context_commit_property(TState& state, EaxDirtyFlags& dst_df,
        TMemberResult TProps::*member) noexcept
    {
        if((mEaxDf & TDirtyBit) != EaxDirtyFlags{})
        {
            dst_df |= TDirtyBit;
            const auto& src_d = state.d.*member;
            state.i.*member = src_d;
            mEax.*member = src_d;
        }
    }

    void eax_initialize_extensions();
    void eax_initialize();

    bool eax_has_no_default_effect_slot() const noexcept;
    void eax_ensure_no_default_effect_slot() const;
    bool eax_has_enough_aux_sends() const noexcept;
    void eax_ensure_enough_aux_sends() const;
    void eax_ensure_compatibility();

    unsigned long eax_detect_speaker_configuration() const;
    void eax_update_speaker_configuration();

    void eax_set_last_error_defaults() noexcept;
    void eax_session_set_defaults() noexcept;
    static void eax4_context_set_defaults(Eax4Props& props) noexcept;
    static void eax4_context_set_defaults(Eax4State& state) noexcept;
    static void eax5_context_set_defaults(Eax5Props& props) noexcept;
    static void eax5_context_set_defaults(Eax5State& state) noexcept;
    void eax_context_set_defaults();
    void eax_set_defaults();

    void eax_dispatch_fx_slot(const EaxCall& call);
    void eax_dispatch_source(const EaxCall& call);

    void eax_get_misc(const EaxCall& call);
    void eax4_get(const EaxCall& call, const Eax4Props& props);
    void eax5_get(const EaxCall& call, const Eax5Props& props);
    void eax_get(const EaxCall& call);

    void eax_context_commit_primary_fx_slot_id();
    void eax_context_commit_distance_factor();
    void eax_context_commit_air_absorbtion_hf();
    void eax_context_commit_hf_reference();
    void eax_context_commit_macro_fx_factor();

    void eax_initialize_fx_slots();

    void eax_update_sources();

    void eax_set_misc(const EaxCall& call);
    void eax4_defer_all(const EaxCall& call, Eax4State& state);
    void eax4_defer(const EaxCall& call, Eax4State& state);
    void eax5_defer_all(const EaxCall& call, Eax5State& state);
    void eax5_defer(const EaxCall& call, Eax5State& state);
    void eax_set(const EaxCall& call);

    void eax4_context_commit(Eax4State& state, EaxDirtyFlags& dst_df);
    void eax5_context_commit(Eax5State& state, EaxDirtyFlags& dst_df);
    void eax_context_commit();
#endif // ALSOFT_EAX
};

using ContextRef = al::intrusive_ptr<ALCcontext>;

ContextRef GetContextRef(void);

void UpdateContextProps(ALCcontext *context);


extern bool TrapALError;


#ifdef ALSOFT_EAX
ALenum AL_APIENTRY EAXSet(
    const GUID* property_set_id,
    ALuint property_id,
    ALuint property_source_id,
    ALvoid* property_value,
    ALuint property_value_size) noexcept;

ALenum AL_APIENTRY EAXGet(
    const GUID* property_set_id,
    ALuint property_id,
    ALuint property_source_id,
    ALvoid* property_value,
    ALuint property_value_size) noexcept;
#endif // ALSOFT_EAX

#endif /* ALC_CONTEXT_H */
