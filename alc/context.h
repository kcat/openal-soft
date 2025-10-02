#ifndef ALC_CONTEXT_H
#define ALC_CONTEXT_H

#include "config.h"

#include <atomic>
#include <bitset>
#include <concepts>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "al/listener.h"
#include "althreads.h"
#include "core/context.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"
#include "opthelpers.h"

#if ALSOFT_EAX
#include "al/eax/api.h"
#include "al/eax/exception.h"
#include "al/eax/fx_slot_index.h"
#include "al/eax/fx_slots.h"
#include "al/eax/utils.h"

class EaxCall;
#endif // ALSOFT_EAX

struct ALeffect;
struct ALeffectslot;
struct DebugGroup;
struct EffectSlotSubList;
struct SourceSubList;

enum class DebugSource : std::uint8_t;
enum class DebugType : std::uint8_t;
enum class DebugSeverity : std::uint8_t;

using uint = unsigned int;


enum ContextFlags {
    DebugBit = 0, /* ALC_CONTEXT_DEBUG_BIT_EXT */
};
using ContextFlagBitset = std::bitset<sizeof(ALuint)*8>;


struct DebugLogEntry {
    const DebugSource mSource;
    const DebugType mType;
    const DebugSeverity mSeverity;
    const uint mId;

    std::string mMessage;

    template<typename T>
    DebugLogEntry(DebugSource source, DebugType type, uint id, DebugSeverity severity, T&& message)
        : mSource{source}, mType{type}, mSeverity{severity}, mId{id}
        , mMessage{std::forward<T>(message)}
    { }
    DebugLogEntry(const DebugLogEntry&) = default;
    DebugLogEntry(DebugLogEntry&&) = default;
};


struct ALCcontext { };

namespace al {
struct Device;
struct Context;

struct ContextDeleter { void operator()(gsl::owner<Context*> context) const noexcept; };
struct Context final : public ALCcontext, intrusive_ref<Context,ContextDeleter>, ContextBase {
    const gsl::not_null<intrusive_ptr<Device>> mALDevice;

    bool mPropsDirty{true};
    bool mDeferUpdates{false};

    std::mutex mPropLock;

    tss<ALenum> mLastThreadError{AL_NO_ERROR};

    const ContextFlagBitset mContextFlags;
    std::atomic<bool> mDebugEnabled{false};

    DistanceModel mDistanceModel{DistanceModel::Default};
    bool mSourceDistanceModel{false};

    float mDopplerFactor{1.0f};
    float mDopplerVelocity{1.0f};
    float mSpeedOfSound{SpeedOfSoundMetersPerSec};
    float mAirAbsorptionGainHF{AirAbsorbGainHF};

    std::mutex mEventCbLock;
    ALEVENTPROCSOFT mEventCb{};
    void *mEventParam{nullptr};

    std::mutex mDebugCbLock;
    ALDEBUGPROCEXT mDebugCb{};
    void *mDebugParam{nullptr};
    std::vector<DebugGroup> mDebugGroups;
    std::deque<DebugLogEntry> mDebugLog;

    ALlistener mListener{};

    std::vector<SourceSubList> mSourceList;
    ALuint mNumSources{0};
    std::mutex mSourceLock;

    std::vector<EffectSlotSubList> mEffectSlotList;
    ALuint mNumEffectSlots{0u};
    std::mutex mEffectSlotLock;

    /* Default effect slot */
    std::unique_ptr<ALeffectslot> mDefaultSlot;

    std::vector<std::string_view> mExtensions;
    std::string mExtensionsString;

    std::unordered_map<ALuint,std::string> mSourceNames;
    std::unordered_map<ALuint,std::string> mEffectSlotNames;

    /**
     * Removes the context from being current on the running thread or
     * globally, and stops the event thread.
     */
    void deinit();

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

    void setErrorImpl(ALenum errorCode, const std::string_view fmt, std::format_args args);

    template<typename ...Args>
    void setError(ALenum errorCode, std::format_string<Args...> msg, Args&& ...args)
    { setErrorImpl(errorCode, msg.get(), std::make_format_args(args...)); }

    [[noreturn]]
    void throw_error_impl(ALenum errorCode, const std::string_view fmt, std::format_args args);

