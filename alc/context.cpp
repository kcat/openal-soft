
#include "config.h"

#include "context.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <functional>
#include <numeric>
#include <optional>
#include <string_view>
#include <utility>

#include "AL/efx.h"

#include "al/auxeffectslot.h"
#include "al/debug.h"
#include "al/source.h"
#include "al/effect.h"
#include "al/event.h"
#include "al/listener.h"
#include "alc/alu.h"
#include "alc/backends/base.h"
#include "alnumeric.h"
#include "atomic.h"
#include "core/async_event.h"
#include "core/devformat.h"
#include "core/device.h"
#include "core/effectslot.h"
#include "core/logging.h"
#include "core/voice_change.h"
#include "device.h"
#include "flexarray.h"
#include "fmt/format.h"
#include "fmt/ranges.h"
#include "gsl/gsl"
#include "ringbuffer.h"
#include "vecmat.h"

#if ALSOFT_EAX
#include "al/eax/call.h"
#include "al/eax/globals.h"
#endif // ALSOFT_EAX

namespace {

using namespace std::string_view_literals;
using voidp = void*;

/* Default context extensions */
auto getContextExtensions() noexcept -> std::vector<std::string_view>
{
    return std::vector({
        "AL_EXT_ALAW"sv,
        "AL_EXT_BFORMAT"sv,
        "AL_EXT_debug"sv,
        "AL_EXT_direct_context"sv,
        "AL_EXT_DOUBLE"sv,
        "AL_EXT_EXPONENT_DISTANCE"sv,
        "AL_EXT_FLOAT32"sv,
        "AL_EXT_IMA4"sv,
        "AL_EXT_LINEAR_DISTANCE"sv,
        "AL_EXT_MCFORMATS"sv,
        "AL_EXT_MULAW"sv,
        "AL_EXT_MULAW_BFORMAT"sv,
        "AL_EXT_MULAW_MCFORMATS"sv,
        "AL_EXT_OFFSET"sv,
        "AL_EXT_source_distance_model"sv,
        "AL_EXT_SOURCE_RADIUS"sv,
        "AL_EXT_STATIC_BUFFER"sv,
        "AL_EXT_STEREO_ANGLES"sv,
        "AL_LOKI_quadriphonic"sv,
        "AL_SOFT_bformat_ex"sv,
        "AL_SOFT_bformat_hoa"sv,
        "AL_SOFT_block_alignment"sv,
        "AL_SOFT_buffer_length_query"sv,
        "AL_SOFT_callback_buffer"sv,
        "AL_SOFTX_convolution_effect"sv,
        "AL_SOFT_deferred_updates"sv,
        "AL_SOFT_direct_channels"sv,
        "AL_SOFT_direct_channels_remix"sv,
        "AL_SOFT_effect_target"sv,
        "AL_SOFT_events"sv,
        "AL_SOFT_gain_clamp_ex"sv,
        "AL_SOFTX_hold_on_disconnect"sv,
        "AL_SOFT_loop_points"sv,
        "AL_SOFTX_map_buffer"sv,
        "AL_SOFT_MSADPCM"sv,
        "AL_SOFT_source_latency"sv,
        "AL_SOFT_source_length"sv,
        "AL_SOFTX_source_panning"sv,
        "AL_SOFT_source_resampler"sv,
        "AL_SOFT_source_spatialize"sv,
        "AL_SOFT_source_start_delay"sv,
        "AL_SOFT_UHJ"sv,
        "AL_SOFT_UHJ_ex"sv,
    });
}

} // namespace


