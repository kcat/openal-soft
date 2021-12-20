
#include "config.h"

#include "context.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>
#include <stddef.h>
#include <stdexcept>

#include "AL/efx.h"

#include "al/auxeffectslot.h"
#include "al/source.h"
#include "al/effect.h"
#include "al/event.h"
#include "al/listener.h"
#include "albit.h"
#include "alc/alu.h"
#include "core/async_event.h"
#include "core/device.h"
#include "core/effectslot.h"
#include "core/logging.h"
#include "core/voice.h"
#include "core/voice_change.h"
#include "device.h"
#include "ringbuffer.h"
#include "vecmat.h"


namespace {

using namespace std::placeholders;

using voidp = void*;

/* Default context extensions */
constexpr ALchar alExtList[] =
    "AL_EXT_ALAW "
    "AL_EXT_BFORMAT "
    "AL_EXT_DOUBLE "
    "AL_EXT_EXPONENT_DISTANCE "
    "AL_EXT_FLOAT32 "
    "AL_EXT_IMA4 "
    "AL_EXT_LINEAR_DISTANCE "
    "AL_EXT_MCFORMATS "
    "AL_EXT_MULAW "
    "AL_EXT_MULAW_BFORMAT "
    "AL_EXT_MULAW_MCFORMATS "
    "AL_EXT_OFFSET "
    "AL_EXT_source_distance_model "
    "AL_EXT_SOURCE_RADIUS "
    "AL_EXT_STEREO_ANGLES "
    "AL_LOKI_quadriphonic "
    "AL_SOFT_bformat_ex "
    "AL_SOFTX_bformat_hoa "
    "AL_SOFT_block_alignment "
    "AL_SOFTX_callback_buffer "
    "AL_SOFTX_convolution_reverb "
    "AL_SOFT_deferred_updates "
    "AL_SOFT_direct_channels "
    "AL_SOFT_direct_channels_remix "
    "AL_SOFT_effect_target "
    "AL_SOFT_events "
    "AL_SOFTX_filter_gain_ex "
    "AL_SOFT_gain_clamp_ex "
    "AL_SOFTX_hold_on_disconnect "
    "AL_SOFT_loop_points "
    "AL_SOFTX_map_buffer "
    "AL_SOFT_MSADPCM "
    "AL_SOFT_source_latency "
    "AL_SOFT_source_length "
    "AL_SOFT_source_resampler "
    "AL_SOFT_source_spatialize "
    "AL_SOFTX_UHJ";

} // namespace


std::atomic<ALCcontext*> ALCcontext::sGlobalContext{nullptr};

thread_local ALCcontext *ALCcontext::sLocalContext{nullptr};
ALCcontext::ThreadCtx::~ThreadCtx()
{
    if(ALCcontext *ctx{ALCcontext::sLocalContext})
    {
        const bool result{ctx->releaseIfNoDelete()};
        ERR("Context %p current for thread being destroyed%s!\n", voidp{ctx},
            result ? "" : ", leak detected");
    }
}
thread_local ALCcontext::ThreadCtx ALCcontext::sThreadContext;

ALeffect ALCcontext::sDefaultEffect;


#ifdef __MINGW32__
ALCcontext *ALCcontext::getThreadContext() noexcept
{ return sLocalContext; }
void ALCcontext::setThreadContext(ALCcontext *context) noexcept
{ sThreadContext.set(context); }
#endif

ALCcontext::ALCcontext(al::intrusive_ptr<ALCdevice> device)
  : ContextBase{device.get()}, mALDevice{std::move(device)}
{
    mPropsDirty.test_and_clear(std::memory_order_relaxed);
}

ALCcontext::~ALCcontext()
{
    TRACE("Freeing context %p\n", voidp{this});

    size_t count{std::accumulate(mSourceList.cbegin(), mSourceList.cend(), size_t{0u},
        [](size_t cur, const SourceSubList &sublist) noexcept -> size_t
        { return cur + static_cast<uint>(al::popcount(~sublist.FreeMask)); })};
    if(count > 0)
        WARN("%zu Source%s not deleted\n", count, (count==1)?"":"s");
    mSourceList.clear();
    mNumSources = 0;

    mDefaultSlot = nullptr;
    count = std::accumulate(mEffectSlotList.cbegin(), mEffectSlotList.cend(), size_t{0u},
        [](size_t cur, const EffectSlotSubList &sublist) noexcept -> size_t
        { return cur + static_cast<uint>(al::popcount(~sublist.FreeMask)); });
    if(count > 0)
        WARN("%zu AuxiliaryEffectSlot%s not deleted\n", count, (count==1)?"":"s");
    mEffectSlotList.clear();
    mNumEffectSlots = 0;
}