    template<typename ...Args> [[noreturn]]
    void throw_error(ALenum errorCode, std::format_string<Args...> fmt, Args&&... args)
    { throw_error_impl(errorCode, fmt.get(), std::make_format_args(args...)); }

    void sendDebugMessage(std::unique_lock<std::mutex> &debuglock, DebugSource source,
        DebugType type, ALuint id, DebugSeverity severity, std::string_view message);

    void debugMessage(DebugSource source, DebugType type, ALuint id, DebugSeverity severity,
        std::string_view message)
    {
        if(!mDebugEnabled.load(std::memory_order_relaxed)) [[likely]]
            return;
        std::unique_lock<std::mutex> debuglock{mDebugCbLock};
        sendDebugMessage(debuglock, source, type, id, severity, message);
    }

    static auto Create(const gsl::not_null<intrusive_ptr<Device>> &device, ContextFlagBitset flags)
        -> intrusive_ptr<Context>;

    /* Process-wide current context */
    static std::atomic<bool> sGlobalContextLock;
    static std::atomic<Context*> sGlobalContext;

protected:
    ~Context();

private:
    Context(const gsl::not_null<intrusive_ptr<Device>> &device, ContextFlagBitset flags);

    void init();

    /* Thread-local current context. */
    static inline thread_local Context *sLocalContext{};

    /* Thread-local context handling. This handles attempting to release the
     * context which may have been left current when the thread is destroyed.
     */
    class ThreadCtx {
    public:
        ThreadCtx() = default;
        ThreadCtx(const ThreadCtx&) = delete;
        auto operator=(const ThreadCtx&) -> ThreadCtx& = delete;

        ~ThreadCtx();
        /* NOLINTBEGIN(readability-convert-member-functions-to-static)
         * This should be non-static to invoke construction of the thread-local
         * sThreadContext, so that it's destructor gets run at thread exit to
         * clear sLocalContext (which isn't a member variable to make read
         * access efficient).
         */
        void set(Context *ctx) const noexcept { sLocalContext = ctx; }
        /* NOLINTEND(readability-convert-member-functions-to-static) */
    };
    static thread_local ThreadCtx sThreadContext;

    friend ContextDeleter;

public:
    static Context *getThreadContext() noexcept { return sLocalContext; }
    static void setThreadContext(Context *context) noexcept { sThreadContext.set(context); }

    /* Default effect that applies to sources that don't have an effect on send 0. */
    static ALeffect sDefaultEffect;

#if ALSOFT_EAX
    bool hasEax() const noexcept { return mEaxIsInitialized; }
    bool eaxIsCapable() const noexcept;

    void eaxUninitialize() noexcept;

    ALenum eax_eax_set(const GUID *property_set_id, ALuint property_id, ALuint property_source_id,
        ALvoid *property_value, ALuint property_value_size);

    ALenum eax_eax_get(const GUID *property_set_id, ALuint property_id, ALuint property_source_id,
        ALvoid *property_value, ALuint property_value_size);

    void eaxSetLastError() noexcept;

    [[nodiscard]]
    auto eaxGetDistanceFactor() const noexcept -> float { return mEax.flDistanceFactor; }

    [[nodiscard]]
    auto eaxGetPrimaryFxSlotIndex() const noexcept -> EaxFxSlotIndex
    { return mEaxPrimaryFxSlotIndex; }

    const ALeffectslot& eaxGetFxSlot(EaxFxSlotIndexValue fx_slot_index) const LIFETIMEBOUND
    { return mEaxFxSlots.get(fx_slot_index); }
    ALeffectslot& eaxGetFxSlot(EaxFxSlotIndexValue fx_slot_index) LIFETIMEBOUND
    { return mEaxFxSlots.get(fx_slot_index); }

    bool eaxNeedsCommit() const noexcept { return mEaxNeedsCommit; }
    void eaxCommit();

    void eaxCommitFxSlots() const { mEaxFxSlots.commit(); }

private:
    enum {
        eax_primary_fx_slot_id_dirty_bit,
        eax_distance_factor_dirty_bit,
        eax_air_absorption_hf_dirty_bit,
        eax_hf_reference_dirty_bit,
        eax_macro_fx_factor_dirty_bit,
        eax_dirty_bit_count
    };

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