namespace al {

std::atomic<bool> Context::sGlobalContextLock{false};
std::atomic<Context*> Context::sGlobalContext{nullptr};

Context::ThreadCtx::~ThreadCtx()
{
    if(auto *ctx = std::exchange(sLocalContext, nullptr))
    {
        const auto result = ctx->releaseIfNoDelete();
        ERR("Context {} current for thread being destroyed{}!", voidp{ctx},
            result ? "" : ", leak detected");
    }
}
thread_local Context::ThreadCtx Context::sThreadContext;

Effect Context::sDefaultEffect;


void ContextDeleter::operator()(gsl::owner<Context*> const context) const noexcept
{ delete context; }

auto Context::Create(const gsl::not_null<intrusive_ptr<Device>> &device,
    ContextFlagBitset const flags) -> intrusive_ptr<Context>
{
    auto ret = ContextRef{new Context{device, flags}};
    ret->init();
    return ret;
}


Context::Context(gsl::not_null<intrusive_ptr<Device>> const &device, ContextFlagBitset const flags)
    : ContextBase{get_not_null(device)}, mALDevice{device}, mContextFlags{flags}
    , mDebugEnabled{flags.test(ContextFlags::DebugBit)}
    , mDebugGroups{{DebugSource::Other, 0, std::string{}}}
{
    /* Low-severity debug messages are disabled by default. */
    alDebugMessageControlDirectEXT(this, AL_DONT_CARE_EXT, AL_DONT_CARE_EXT,
        AL_DEBUG_SEVERITY_LOW_EXT, 0, nullptr, AL_FALSE);
}

Context::~Context()
{
    TRACE("Freeing context {}", voidp{this});
    deinit();

    auto count = std::accumulate(mSourceList.cbegin(), mSourceList.cend(), 0_uz,
        [](usize const cur, SourceSubList const &sublist) noexcept -> size_t
    { return cur + gsl::narrow_cast<unsigned>(std::popcount(~sublist.mFreeMask)); });
    if(count > 0)
        WARN("{} Source{} not deleted", count, (count==1)?"":"s");
    mSourceList.clear();
    mNumSources = 0;

#if ALSOFT_EAX
    eaxUninitialize();
#endif // ALSOFT_EAX

    mDefaultSlot = nullptr;
    count = std::accumulate(mEffectSlotList.cbegin(), mEffectSlotList.cend(), 0_uz,
        [](size_t cur, const EffectSlotSubList &sublist) noexcept -> size_t
    { return cur + gsl::narrow_cast<unsigned>(std::popcount(~sublist.mFreeMask)); });
    if(count > 0)
        WARN("{} AuxiliaryEffectSlot{} not deleted", count, (count==1)?"":"s");
    mEffectSlotList.clear();
    mNumEffectSlots = 0;
}

void Context::init()
{
    if(sDefaultEffect.mType != AL_EFFECT_NULL && mDevice->Type == DeviceType::Playback)
    {
        mDefaultSlot = std::make_unique<EffectSlot>(gsl::make_not_null(this));
        aluInitEffectPanning(mDefaultSlot->mSlot, this);
    }

    auto auxslots = std::unique_ptr<EffectSlotArray>{};
    if(!mDefaultSlot)
        auxslots = EffectSlotBase::CreatePtrArray(0);
    else
    {
        auxslots = EffectSlotBase::CreatePtrArray(2);
        (*auxslots)[0] = mDefaultSlot->mSlot;
        (*auxslots)[1] = mDefaultSlot->mSlot;
        mDefaultSlot->mState = SlotState::Playing;
    }
    mActiveAuxSlots.store(std::move(auxslots), std::memory_order_relaxed);

    allocVoiceChanges();
    {
        auto *cur = mVoiceChangeTail;
        while(auto *next = cur->mNext.load(std::memory_order_relaxed))
            cur = next;
        mCurrentVoiceChange.store(cur, std::memory_order_relaxed);
    }

    mExtensions = getContextExtensions();

    if(sBufferSubDataCompat)
    {
        auto iter = std::ranges::find(mExtensions, "AL_EXT_SOURCE_RADIUS"sv);
        if(iter != mExtensions.end()) mExtensions.erase(iter);

        /* Insert the AL_SOFT_buffer_sub_data extension string between
         * AL_SOFT_buffer_length_query and AL_SOFT_callback_buffer.
         */
        iter = std::ranges::find(mExtensions, "AL_SOFT_callback_buffer"sv);
        mExtensions.emplace(iter, "AL_SOFT_buffer_sub_data"sv);
    }

#if ALSOFT_EAX
    eax_initialize_extensions();
#endif // ALSOFT_EAX

    mExtensionsString = fmt::format("{}", fmt::join(mExtensions, " "));

#if ALSOFT_EAX
    eax_set_defaults();
#endif

    mParams.Position = al::Vector{0.0f, 0.0f, 0.0f, 1.0f};
    mParams.Matrix = al::Matrix::Identity();
    mParams.Velocity = al::Vector{};
    mParams.Gain = mListener.mGain;
    mParams.MetersPerUnit = mListener.mMetersPerUnit
#if ALSOFT_EAX
        * eaxGetDistanceFactor()
#endif
        ;
    mParams.AirAbsorptionGainHF = mAirAbsorptionGainHF;
    mParams.DopplerFactor = mDopplerFactor;
    mParams.SpeedOfSound = mSpeedOfSound * mDopplerVelocity
#if ALSOFT_EAX
        / eaxGetDistanceFactor()
#endif
        ;
    mParams.SourceDistanceModel = mSourceDistanceModel;
    mParams.mDistanceModel = mDistanceModel;


    mAsyncEvents = FifoBuffer<AsyncEvent>::Create(1024, false);
    StartEventThrd(this);


    allocVoices(256);
    mActiveVoiceCount.store(64, std::memory_order_relaxed);
}

void Context::deinit()
{
    if(sLocalContext == this)
    {
        WARN("{} released while current on thread", voidp{this});
        auto _ = ContextRef{sLocalContext};
        sThreadContext.set(nullptr);
    }

    if(auto *origctx = this; sGlobalContext.compare_exchange_strong(origctx, nullptr))
    {
        auto _ = ContextRef{origctx};
        while(sGlobalContextLock.load()) {
            /* Wait to make sure another thread didn't get the context and is
             * trying to increment its refcount.
             */
        }
    }

    StopEventThrd(this);
}

void Context::applyAllUpdates()
{
    /* Tell the mixer to stop applying updates, then wait for any active
     * updating to finish, before providing updates.
     */
    mHoldUpdates.store(true, std::memory_order_release);
    while((mUpdateCount.load(std::memory_order_acquire)&1) != 0) {
        /* busy-wait */
    }

#if ALSOFT_EAX
    if(mEaxNeedsCommit)
        eaxCommit();
#endif

    if(std::exchange(mPropsDirty, false))
        UpdateContextProps(this);
    UpdateAllEffectSlotProps(gsl::make_not_null(this));
    UpdateAllSourceProps(gsl::make_not_null(this));

    /* Now with all updates declared, let the mixer continue applying them so
     * they all happen at once.
     */
    mHoldUpdates.store(false, std::memory_order_release);
}

} // namespace al