void ALCcontext::init()
{
    if(sDefaultEffect.type != AL_EFFECT_NULL && mDevice->Type == DeviceType::Playback)
    {
        mDefaultSlot = std::make_unique<ALeffectslot>();
        aluInitEffectPanning(&mDefaultSlot->mSlot, this);
    }

    EffectSlotArray *auxslots;
    if(!mDefaultSlot)
        auxslots = EffectSlot::CreatePtrArray(0);
    else
    {
        auxslots = EffectSlot::CreatePtrArray(1);
        (*auxslots)[0] = &mDefaultSlot->mSlot;
        mDefaultSlot->mState = SlotState::Playing;
    }
    mActiveAuxSlots.store(auxslots, std::memory_order_relaxed);

    allocVoiceChanges();
    {
        VoiceChange *cur{mVoiceChangeTail};
        while(VoiceChange *next{cur->mNext.load(std::memory_order_relaxed)})
            cur = next;
        mCurrentVoiceChange.store(cur, std::memory_order_relaxed);
    }

    mExtensionList = alExtList;


    mParams.Position = alu::Vector{0.0f, 0.0f, 0.0f, 1.0f};
    mParams.Matrix = alu::Matrix::Identity();
    mParams.Velocity = alu::Vector{};
    mParams.Gain = mListener.Gain;
    mParams.MetersPerUnit = mListener.mMetersPerUnit;
    mParams.DopplerFactor = mDopplerFactor;
    mParams.SpeedOfSound = mSpeedOfSound * mDopplerVelocity;
    mParams.SourceDistanceModel = mSourceDistanceModel;
    mParams.mDistanceModel = mDistanceModel;


    mAsyncEvents = RingBuffer::Create(511, sizeof(AsyncEvent), false);
    StartEventThrd(this);


    allocVoices(256);
    mActiveVoiceCount.store(64, std::memory_order_relaxed);
}

bool ALCcontext::deinit()
{
    if(sLocalContext == this)
    {
        WARN("%p released while current on thread\n", voidp{this});
        sThreadContext.set(nullptr);
        release();
    }

    ALCcontext *origctx{this};
    if(sGlobalContext.compare_exchange_strong(origctx, nullptr))
        release();

    bool ret{};
    /* First make sure this context exists in the device's list. */
    auto *oldarray = mDevice->mContexts.load(std::memory_order_acquire);
    if(auto toremove = static_cast<size_t>(std::count(oldarray->begin(), oldarray->end(), this)))
    {
        using ContextArray = al::FlexArray<ContextBase*>;
        auto alloc_ctx_array = [](const size_t count) -> ContextArray*
        {
            if(count == 0) return &DeviceBase::sEmptyContextArray;
            return ContextArray::Create(count).release();
        };
        auto *newarray = alloc_ctx_array(oldarray->size() - toremove);

        /* Copy the current/old context handles to the new array, excluding the
         * given context.
         */
        std::copy_if(oldarray->begin(), oldarray->end(), newarray->begin(),
            std::bind(std::not_equal_to<>{}, _1, this));

        /* Store the new context array in the device. Wait for any current mix
         * to finish before deleting the old array.
         */
        mDevice->mContexts.store(newarray);
        if(oldarray != &DeviceBase::sEmptyContextArray)
        {
            mDevice->waitForMix();
            delete oldarray;
        }

        ret = !newarray->empty();
    }
    else
        ret = !oldarray->empty();

    StopEventThrd(this);

    return ret;
}

void ALCcontext::processUpdates()
{
    std::lock_guard<std::mutex> _{mPropLock};
    if(mDeferUpdates.exchange(false, std::memory_order_acq_rel))
    {
        /* Tell the mixer to stop applying updates, then wait for any active
         * updating to finish, before providing updates.
         */
        mHoldUpdates.store(true, std::memory_order_release);
        while((mUpdateCount.load(std::memory_order_acquire)&1) != 0) {
            /* busy-wait */
        }

        if(mPropsDirty.test_and_clear(std::memory_order_acq_rel))
            UpdateContextProps(this);
        if(mListener.mPropsDirty.test_and_clear(std::memory_order_acq_rel))
            UpdateListenerProps(this);
        UpdateAllEffectSlotProps(this);
        UpdateAllSourceProps(this);

        /* Now with all updates declared, let the mixer continue applying them
         * so they all happen at once.
         */
        mHoldUpdates.store(false, std::memory_order_release);
    }
}