    /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
    class ContextException final : public EaxException {
    public:
        explicit ContextException(const std::string_view message)
            : EaxException{"EAX_CONTEXT", message}
        { }
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
    std::bitset<eax_dirty_bit_count> mEaxDf; // Dirty flags for the current EAX version.
    Eax5State mEax123{}; // EAX1/EAX2/EAX3 state.
    Eax4State mEax4{}; // EAX4 state.
    Eax5State mEax5{}; // EAX5 state.
    Eax5Props mEax{}; // Current EAX state.
    EAXSESSIONPROPERTIES mEaxSession{};

    [[noreturn]] static void eax_fail(const std::string_view message);
    [[noreturn]] static void eax_fail_unknown_property_set_id();
    [[noreturn]] static void eax_fail_unknown_primary_fx_slot_id();
    [[noreturn]] static void eax_fail_unknown_property_id();
    [[noreturn]] static void eax_fail_unknown_version();

    /* Gets a value from EAX call, validates it, and updates the current value. */
    template<typename TValidator>
    static void eax_set(const EaxCall &call, auto &property)
    {
        const auto &value = call.load<const std::remove_cvref_t<decltype(property)>>();
        TValidator{}(value);
        property = value;
    }

    /* Gets a new value from EAX call, validates it, updates the deferred
     * value, and updates a dirty flag.
     */
    template<typename TValidator>
    void eax_defer(const EaxCall &call, auto &state, size_t dirty_bit, auto member)
    {
        static_assert(std::invocable<decltype(member), decltype(state.i)>);
        using TMemberResult = std::invoke_result_t<decltype(member), decltype(state.i)>;
        const auto &src = call.load<const std::remove_cvref_t<TMemberResult>>();
        TValidator{}(src);
        const auto &dst_i = std::invoke(member, state.i);
        auto &dst_d = std::invoke(member, state.d);
        dst_d = src;

        if(dst_i != dst_d)
            mEaxDf.set(dirty_bit);
    }

    void eax_context_commit_property(auto &state, std::bitset<eax_dirty_bit_count> &dst_df,
        size_t dirty_bit, std::invocable<decltype(mEax)> auto member) noexcept
    {
        if(mEaxDf.test(dirty_bit))
        {
            dst_df.set(dirty_bit);
            const auto &src_d = std::invoke(member, state.d);
            std::invoke(member, state.i) = src_d;
            std::invoke(member, mEax) = src_d;
        }
    }

    void eax_initialize_extensions();
    void eax_initialize();

    bool eax_has_no_default_effect_slot() const noexcept;
    void eax_ensure_no_default_effect_slot() const;
    bool eax_has_enough_aux_sends() const noexcept;
    void eax_ensure_enough_aux_sends() const;
    void eax_ensure_compatibility() const;

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
    void eax_context_commit_air_absorption_hf();
    static void eax_context_commit_hf_reference();
    static void eax_context_commit_macro_fx_factor();

    void eax_initialize_fx_slots();

    void eax_update_sources();

    void eax_set_misc(const EaxCall& call);
    void eax4_defer_all(const EaxCall& call, Eax4State& state);
    void eax4_defer(const EaxCall& call, Eax4State& state);
    void eax5_defer_all(const EaxCall& call, Eax5State& state);
    void eax5_defer(const EaxCall& call, Eax5State& state);
    void eax_set(const EaxCall& call);

    void eax4_context_commit(Eax4State& state, std::bitset<eax_dirty_bit_count>& dst_df);
    void eax5_context_commit(Eax5State& state, std::bitset<eax_dirty_bit_count>& dst_df);
    void eax_context_commit();
#endif // ALSOFT_EAX
};

} // namespace al

using ContextRef = al::intrusive_ptr<al::Context>;

ContextRef GetContextRef() noexcept;

void UpdateContextProps(al::Context *context);


inline bool TrapALError{false};


#if ALSOFT_EAX
auto AL_APIENTRY EAXSet(const GUID *property_set_id, ALuint property_id,
    ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum;

auto AL_APIENTRY EAXGet(const GUID *property_set_id, ALuint property_id,
    ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum;
#endif // ALSOFT_EAX

#endif /* ALC_CONTEXT_H */