#if ALSOFT_EAX
namespace {

void ForEachSource(al::Context *context, std::invocable<al::Source&> auto&& func)
{
    std::ranges::for_each(context->mSourceList, [&func](SourceSubList &sublist)
    {
        auto usemask = ~sublist.mFreeMask;
        while(usemask)
        {
            const auto idx = as_unsigned(std::countr_zero(usemask));
            usemask ^= 1_u64 << idx;

            std::invoke(func, (*sublist.mSources)[idx]);
        }
    });
}

} // namespace

namespace al {

auto Context::eaxIsCapable() const noexcept -> bool
{
    return eax_has_enough_aux_sends();
}

void Context::eaxUninitialize() noexcept
{
    if(!mEaxIsInitialized)
        return;

    mEaxIsInitialized = false;
    mEaxIsTried = false;
    mEaxFxSlots.uninitialize();
}

auto Context::eax_eax_set(const GUID *property_set_id, ALuint property_id,
    ALuint property_source_id, ALvoid *property_value, ALuint property_value_size) -> ALenum
{
    const auto call = create_eax_call(EaxCallType::set, property_set_id, property_id,
        property_source_id, property_value, property_value_size);

    eax_initialize();

    switch(call.get_property_set_id())
    {
    case EaxCallPropertySetId::context:
        eax_set(call);
        break;
    case EaxCallPropertySetId::fx_slot:
    case EaxCallPropertySetId::fx_slot_effect:
        eax_dispatch_fx_slot(call);
        break;
    case EaxCallPropertySetId::source:
        eax_dispatch_source(call);
        break;
    default:
        eax_fail_unknown_property_set_id();
    }
    mEaxNeedsCommit = true;

    if(!call.is_deferred())
    {
        eaxCommit();
        if(!mDeferUpdates)
            applyAllUpdates();
    }

    return AL_NO_ERROR;
}

auto Context::eax_eax_get(const GUID* property_set_id, ALuint property_id,
    ALuint property_source_id, ALvoid *property_value, ALuint property_value_size) -> ALenum
{
    const auto call = create_eax_call(EaxCallType::get, property_set_id, property_id,
        property_source_id, property_value, property_value_size);

    eax_initialize();

    switch(call.get_property_set_id())
    {
    case EaxCallPropertySetId::context:
        eax_get(call);
        break;
    case EaxCallPropertySetId::fx_slot:
    case EaxCallPropertySetId::fx_slot_effect:
        eax_dispatch_fx_slot(call);
        break;
    case EaxCallPropertySetId::source:
        eax_dispatch_source(call);
        break;
    default:
        eax_fail_unknown_property_set_id();
    }

    return AL_NO_ERROR;
}

void Context::eaxSetLastError() noexcept
{
    mEaxLastError = EAXERR_INVALID_OPERATION;
}

[[noreturn]]
void Context::eax_fail(const std::string_view message) { throw ContextException{message}; }

[[noreturn]]
void Context::eax_fail_unknown_property_set_id() { eax_fail("Unknown property ID."); }

[[noreturn]]
void Context::eax_fail_unknown_primary_fx_slot_id()
{ eax_fail("Unknown primary FX Slot ID."); }

[[noreturn]] void Context::eax_fail_unknown_property_id() { eax_fail("Unknown property ID."); }

[[noreturn]] void Context::eax_fail_unknown_version() { eax_fail("Unknown version."); }

void Context::eax_initialize_extensions()
{
    if(!eax_g_is_enabled)
        return;

    mExtensions.emplace(mExtensions.begin(), "EAX-RAM"sv);
    if(eaxIsCapable())
    {
        mExtensions.emplace(mExtensions.begin(), "EAX5.0"sv);
        mExtensions.emplace(mExtensions.begin(), "EAX4.0"sv);
        mExtensions.emplace(mExtensions.begin(), "EAX3.0"sv);
        mExtensions.emplace(mExtensions.begin(), "EAX2.0"sv);
        mExtensions.emplace(mExtensions.begin(), "EAX"sv);
    }
}

void Context::eax_initialize()
{
    if(mEaxIsInitialized)
        return;

    if(mEaxIsTried)
        eax_fail("No EAX.");

    mEaxIsTried = true;

    if(!eax_g_is_enabled)
        eax_fail("EAX disabled by a configuration.");

    eax_ensure_compatibility();
    eax_set_defaults();
    eax_context_commit_air_absorption_hf();
    eax_update_speaker_configuration();
    eax_initialize_fx_slots();

    mEaxIsInitialized = true;
}

auto Context::eax_has_no_default_effect_slot() const noexcept -> bool
{
    return mDefaultSlot == nullptr;
}

void Context::eax_ensure_no_default_effect_slot() const
{
    if(!eax_has_no_default_effect_slot())
        eax_fail("There is a default effect slot in the context.");
}

auto Context::eax_has_enough_aux_sends() const noexcept -> bool
{
    return mALDevice->NumAuxSends >= EAX_MAX_FXSLOTS;
}

void Context::eax_ensure_enough_aux_sends() const
{
    if(!eax_has_enough_aux_sends())
        eax_fail("Not enough aux sends.");
}

void Context::eax_ensure_compatibility() const
{
    eax_ensure_enough_aux_sends();
}

auto Context::eax_detect_speaker_configuration() const -> eax_ulong
{
#define EAX_PREFIX "[EAX_DETECT_SPEAKER_CONFIG]"

    switch(mDevice->FmtChans)
    {
    case DevFmtMono: return SPEAKERS_2;
    case DevFmtStereo:
        /* Pretend 7.1 if using UHJ output, since they both provide full
         * horizontal surround.
         */
        if(std::holds_alternative<UhjPostProcess>(mDevice->mPostProcess))
            return SPEAKERS_7;
        if(mDevice->Flags.test(DirectEar))
            return HEADPHONES;
        return SPEAKERS_2;
    case DevFmtQuad: return SPEAKERS_4;
    case DevFmtX51: return SPEAKERS_5;
    case DevFmtX61: return SPEAKERS_6;
    case DevFmtX71: return SPEAKERS_7;
    /* 7.1.4(.4) is compatible with 7.1. This could instead be HEADPHONES to
     * suggest with-height surround sound (like HRTF).
     */
    case DevFmtX714: return SPEAKERS_7;
    case DevFmtX7144: return SPEAKERS_7;
    /* 3D7.1 is only compatible with 5.1. This could instead be HEADPHONES to
     * suggest full-sphere surround sound (like HRTF).
     */
    case DevFmtX3D71: return SPEAKERS_5;
    /* This could also be HEADPHONES, since headphones-based HRTF and Ambi3D
     * provide full-sphere surround sound. Depends if apps are more likely to
     * consider headphones or 7.1 for surround sound support.
     */
    case DevFmtAmbi3D: return SPEAKERS_7;
    }
    ERR(EAX_PREFIX "Unexpected device channel format {:#x}.",
        unsigned{al::to_underlying(mDevice->FmtChans)});
    return HEADPHONES;

#undef EAX_PREFIX
}

void Context::eax_update_speaker_configuration()
{
    mEaxSpeakerConfig = eax_detect_speaker_configuration();
}

void Context::eax_set_last_error_defaults() noexcept
{
    mEaxLastError = EAXCONTEXT_DEFAULTLASTERROR;
}

void Context::eax_session_set_defaults() noexcept
{
    mEaxSession.ulEAXVersion = EAXCONTEXT_DEFAULTEAXSESSION;
    mEaxSession.ulMaxActiveSends = EAXCONTEXT_DEFAULTMAXACTIVESENDS;
}

void Context::eax4_context_set_defaults(Eax4Props& props) noexcept
{
    props.guidPrimaryFXSlotID = EAX40CONTEXT_DEFAULTPRIMARYFXSLOTID;
    props.flDistanceFactor = EAXCONTEXT_DEFAULTDISTANCEFACTOR;
    props.flAirAbsorptionHF = EAXCONTEXT_DEFAULTAIRABSORPTIONHF;
    props.flHFReference = EAXCONTEXT_DEFAULTHFREFERENCE;
}

void Context::eax4_context_set_defaults(Eax4State& state) noexcept
{
    eax4_context_set_defaults(state.i);
    state.d = state.i;
}

void Context::eax5_context_set_defaults(Eax5Props& props) noexcept
{
    props.guidPrimaryFXSlotID = EAX50CONTEXT_DEFAULTPRIMARYFXSLOTID;
    props.flDistanceFactor = EAXCONTEXT_DEFAULTDISTANCEFACTOR;
    props.flAirAbsorptionHF = EAXCONTEXT_DEFAULTAIRABSORPTIONHF;
    props.flHFReference = EAXCONTEXT_DEFAULTHFREFERENCE;
    props.flMacroFXFactor = EAXCONTEXT_DEFAULTMACROFXFACTOR;
}

void Context::eax5_context_set_defaults(Eax5State& state) noexcept
{
    eax5_context_set_defaults(state.i);
    state.d = state.i;
}

void Context::eax_context_set_defaults()
{
    eax5_context_set_defaults(mEax123);
    eax4_context_set_defaults(mEax4);
    eax5_context_set_defaults(mEax5);
    mEax = mEax5.i;
    mEaxVersion = 5;
    mEaxDf.reset();
}

void Context::eax_set_defaults()
{
    eax_set_last_error_defaults();
    eax_session_set_defaults();
    eax_context_set_defaults();
}

void Context::eax_dispatch_fx_slot(const EaxCall& call)
{
    const auto fx_slot_index = call.get_fx_slot_index();
    if(!fx_slot_index.has_value())
        eax_fail("Invalid fx slot index.");

    auto& fx_slot = eaxGetFxSlot(*fx_slot_index);
    if(fx_slot.eax_dispatch(call))
    {
        const auto srclock = std::lock_guard{mSourceLock};
        ForEachSource(this, &Source::eaxMarkAsChanged);
    }
}

void Context::eax_dispatch_source(const EaxCall& call)
{
    const auto source_id = call.get_property_al_name();
    const auto srclock = std::lock_guard{mSourceLock};

    const auto source = Source::EaxLookupSource(gsl::make_not_null(this), source_id);
    if(source == nullptr)
        eax_fail("Source not found.");

    source->eaxDispatch(call);
}

void Context::eax_get_misc(const EaxCall& call)
{
    switch(call.get_property_id())
    {
    case EAXCONTEXT_NONE: break;
    case EAXCONTEXT_LASTERROR: call.store(std::exchange(mEaxLastError, EAX_OK)); break;
    case EAXCONTEXT_SPEAKERCONFIG: call.store(mEaxSpeakerConfig); break;
    case EAXCONTEXT_EAXSESSION: call.store(mEaxSession); break;
    default: eax_fail_unknown_property_id();
    }
}

void Context::eax4_get(const EaxCall& call, const Eax4Props& props)
{
    switch(call.get_property_id())
    {
    case EAXCONTEXT_ALLPARAMETERS: call.store(props); break;
    case EAXCONTEXT_PRIMARYFXSLOTID: call.store(props.guidPrimaryFXSlotID); break;
    case EAXCONTEXT_DISTANCEFACTOR: call.store(props.flDistanceFactor); break;
    case EAXCONTEXT_AIRABSORPTIONHF: call.store(props.flAirAbsorptionHF); break;
    case EAXCONTEXT_HFREFERENCE: call.store(props.flHFReference); break;
    default: eax_get_misc(call); break;
    }
}

void Context::eax5_get(const EaxCall& call, const Eax5Props& props)
{
    switch(call.get_property_id())
    {
    case EAXCONTEXT_ALLPARAMETERS: call.store(props); break;
    case EAXCONTEXT_PRIMARYFXSLOTID: call.store(props.guidPrimaryFXSlotID); break;
    case EAXCONTEXT_DISTANCEFACTOR: call.store(props.flDistanceFactor); break;
    case EAXCONTEXT_AIRABSORPTIONHF: call.store(props.flAirAbsorptionHF); break;
    case EAXCONTEXT_HFREFERENCE: call.store(props.flHFReference); break;
    case EAXCONTEXT_MACROFXFACTOR: call.store(props.flMacroFXFactor); break;
    default: eax_get_misc(call); break;
    }
}

void Context::eax_get(const EaxCall& call)
{
    switch(call.get_version())
    {
    case 4: eax4_get(call, mEax4.i); break;
    case 5: eax5_get(call, mEax5.i); break;
    default: eax_fail_unknown_version();
    }
}

void Context::eax_context_commit_primary_fx_slot_id()
{
    mEaxPrimaryFxSlotIndex = mEax.guidPrimaryFXSlotID;
}

void Context::eax_context_commit_distance_factor()
{
    /* mEax.flDistanceFactor was changed, so the context props are dirty. */
    mPropsDirty = true;
}

void Context::eax_context_commit_air_absorption_hf()
{
    const auto new_value = level_mb_to_gain(mEax.flAirAbsorptionHF);

    if(mAirAbsorptionGainHF == new_value)
        return;

    mAirAbsorptionGainHF = new_value;
    mPropsDirty = true;
}

void Context::eax_context_commit_hf_reference()
{
    // TODO
}

void Context::eax_context_commit_macro_fx_factor()
{
    // TODO
}

void Context::eax_initialize_fx_slots()
{
    mEaxFxSlots.initialize(gsl::make_not_null(this));
    mEaxPrimaryFxSlotIndex = mEax.guidPrimaryFXSlotID;
}

void Context::eax_update_sources()
{
    const auto srclock = std::lock_guard{mSourceLock};
    ForEachSource(this, &Source::eaxCommit);
}

void Context::eax_set_misc(const EaxCall& call)
{
    switch(call.get_property_id())
    {
    case EAXCONTEXT_NONE:
        break;
    case EAXCONTEXT_SPEAKERCONFIG:
        eax_set<Eax5SpeakerConfigValidator>(call, mEaxSpeakerConfig);
        break;
    case EAXCONTEXT_EAXSESSION:
        eax_set<Eax5SessionAllValidator>(call, mEaxSession);
        break;
    default:
        eax_fail_unknown_property_id();
    }
}

void Context::eax4_defer_all(const EaxCall& call, Eax4State& state)
{
    const auto &src = call.load<const EAX40CONTEXTPROPERTIES>();
    Eax4AllValidator{}(src);
    const auto &dst_i = state.i;
    auto &dst_d = state.d;
    dst_d = src;

    if(dst_i.guidPrimaryFXSlotID != dst_d.guidPrimaryFXSlotID)
        mEaxDf.set(eax_primary_fx_slot_id_dirty_bit);

    if(dst_i.flDistanceFactor != dst_d.flDistanceFactor)
        mEaxDf.set(eax_distance_factor_dirty_bit);

    if(dst_i.flAirAbsorptionHF != dst_d.flAirAbsorptionHF)
        mEaxDf.set(eax_air_absorption_hf_dirty_bit);

    if(dst_i.flHFReference != dst_d.flHFReference)
        mEaxDf.set(eax_hf_reference_dirty_bit);
}

void Context::eax4_defer(const EaxCall& call, Eax4State& state)
{
    switch(call.get_property_id())
    {
    case EAXCONTEXT_ALLPARAMETERS:
        eax4_defer_all(call, state);
        break;
    case EAXCONTEXT_PRIMARYFXSLOTID:
        eax_defer<Eax4PrimaryFxSlotIdValidator>(call, state, eax_primary_fx_slot_id_dirty_bit,
            &EAX40CONTEXTPROPERTIES::guidPrimaryFXSlotID);
        break;
    case EAXCONTEXT_DISTANCEFACTOR:
        eax_defer<Eax4DistanceFactorValidator>(call, state, eax_distance_factor_dirty_bit,
            &EAX40CONTEXTPROPERTIES::flDistanceFactor);
        break;
    case EAXCONTEXT_AIRABSORPTIONHF:
        eax_defer<Eax4AirAbsorptionHfValidator>(call, state, eax_air_absorption_hf_dirty_bit,
            &EAX40CONTEXTPROPERTIES::flAirAbsorptionHF);
        break;
    case EAXCONTEXT_HFREFERENCE:
        eax_defer<Eax4HfReferenceValidator>(call, state, eax_hf_reference_dirty_bit,
            &EAX40CONTEXTPROPERTIES::flHFReference);
        break;
    default:
        eax_set_misc(call);
        break;
    }
}

void Context::eax5_defer_all(const EaxCall& call, Eax5State& state)
{
    const auto &src = call.load<const EAX50CONTEXTPROPERTIES>();
    Eax4AllValidator{}(src);
    const auto &dst_i = state.i;
    auto &dst_d = state.d;
    dst_d = src;

    if(dst_i.guidPrimaryFXSlotID != dst_d.guidPrimaryFXSlotID)
        mEaxDf.set(eax_primary_fx_slot_id_dirty_bit);

    if(dst_i.flDistanceFactor != dst_d.flDistanceFactor)
        mEaxDf.set(eax_distance_factor_dirty_bit);

    if(dst_i.flAirAbsorptionHF != dst_d.flAirAbsorptionHF)
        mEaxDf.set(eax_air_absorption_hf_dirty_bit);

    if(dst_i.flHFReference != dst_d.flHFReference)
        mEaxDf.set(eax_hf_reference_dirty_bit);

    if(dst_i.flMacroFXFactor != dst_d.flMacroFXFactor)
        mEaxDf.set(eax_macro_fx_factor_dirty_bit);
}

void Context::eax5_defer(const EaxCall& call, Eax5State& state)
{
    switch(call.get_property_id())
    {
    case EAXCONTEXT_ALLPARAMETERS:
        eax5_defer_all(call, state);
        break;
    case EAXCONTEXT_PRIMARYFXSLOTID:
        eax_defer<Eax5PrimaryFxSlotIdValidator>(call, state, eax_primary_fx_slot_id_dirty_bit,
            &EAX50CONTEXTPROPERTIES::guidPrimaryFXSlotID);
        break;
    case EAXCONTEXT_DISTANCEFACTOR:
        eax_defer<Eax4DistanceFactorValidator>(call, state, eax_distance_factor_dirty_bit,
            &EAX50CONTEXTPROPERTIES::flDistanceFactor);
        break;
    case EAXCONTEXT_AIRABSORPTIONHF:
        eax_defer<Eax4AirAbsorptionHfValidator>(call, state, eax_air_absorption_hf_dirty_bit,
            &EAX50CONTEXTPROPERTIES::flAirAbsorptionHF);
        break;
    case EAXCONTEXT_HFREFERENCE:
        eax_defer<Eax4HfReferenceValidator>(call, state, eax_hf_reference_dirty_bit,
            &EAX50CONTEXTPROPERTIES::flHFReference);
        break;
    case EAXCONTEXT_MACROFXFACTOR:
        eax_defer<Eax5MacroFxFactorValidator>(call, state, eax_macro_fx_factor_dirty_bit,
            &EAX50CONTEXTPROPERTIES::flMacroFXFactor);
        break;
    default:
        eax_set_misc(call);
        break;
    }
}

void Context::eax_set(const EaxCall& call)
{
    const auto version = call.get_version();
    switch(version)
    {
    case 4: eax4_defer(call, mEax4); break;
    case 5: eax5_defer(call, mEax5); break;
    default: eax_fail_unknown_version();
    }
    if(version != mEaxVersion)
        mEaxDf.set();
    mEaxVersion = version;
}

void Context::eax4_context_commit(Eax4State& state, std::bitset<eax_dirty_bit_count>& dst_df)
{
    if(mEaxDf.none())
        return;

    eax_context_commit_property(state, dst_df, eax_primary_fx_slot_id_dirty_bit,
        &EAX40CONTEXTPROPERTIES::guidPrimaryFXSlotID);
    eax_context_commit_property(state, dst_df, eax_distance_factor_dirty_bit,
        &EAX40CONTEXTPROPERTIES::flDistanceFactor);
    eax_context_commit_property(state, dst_df, eax_air_absorption_hf_dirty_bit,
        &EAX40CONTEXTPROPERTIES::flAirAbsorptionHF);
    eax_context_commit_property(state, dst_df, eax_hf_reference_dirty_bit,
        &EAX40CONTEXTPROPERTIES::flHFReference);

    mEaxDf.reset();
}

void Context::eax5_context_commit(Eax5State &state, std::bitset<eax_dirty_bit_count> &dst_df)
{
    if(mEaxDf.none())
        return;

    eax_context_commit_property(state, dst_df, eax_primary_fx_slot_id_dirty_bit,
        &EAX50CONTEXTPROPERTIES::guidPrimaryFXSlotID);
    eax_context_commit_property(state, dst_df, eax_distance_factor_dirty_bit,
        &EAX50CONTEXTPROPERTIES::flDistanceFactor);
    eax_context_commit_property(state, dst_df, eax_air_absorption_hf_dirty_bit,
        &EAX50CONTEXTPROPERTIES::flAirAbsorptionHF);
    eax_context_commit_property(state, dst_df, eax_hf_reference_dirty_bit,
        &EAX50CONTEXTPROPERTIES::flHFReference);
    eax_context_commit_property(state, dst_df, eax_macro_fx_factor_dirty_bit,
        &EAX50CONTEXTPROPERTIES::flMacroFXFactor);

    mEaxDf.reset();
}

void Context::eax_context_commit()
{
    auto dst_df = std::bitset<eax_dirty_bit_count>{};

    switch(mEaxVersion)
    {
    case 1:
    case 2:
    case 3:
        eax5_context_commit(mEax123, dst_df);
        break;
    case 4:
        eax4_context_commit(mEax4, dst_df);
        break;
    case 5:
        eax5_context_commit(mEax5, dst_df);
        break;
    }

    if(dst_df.none())
        return;

    if(dst_df.test(eax_primary_fx_slot_id_dirty_bit))
        eax_context_commit_primary_fx_slot_id();

    if(dst_df.test(eax_distance_factor_dirty_bit))
        eax_context_commit_distance_factor();

    if(dst_df.test(eax_air_absorption_hf_dirty_bit))
        eax_context_commit_air_absorption_hf();

    if(dst_df.test(eax_hf_reference_dirty_bit))
        eax_context_commit_hf_reference();

    if(dst_df.test(eax_macro_fx_factor_dirty_bit))
        eax_context_commit_macro_fx_factor();

    if(dst_df.test(eax_primary_fx_slot_id_dirty_bit))
        eax_update_sources();
}

void Context::eaxCommit()
{
    mEaxNeedsCommit = false;
    eax_context_commit();
    eaxCommitFxSlots();
    eax_update_sources();
}

} // namespace al

#endif // ALSOFT_EAX
