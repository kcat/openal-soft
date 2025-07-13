/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "source.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx.h"

#include "alc/backends/base.h"
#include "alc/context.h"
#include "alc/device.h"
#include "alc/inprogext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "atomic.h"
#include "auxeffectslot.h"
#include "buffer.h"
#include "core/buffer_storage.h"
#include "core/except.h"
#include "core/logging.h"
#include "core/mixer/defs.h"
#include "core/voice_change.h"
#include "direct_defs.h"
#include "filter.h"
#include "flexarray.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"
#include "opthelpers.h"

#if ALSOFT_EAX
#include "eax/api.h"
#include "eax/call.h"
#include "eax/fx_slot_index.h"
#include "eax/utils.h"
#endif

using uint = unsigned int;

namespace {

using SubListAllocator = al::allocator<std::array<ALsource,64>>;
using std::chrono::nanoseconds;
using seconds_d = std::chrono::duration<double>;
using source_store_array = std::array<ALsource*,3>;
using source_store_vector = std::vector<ALsource*>;
using source_store_variant = std::variant<std::monostate,source_store_array,source_store_vector>;

using namespace std::string_view_literals;

constexpr auto HasBuffer(const ALbufferQueueItem &item) noexcept -> bool
{ return bool{item.mBuffer}; }


constexpr auto get_srchandles(source_store_variant &source_store, size_t count)
{
    if(count > std::tuple_size_v<source_store_array>)
        return std::span{source_store.emplace<source_store_vector>(count)};
    return std::span{source_store.emplace<source_store_array>()}.first(count);
}

auto GetSourceVoice(ALsource *source, ALCcontext *context) -> Voice*
{
    auto voicelist = context->getVoicesSpan();
    auto idx = source->VoiceIdx;
    if(idx < voicelist.size())
    {
        auto *voice = voicelist[idx];
        if(voice->mSourceID.load(std::memory_order_acquire) == source->id)
            return voice;
    }
    source->VoiceIdx = InvalidVoiceIndex;
    return nullptr;
}


void UpdateSourceProps(const ALsource *source, Voice *voice, ALCcontext *context)
{
    /* Get an unused property container, or allocate a new one as needed. */
    auto *props = context->mFreeVoiceProps.load(std::memory_order_acquire);
    if(!props)
    {
        context->allocVoiceProps();
        props = context->mFreeVoiceProps.load(std::memory_order_acquire);
    }
    VoicePropsItem *next;
    do {
        next = props->next.load(std::memory_order_relaxed);
    } while(context->mFreeVoiceProps.compare_exchange_weak(props, next,
        std::memory_order_acq_rel, std::memory_order_acquire) == false);

    props->Pitch = source->Pitch;
    props->Gain = source->Gain;
    props->OuterGain = source->OuterGain;
    props->MinGain = source->MinGain;
    props->MaxGain = source->MaxGain;
    props->InnerAngle = source->InnerAngle;
    props->OuterAngle = source->OuterAngle;
    props->RefDistance = source->RefDistance;
    props->MaxDistance = source->MaxDistance;
    props->RolloffFactor = source->RolloffFactor
#if ALSOFT_EAX
        + source->RolloffFactor2
#endif
    ;
    props->Position = source->Position;
    props->Velocity = source->Velocity;
    props->Direction = source->Direction;
    props->OrientAt = source->OrientAt;
    props->OrientUp = source->OrientUp;
    props->HeadRelative = source->HeadRelative;
    props->mDistanceModel = source->mDistanceModel;
    props->mResampler = source->mResampler;
    props->DirectChannels = source->DirectChannels;
    props->mSpatializeMode = source->mSpatialize;
    props->mPanningEnabled = source->mPanningEnabled;

    props->DryGainHFAuto = source->DryGainHFAuto;
    props->WetGainAuto = source->WetGainAuto;
    props->WetGainHFAuto = source->WetGainHFAuto;
    props->OuterGainHF = source->OuterGainHF;

    props->AirAbsorptionFactor = source->AirAbsorptionFactor;
    props->RoomRolloffFactor = source->RoomRolloffFactor;
    props->DopplerFactor = source->DopplerFactor;

    props->StereoPan = source->StereoPan;

    props->Radius = source->Radius;
    props->EnhWidth = source->EnhWidth;
    props->Panning = source->mPanningEnabled ? source->mPan : 0.0f;

    props->Direct.Gain = source->Direct.Gain;
    props->Direct.GainHF = source->Direct.GainHF;
    props->Direct.HFReference = source->Direct.HFReference;
    props->Direct.GainLF = source->Direct.GainLF;
    props->Direct.LFReference = source->Direct.LFReference;

    std::ranges::transform(source->Send, props->Send.begin(),
        [](const ALsource::SendData &srcsend) noexcept
    {
        auto ret = VoiceProps::SendData{};
        ret.Slot = srcsend.mSlot ? srcsend.mSlot->mSlot.get() : nullptr;
        ret.Gain = srcsend.mGain;
        ret.GainHF = srcsend.mGainHF;
        ret.HFReference = srcsend.mHFReference;
        ret.GainLF = srcsend.mGainLF;
        ret.LFReference = srcsend.mLFReference;
        return ret;
    });
    if(!props->Send[0].Slot && context->mDefaultSlot)
        props->Send[0].Slot = context->mDefaultSlot->mSlot;

    /* Set the new container for updating internal parameters. */
    props = voice->mUpdate.exchange(props, std::memory_order_acq_rel);
    if(props)
    {
        /* If there was an unused update container, put it back in the
         * freelist.
         */
        AtomicReplaceHead(context->mFreeVoiceProps, props);
    }
}

/* GetSourceSampleOffset
 *
 * Gets the current read offset for the given Source, in 32.32 fixed-point
 * samples. The offset is relative to the start of the queue (not the start of
 * the current buffer).
 */
auto GetSourceSampleOffset(ALsource *Source, ALCcontext *context, nanoseconds *clocktime)
    -> int64_t
{
    auto *device = context->mALDevice.get();
    auto const *Current = LPVoiceBufferItem{};
    auto readPos = int64_t{};
    auto readPosFrac = uint{};
    auto refcount = uint{};

    do {
        refcount = device->waitForMix();
        *clocktime = device->getClockTime();
        auto *voice = GetSourceVoice(Source, context);
        if(not voice) return 0;

        Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);
        readPos = voice->mPosition.load(std::memory_order_relaxed);
        readPosFrac = voice->mPositionFrac.load(std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->mMixCount.load(std::memory_order_relaxed));

    if(readPos < 0)
        return (readPos * (std::numeric_limits<uint>::max()+1_i64))
            + (int64_t{readPosFrac} << (32-MixerFracBits));

    std::ignore = std::ranges::find_if(Source->mQueue,
        [Current,&readPos](const VoiceBufferItem &item)
    {
        if(&item == Current)
            return true;
        readPos += item.mSampleLen;
        return false;
    });
    if(readPos >= std::numeric_limits<int64_t>::max()>>32)
        return std::numeric_limits<int64_t>::max();
    return (readPos<<32) + (int64_t{readPosFrac} << (32-MixerFracBits));
}

/* GetSourceSecOffset
 *
 * Gets the current read offset for the given Source, in seconds. The offset is
 * relative to the start of the queue (not the start of the current buffer).
 */
auto GetSourceSecOffset(ALsource *Source, ALCcontext *context, nanoseconds *clocktime) -> double
{
    auto *device = context->mALDevice.get();
    auto const *Current = LPVoiceBufferItem{};
    auto readPos = int64_t{};
    auto readPosFrac = uint{};
    auto refcount = uint{};

    do {
        refcount = device->waitForMix();
        *clocktime = device->getClockTime();
        auto *voice = GetSourceVoice(Source, context);
        if(not voice) return 0.0;

        Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);
        readPos = voice->mPosition.load(std::memory_order_relaxed);
        readPosFrac = voice->mPositionFrac.load(std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->mMixCount.load(std::memory_order_relaxed));

    const auto BufferFmt = std::invoke([Source]() -> ALbuffer*
    {
        const auto iter = std::ranges::find_if(Source->mQueue, HasBuffer);
        if(iter != Source->mQueue.end())
            return iter->mBuffer.get();
        return nullptr;
    });
    Ensures(BufferFmt != nullptr);

    std::ignore = std::ranges::find_if(Source->mQueue,
        [Current,&readPos](const ALbufferQueueItem &item)
    {
        if(&item == Current)
            return true;
        readPos += item.mSampleLen;
        return false;
    });
    return (gsl::narrow_cast<double>(readPosFrac)/double{MixerFracOne}
        + gsl::narrow_cast<double>(readPos)) / BufferFmt->mSampleRate;
}

/* GetSourceOffset
 *
 * Gets the current read offset for the given Source, in the appropriate format
 * (Bytes, Samples or Seconds). The offset is relative to the start of the
 * queue (not the start of the current buffer).
 */
template<typename T>
NOINLINE auto GetSourceOffset(ALsource *Source, ALenum name, ALCcontext *context) -> T
{
    auto *device = context->mALDevice.get();
    auto const *Current = LPVoiceBufferItem{};
    auto readPos = int64_t{};
    auto readPosFrac = uint{};
    auto refcount = uint{};

    do {
        refcount = device->waitForMix();
        auto *voice = GetSourceVoice(Source, context);
        if(not voice) return T{0};

        Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);
        readPos = voice->mPosition.load(std::memory_order_relaxed);
        readPosFrac = voice->mPositionFrac.load(std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->mMixCount.load(std::memory_order_relaxed));

    const auto BufferFmt = std::invoke([Source]() -> ALbuffer*
    {
        const auto iter = std::ranges::find_if(Source->mQueue, HasBuffer);
        if(iter != Source->mQueue.end())
            return iter->mBuffer.get();
        return nullptr;
    });
    std::ignore = std::ranges::find_if(Source->mQueue,
        [Current,&readPos](const ALbufferQueueItem &item)
    {
        if(&item == Current)
            return true;
        readPos += item.mSampleLen;
        return false;
    });

    switch(name)
    {
    case AL_SEC_OFFSET:
        if constexpr(std::is_floating_point_v<T>)
        {
            const auto offset = gsl::narrow_cast<T>(readPos)
                + gsl::narrow_cast<T>(readPosFrac)/T{MixerFracOne};
            return offset / gsl::narrow_cast<T>(BufferFmt->mSampleRate);
        }
        else
        {
            readPos /= BufferFmt->mSampleRate;
            return gsl::narrow_cast<T>(std::clamp<int64_t>(readPos, std::numeric_limits<T>::min(),
                std::numeric_limits<T>::max()));
        }

    case AL_SAMPLE_OFFSET:
        if constexpr(std::is_floating_point_v<T>)
            return gsl::narrow_cast<T>(readPos) + gsl::narrow_cast<T>(readPosFrac)/T{MixerFracOne};
        else
            return gsl::narrow_cast<T>(std::clamp<int64_t>(readPos, std::numeric_limits<T>::min(),
                std::numeric_limits<T>::max()));

    case AL_BYTE_OFFSET:
        /* Round down to the block boundary. */
        const auto BlockSize = uint{BufferFmt->blockSizeFromFmt()};
        readPos = readPos / BufferFmt->mBlockAlign * BlockSize;

        if constexpr(std::is_floating_point_v<T>)
            return gsl::narrow_cast<T>(readPos);
        else
        {
            if(readPos > std::numeric_limits<T>::max())
                return RoundDown(std::numeric_limits<T>::max(), gsl::narrow_cast<T>(BlockSize));
            if(readPos < std::numeric_limits<T>::min())
                return RoundUp(std::numeric_limits<T>::min(), gsl::narrow_cast<T>(BlockSize));
            return gsl::narrow_cast<T>(readPos);
        }
    }
    return T{0};
}

/* GetSourceLength
 *
 * Gets the length of the given Source's buffer queue, in the appropriate
 * format (Bytes, Samples or Seconds).
 */
template<typename T>
NOINLINE auto GetSourceLength(const ALsource *source, ALenum name) -> T
{
    const auto BufferFmt = std::invoke([source]() -> ALbuffer*
    {
        const auto iter = std::ranges::find_if(source->mQueue, HasBuffer);
        if(iter != source->mQueue.end())
            return iter->mBuffer.get();
        return nullptr;
    });
    if(!BufferFmt)
        return T{0};

    const auto length = std::accumulate(source->mQueue.begin(), source->mQueue.end(), 0_u64,
        [](uint64_t count, const ALbufferQueueItem &item) { return count + item.mSampleLen; });
    if(length == 0)
        return T{0};

    switch(name)
    {
    case AL_SEC_LENGTH_SOFT:
        if constexpr(std::is_floating_point_v<T>)
            return gsl::narrow_cast<T>(length) / gsl::narrow_cast<T>(BufferFmt->mSampleRate);
        else
            return gsl::narrow_cast<T>(std::min<uint64_t>(length/BufferFmt->mSampleRate,
                std::numeric_limits<T>::max()));

    case AL_SAMPLE_LENGTH_SOFT:
        if constexpr(std::is_floating_point_v<T>)
            return gsl::narrow_cast<T>(length);
        else
            return gsl::narrow_cast<T>(std::min<uint64_t>(length, std::numeric_limits<T>::max()));

    case AL_BYTE_LENGTH_SOFT:
        /* Round down to the block boundary. */
        const auto BlockSize = ALuint{BufferFmt->blockSizeFromFmt()};
        const auto alignedlen = length / BufferFmt->mBlockAlign * BlockSize;

        if constexpr(std::is_floating_point_v<T>)
            return gsl::narrow_cast<T>(alignedlen);
        else
        {
            if(alignedlen > uint64_t{std::numeric_limits<T>::max()})
                return RoundDown(std::numeric_limits<T>::max(), gsl::narrow_cast<T>(BlockSize));
            return gsl::narrow_cast<T>(alignedlen);
        }
    }
    return T{0};
}


struct VoicePos {
    int pos;
    uint frac;
    ALbufferQueueItem *bufferitem;
};

/**
 * GetSampleOffset
 *
 * Retrieves the voice position, fixed-point fraction, and bufferlist item
 * using the given offset type and offset. If the offset is out of range,
 * returns an empty optional.
 */
std::optional<VoicePos> GetSampleOffset(std::deque<ALbufferQueueItem> &BufferList,
    ALenum OffsetType, double Offset)
{
    /* Find the first valid Buffer in the Queue */
    const auto BufferFmt = std::invoke([&BufferList]() -> ALbuffer*
    {
        const auto iter = std::ranges::find_if(BufferList, HasBuffer);
        if(iter != BufferList.end())
            return iter->mBuffer.get();
        return nullptr;
    });
    if(!BufferFmt) [[unlikely]]
        return std::nullopt;

    /* Get sample frame offset */
    auto [offset, frac] = std::invoke([OffsetType,Offset,BufferFmt]() -> std::pair<int64_t,uint>
    {
        auto dbloff = double{};
        auto dblfrac = double{};
        switch(OffsetType)
        {
        case AL_SEC_OFFSET:
            dblfrac = std::modf(Offset*BufferFmt->mSampleRate, &dbloff);
            if(dblfrac < 0.0)
            {
                /* If there's a negative fraction, reduce the offset to "floor"
                 * it, and convert the fraction to a percentage to the next
                 * greater value (e.g. -2.75 -> -2 + -0.75 -> -3 + 0.25).
                 */
                dbloff -= 1.0;
                dblfrac += 1.0;
            }
            return {gsl::narrow_cast<int64_t>(dbloff),
                gsl::narrow_cast<uint>(std::min(dblfrac*MixerFracOne, MixerFracOne-1.0))};

        case AL_SAMPLE_OFFSET:
            dblfrac = std::modf(Offset, &dbloff);
            if(dblfrac < 0.0)
            {
                dbloff -= 1.0;
                dblfrac += 1.0;
            }
            return {gsl::narrow_cast<int64_t>(dbloff),
                gsl::narrow_cast<uint>(std::min(dblfrac*MixerFracOne, MixerFracOne-1.0))};

        case AL_BYTE_OFFSET:
            /* Determine the ByteOffset (and ensure it is block aligned) */
            const auto blockoffset = std::floor(Offset / BufferFmt->blockSizeFromFmt());
            return {gsl::narrow_cast<int64_t>(blockoffset) * BufferFmt->mBlockAlign, 0u};
        }
        return {0_i64, 0u};
    });

    /* Find the bufferlist item this offset belongs to. */
    if(offset < 0)
    {
        if(offset < std::numeric_limits<int>::min())
            return std::nullopt;
        return VoicePos{gsl::narrow_cast<int>(offset), frac, &BufferList.front()};
    }

    if(BufferFmt->mCallback)
        return std::nullopt;

    const auto iter = std::ranges::find_if(BufferList, [&offset](const ALbufferQueueItem &item)
    {
        if(item.mSampleLen > offset)
            return true;
        offset -= item.mSampleLen;
        return false;
    });
    if(iter != BufferList.end())
    {
        /* Offset is in this buffer */
        return VoicePos{gsl::narrow_cast<int>(offset), frac, &*iter};
    }

    /* Offset is out of range of the queue */
    return std::nullopt;
}


void InitVoice(Voice *voice, ALsource *source, ALbufferQueueItem *BufferList, ALCcontext *context,
    al::Device *device)
{
    voice->mLoopBuffer.store(source->Looping ? &source->mQueue.front() : nullptr,
        std::memory_order_relaxed);

    auto *buffer = BufferList->mBuffer.get();
    voice->mFrequency = buffer->mSampleRate;
    if(buffer->mChannels == FmtStereo && source->mStereoMode == SourceStereo::Enhanced)
        voice->mFmtChannels = FmtSuperStereo;
    else
        voice->mFmtChannels = buffer->mChannels;
    voice->mFrameStep = buffer->channelsFromFmt();
    voice->mBytesPerBlock = buffer->blockSizeFromFmt();
    voice->mSamplesPerBlock = buffer->mBlockAlign;
    voice->mAmbiLayout = IsUHJ(voice->mFmtChannels) ? AmbiLayout::FuMa : buffer->mAmbiLayout;
    voice->mAmbiScaling = IsUHJ(voice->mFmtChannels) ? AmbiScaling::UHJ : buffer->mAmbiScaling;
    voice->mAmbiOrder = (voice->mFmtChannels == FmtSuperStereo) ? 1 : buffer->mAmbiOrder;

    if(buffer->mCallback) voice->mFlags.set(VoiceIsCallback);
    else if(source->SourceType == AL_STATIC) voice->mFlags.set(VoiceIsStatic);
    voice->mNumCallbackBlocks = 0;
    voice->mCallbackBlockOffset = 0;

    voice->prepare(device);

    source->mPropsDirty = false;
    UpdateSourceProps(source, voice, context);

    voice->mSourceID.store(source->id, std::memory_order_release);
}


VoiceChange *GetVoiceChanger(ALCcontext *ctx)
{
    VoiceChange *vchg{ctx->mVoiceChangeTail};
    if(vchg == ctx->mCurrentVoiceChange.load(std::memory_order_acquire)) [[unlikely]]
    {
        ctx->allocVoiceChanges();
        vchg = ctx->mVoiceChangeTail;
    }

    ctx->mVoiceChangeTail = vchg->mNext.exchange(nullptr, std::memory_order_relaxed);

    return vchg;
}

void SendVoiceChanges(ALCcontext *ctx, VoiceChange *tail)
{
    auto *device = ctx->mALDevice.get();

    VoiceChange *oldhead{ctx->mCurrentVoiceChange.load(std::memory_order_acquire)};
    while(VoiceChange *next{oldhead->mNext.load(std::memory_order_relaxed)})
        oldhead = next;
    oldhead->mNext.store(tail, std::memory_order_release);

    const bool connected{device->Connected.load(std::memory_order_acquire)};
    std::ignore = device->waitForMix();
    if(!connected) [[unlikely]]
    {
        if(ctx->mStopVoicesOnDisconnect.load(std::memory_order_acquire))
        {
            /* If the device is disconnected and voices are stopped, just
             * ignore all pending changes.
             */
            VoiceChange *cur{ctx->mCurrentVoiceChange.load(std::memory_order_acquire)};
            while(VoiceChange *next{cur->mNext.load(std::memory_order_acquire)})
            {
                cur = next;
                if(Voice *voice{cur->mVoice})
                    voice->mSourceID.store(0, std::memory_order_relaxed);
            }
            ctx->mCurrentVoiceChange.store(cur, std::memory_order_release);
        }
    }
}


auto SetVoiceOffset(Voice *oldvoice, const VoicePos &vpos, ALsource *source, ALCcontext *context,
    al::Device *device) -> bool
{
    /* First, get a free voice to start at the new offset. */
    auto voicelist = context->getVoicesSpan();
    Voice *newvoice{};
    ALuint vidx{0};
    for(Voice *voice : voicelist)
    {
        if(voice->mPlayState.load(std::memory_order_acquire) == Voice::Stopped
            && voice->mSourceID.load(std::memory_order_relaxed) == 0u
            && voice->mPendingChange.load(std::memory_order_relaxed) == false)
        {
            newvoice = voice;
            break;
        }
        ++vidx;
    }
    if(!newvoice) [[unlikely]]
    {
        auto &allvoices = *context->mVoices.load(std::memory_order_relaxed);
        if(allvoices.size() == voicelist.size())
            context->allocVoices(1);
        context->mActiveVoiceCount.fetch_add(1, std::memory_order_release);
        voicelist = context->getVoicesSpan();

        vidx = 0;
        for(Voice *voice : voicelist)
        {
            if(voice->mPlayState.load(std::memory_order_acquire) == Voice::Stopped
                && voice->mSourceID.load(std::memory_order_relaxed) == 0u
                && voice->mPendingChange.load(std::memory_order_relaxed) == false)
            {
                newvoice = voice;
                break;
            }
            ++vidx;
        }
        ASSUME(newvoice != nullptr);
    }

    /* Initialize the new voice and set its starting offset.
     * TODO: It might be better to have the VoiceChange processing copy the old
     * voice's mixing parameters (and pending update) insead of initializing it
     * all here. This would just need to set the minimum properties to link the
     * voice to the source and its position-dependent properties (including the
     * fading flag).
     */
    newvoice->mPlayState.store(Voice::Pending, std::memory_order_relaxed);
    newvoice->mPosition.store(vpos.pos, std::memory_order_relaxed);
    newvoice->mPositionFrac.store(vpos.frac, std::memory_order_relaxed);
    newvoice->mCurrentBuffer.store(vpos.bufferitem, std::memory_order_relaxed);
    newvoice->mStartTime = oldvoice->mStartTime;
    newvoice->mFlags.reset();
    if(vpos.pos > 0 || (vpos.pos == 0 && vpos.frac > 0)
        || vpos.bufferitem != &source->mQueue.front())
        newvoice->mFlags.set(VoiceIsFading);
    InitVoice(newvoice, source, vpos.bufferitem, context, device);
    source->VoiceIdx = vidx;

    /* Set the old voice as having a pending change, and send it off with the
     * new one with a new offset voice change.
     */
    oldvoice->mPendingChange.store(true, std::memory_order_relaxed);

    VoiceChange *vchg{GetVoiceChanger(context)};
    vchg->mOldVoice = oldvoice;
    vchg->mVoice = newvoice;
    vchg->mSourceID = source->id;
    vchg->mState = VChangeState::Restart;
    SendVoiceChanges(context, vchg);

    /* If the old voice still has a sourceID, it's still active and the change-
     * over will work on the next update.
     */
    if(oldvoice->mSourceID.load(std::memory_order_acquire) != 0u) [[likely]]
        return true;

    /* Otherwise, if the new voice's state is not pending, the change-over
     * already happened.
     */
    if(newvoice->mPlayState.load(std::memory_order_acquire) != Voice::Pending)
        return true;

    /* Otherwise, wait for any current mix to finish and check one last time. */
    std::ignore = device->waitForMix();
    if(newvoice->mPlayState.load(std::memory_order_acquire) != Voice::Pending)
        return true;
    /* The change-over failed because the old voice stopped before the new
     * voice could start at the new offset. Let go of the new voice and have
     * the caller store the source offset since it's stopped.
     */
    newvoice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
    newvoice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
    newvoice->mSourceID.store(0u, std::memory_order_relaxed);
    newvoice->mPlayState.store(Voice::Stopped, std::memory_order_relaxed);
    return false;
}


/**
 * Returns if the last known state for the source was playing or paused. Does
 * not sync with the mixer voice.
 */
inline bool IsPlayingOrPaused(ALsource *source)
{ return source->state == AL_PLAYING || source->state == AL_PAUSED; }

/**
 * Returns an updated source state using the matching voice's status (or lack
 * thereof).
 */
inline ALenum GetSourceState(ALsource *source, Voice *voice)
{
    if(!voice && source->state == AL_PLAYING)
        source->state = AL_STOPPED;
    return source->state;
}


bool EnsureSources(ALCcontext *context, size_t needed)
{
    auto count = std::accumulate(context->mSourceList.cbegin(), context->mSourceList.cend(), 0_uz,
        [](size_t cur, const SourceSubList &sublist) noexcept -> size_t
        { return cur + gsl::narrow_cast<ALuint>(std::popcount(sublist.FreeMask)); });

    try {
        while(needed > count)
        {
            if(context->mSourceList.size() >= 1<<25) [[unlikely]]
                return false;

            auto sublist = SourceSubList{};
            sublist.FreeMask = ~0_u64;
            sublist.Sources = SubListAllocator{}.allocate(1);
            context->mSourceList.emplace_back(std::move(sublist));
            count += std::tuple_size_v<SubListAllocator::value_type>;
        }
    }
    catch(...) {
        return false;
    }
    return true;
}

[[nodiscard]]
auto AllocSource(ALCcontext *context) noexcept -> gsl::not_null<ALsource*>
{
    auto sublist = std::ranges::find_if(context->mSourceList, &SourceSubList::FreeMask);
    auto lidx = gsl::narrow_cast<uint>(std::distance(context->mSourceList.begin(), sublist));
    auto slidx = gsl::narrow_cast<uint>(std::countr_zero(sublist->FreeMask));
    ASSUME(slidx < 64);

    auto *source = std::construct_at(std::to_address(std::next(sublist->Sources->begin(), slidx)));
#if ALSOFT_EAX
    source->eaxInitialize(context);
#endif // ALSOFT_EAX

    /* Add 1 to avoid source ID 0. */
    source->id = ((lidx<<6) | slidx) + 1;

    context->mNumSources += 1;
    sublist->FreeMask &= ~(1_u64 << slidx);

    return source;
}

void FreeSource(ALCcontext *context, ALsource *source)
{
    context->mSourceNames.erase(source->id);

    const ALuint id{source->id - 1};
    const size_t lidx{id >> 6};
    const ALuint slidx{id & 0x3f};

    if(Voice *voice{GetSourceVoice(source, context)})
    {
        VoiceChange *vchg{GetVoiceChanger(context)};

        voice->mPendingChange.store(true, std::memory_order_relaxed);
        vchg->mVoice = voice;
        vchg->mSourceID = source->id;
        vchg->mState = VChangeState::Stop;

        SendVoiceChanges(context, vchg);
    }

    std::destroy_at(source);

    context->mSourceList[lidx].FreeMask |= 1_u64 << slidx;
    context->mNumSources--;
}


[[nodiscard]]
inline auto LookupSource(std::nothrow_t, gsl::not_null<ALCcontext*> context, ALuint id) noexcept
    -> ALsource*
{
    const auto lidx = (id-1) >> 6;
    const auto slidx = (id-1) & 0x3f;

    if(lidx >= context->mSourceList.size()) [[unlikely]]
        return nullptr;
    auto &sublist = context->mSourceList[lidx];
    if(sublist.FreeMask & (1_u64 << slidx)) [[unlikely]]
        return nullptr;
    return std::to_address(sublist.Sources->begin() + slidx);
}

[[nodiscard]]
auto LookupSource(gsl::not_null<ALCcontext*> context, ALuint id) -> gsl::not_null<ALsource*>
{
    if(auto *source = LookupSource(std::nothrow, context, id)) [[likely]]
        return source;
    context->throw_error(AL_INVALID_NAME, "Invalid source ID {}", id);
}

[[nodiscard]]
inline auto LookupBuffer(std::nothrow_t, al::Device *device, std::unsigned_integral auto id)
    noexcept -> ALbuffer*
{
    const auto lidx = (id-1) >> 6;
    const auto slidx = (id-1) & 0x3f;

    if(lidx >= device->BufferList.size()) [[unlikely]]
        return nullptr;
    auto &sublist = device->BufferList[gsl::narrow_cast<size_t>(lidx)];
    if(sublist.FreeMask & (1_u64 << slidx)) [[unlikely]]
        return nullptr;
    return std::to_address(sublist.Buffers->begin() + gsl::narrow_cast<size_t>(slidx));
};

[[nodiscard]]
auto LookupBuffer(gsl::not_null<ALCcontext*> context, std::unsigned_integral auto id)
    -> gsl::not_null<ALbuffer*>
{
    if(auto *buffer = LookupBuffer(std::nothrow, context->mALDevice.get(), id)) [[likely]]
        return buffer;
    context->throw_error(AL_INVALID_NAME, "Invalid buffer ID {}", id);
}

inline auto LookupFilter(al::Device *device, std::unsigned_integral auto id) noexcept -> ALfilter*
{
    const auto lidx = (id-1) >> 6;
    const auto slidx = (id-1) & 0x3f;

    if(lidx >= device->FilterList.size()) [[unlikely]]
        return nullptr;
    auto &sublist = device->FilterList[gsl::narrow_cast<size_t>(lidx)];
    if(sublist.FreeMask & (1_u64 << slidx)) [[unlikely]]
        return nullptr;
    return std::to_address(sublist.Filters->begin() + gsl::narrow_cast<size_t>(slidx));
};

inline auto LookupEffectSlot(gsl::not_null<ALCcontext*> context, std::unsigned_integral auto id)
    noexcept -> ALeffectslot*
{
    const auto lidx = (id-1) >> 6;
    const auto slidx = (id-1) & 0x3f;

    if(lidx >= context->mEffectSlotList.size()) [[unlikely]]
        return nullptr;
    EffectSlotSubList &sublist{context->mEffectSlotList[gsl::narrow_cast<size_t>(lidx)]};
    if(sublist.FreeMask & (1_u64 << slidx)) [[unlikely]]
        return nullptr;
    return std::to_address(sublist.EffectSlots->begin() + gsl::narrow_cast<size_t>(slidx));
};


inline auto StereoModeFromEnum(std::signed_integral auto mode) noexcept
    -> std::optional<SourceStereo>
{
    switch(mode)
    {
    case AL_NORMAL_SOFT: return SourceStereo::Normal;
    case AL_SUPER_STEREO_SOFT: return SourceStereo::Enhanced;
    }
    return std::nullopt;
};
ALenum EnumFromStereoMode(SourceStereo mode)
{
    switch(mode)
    {
    case SourceStereo::Normal: return AL_NORMAL_SOFT;
    case SourceStereo::Enhanced: return AL_SUPER_STEREO_SOFT;
    }
    throw std::runtime_error{fmt::format("Invalid SourceStereo: {:#x}", al::to_underlying(mode))};
}

inline auto SpatializeModeFromEnum(std::signed_integral auto mode) noexcept
    -> std::optional<SpatializeMode>
{
    switch(mode)
    {
    case AL_FALSE: return SpatializeMode::Off;
    case AL_TRUE: return SpatializeMode::On;
    case AL_AUTO_SOFT: return SpatializeMode::Auto;
    }
    return std::nullopt;
};
ALenum EnumFromSpatializeMode(SpatializeMode mode)
{
    switch(mode)
    {
    case SpatializeMode::Off: return AL_FALSE;
    case SpatializeMode::On: return AL_TRUE;
    case SpatializeMode::Auto: return AL_AUTO_SOFT;
    }
    throw std::runtime_error{fmt::format("Invalid SpatializeMode: {}",
        int{al::to_underlying(mode)})};
}

inline auto DirectModeFromEnum(std::signed_integral auto mode) noexcept
    -> std::optional<DirectMode>
{
    switch(mode)
    {
    case AL_FALSE: return DirectMode::Off;
    case AL_DROP_UNMATCHED_SOFT: return DirectMode::DropMismatch;
    case AL_REMIX_UNMATCHED_SOFT: return DirectMode::RemixMismatch;
    }
    return std::nullopt;
};
ALenum EnumFromDirectMode(DirectMode mode)
{
    switch(mode)
    {
    case DirectMode::Off: return AL_FALSE;
    case DirectMode::DropMismatch: return AL_DROP_UNMATCHED_SOFT;
    case DirectMode::RemixMismatch: return AL_REMIX_UNMATCHED_SOFT;
    }
    throw std::runtime_error{fmt::format("Invalid DirectMode: {}", int{al::to_underlying(mode)})};
}

inline auto DistanceModelFromALenum(std::signed_integral auto model) noexcept
    -> std::optional<DistanceModel>
{
    switch(model)
    {
    case AL_NONE: return DistanceModel::Disable;
    case AL_INVERSE_DISTANCE: return DistanceModel::Inverse;
    case AL_INVERSE_DISTANCE_CLAMPED: return DistanceModel::InverseClamped;
    case AL_LINEAR_DISTANCE: return DistanceModel::Linear;
    case AL_LINEAR_DISTANCE_CLAMPED: return DistanceModel::LinearClamped;
    case AL_EXPONENT_DISTANCE: return DistanceModel::Exponent;
    case AL_EXPONENT_DISTANCE_CLAMPED: return DistanceModel::ExponentClamped;
    }
    return std::nullopt;
};
ALenum ALenumFromDistanceModel(DistanceModel model)
{
    switch(model)
    {
    case DistanceModel::Disable: return AL_NONE;
    case DistanceModel::Inverse: return AL_INVERSE_DISTANCE;
    case DistanceModel::InverseClamped: return AL_INVERSE_DISTANCE_CLAMPED;
    case DistanceModel::Linear: return AL_LINEAR_DISTANCE;
    case DistanceModel::LinearClamped: return AL_LINEAR_DISTANCE_CLAMPED;
    case DistanceModel::Exponent: return AL_EXPONENT_DISTANCE;
    case DistanceModel::ExponentClamped: return AL_EXPONENT_DISTANCE_CLAMPED;
    }
    throw std::runtime_error{fmt::format("Unexpected distance model: {}",
        int{al::to_underlying(model)})};
}

enum SourceProp : ALenum {
    srcPitch = AL_PITCH,
    srcGain = AL_GAIN,
    srcMinGain = AL_MIN_GAIN,
    srcMaxGain = AL_MAX_GAIN,
    srcMaxDistance = AL_MAX_DISTANCE,
    srcRolloffFactor = AL_ROLLOFF_FACTOR,
    srcDopplerFactor = AL_DOPPLER_FACTOR,
    srcConeOuterGain = AL_CONE_OUTER_GAIN,
    srcSecOffset = AL_SEC_OFFSET,
    srcSampleOffset = AL_SAMPLE_OFFSET,
    srcByteOffset = AL_BYTE_OFFSET,
    srcConeInnerAngle = AL_CONE_INNER_ANGLE,
    srcConeOuterAngle = AL_CONE_OUTER_ANGLE,
    srcRefDistance = AL_REFERENCE_DISTANCE,

    srcPosition = AL_POSITION,
    srcVelocity = AL_VELOCITY,
    srcDirection = AL_DIRECTION,

    srcSourceRelative = AL_SOURCE_RELATIVE,
    srcLooping = AL_LOOPING,
    srcBuffer = AL_BUFFER,
    srcSourceState = AL_SOURCE_STATE,
    srcBuffersQueued = AL_BUFFERS_QUEUED,
    srcBuffersProcessed = AL_BUFFERS_PROCESSED,
    srcSourceType = AL_SOURCE_TYPE,

    /* ALC_EXT_EFX */
    srcConeOuterGainHF = AL_CONE_OUTER_GAINHF,
    srcAirAbsorptionFactor = AL_AIR_ABSORPTION_FACTOR,
    srcRoomRolloffFactor =  AL_ROOM_ROLLOFF_FACTOR,
    srcDirectFilterGainHFAuto = AL_DIRECT_FILTER_GAINHF_AUTO,
    srcAuxSendFilterGainAuto = AL_AUXILIARY_SEND_FILTER_GAIN_AUTO,
    srcAuxSendFilterGainHFAuto = AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO,
    srcDirectFilter = AL_DIRECT_FILTER,
    srcAuxSendFilter = AL_AUXILIARY_SEND_FILTER,

    /* AL_SOFT_direct_channels */
    srcDirectChannelsSOFT = AL_DIRECT_CHANNELS_SOFT,

    /* AL_EXT_source_distance_model */
    srcDistanceModel = AL_DISTANCE_MODEL,

    /* AL_SOFT_source_latency */
    srcSampleOffsetLatencySOFT = AL_SAMPLE_OFFSET_LATENCY_SOFT,
    srcSecOffsetLatencySOFT = AL_SEC_OFFSET_LATENCY_SOFT,

    /* AL_EXT_STEREO_ANGLES */
    srcAngles = AL_STEREO_ANGLES,

    /* AL_EXT_SOURCE_RADIUS */
    srcRadius = AL_SOURCE_RADIUS,

    /* AL_EXT_BFORMAT */
    srcOrientation = AL_ORIENTATION,

    /* AL_SOFT_source_length */
    srcByteLength = AL_BYTE_LENGTH_SOFT,
    srcSampleLength = AL_SAMPLE_LENGTH_SOFT,
    srcSecLength = AL_SEC_LENGTH_SOFT,

    /* AL_SOFT_source_resampler */
    srcResampler = AL_SOURCE_RESAMPLER_SOFT,

    /* AL_SOFT_source_spatialize */
    srcSpatialize = AL_SOURCE_SPATIALIZE_SOFT,

    /* ALC_SOFT_device_clock */
    srcSampleOffsetClockSOFT = AL_SAMPLE_OFFSET_CLOCK_SOFT,
    srcSecOffsetClockSOFT = AL_SEC_OFFSET_CLOCK_SOFT,

    /* AL_SOFT_UHJ */
    srcStereoMode = AL_STEREO_MODE_SOFT,
    srcSuperStereoWidth = AL_SUPER_STEREO_WIDTH_SOFT,

    /* AL_SOFT_buffer_sub_data */
    srcByteRWOffsetsSOFT = AL_BYTE_RW_OFFSETS_SOFT,
    srcSampleRWOffsetsSOFT = AL_SAMPLE_RW_OFFSETS_SOFT,

    /* AL_SOFT_source_panning */
    srcPanningEnabledSOFT = AL_PANNING_ENABLED_SOFT,
    srcPanSOFT = AL_PAN_SOFT,
};


constexpr auto IntValsByProp(ALenum prop) -> ALuint
{
    switch(SourceProp{prop})
    {
    case AL_SOURCE_STATE:
    case AL_SOURCE_TYPE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
    case AL_BUFFER:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
    case AL_DIRECT_FILTER:
    case AL_DIRECT_FILTER_GAINHF_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
    case AL_DIRECT_CHANNELS_SOFT:
    case AL_DISTANCE_MODEL:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_STEREO_MODE_SOFT:
    case AL_PANNING_ENABLED_SOFT:
    case AL_PAN_SOFT:
        return 1;

    case AL_SOURCE_RADIUS: /*AL_BYTE_RW_OFFSETS_SOFT:*/
        if(sBufferSubDataCompat)
            return 2;
        [[fallthrough]];
    case AL_CONE_INNER_ANGLE:
    case AL_CONE_OUTER_ANGLE:
    case AL_PITCH:
    case AL_GAIN:
    case AL_MIN_GAIN:
    case AL_MAX_GAIN:
    case AL_REFERENCE_DISTANCE:
    case AL_ROLLOFF_FACTOR:
    case AL_CONE_OUTER_GAIN:
    case AL_MAX_DISTANCE:
    case AL_SEC_OFFSET:
    case AL_DOPPLER_FACTOR:
    case AL_CONE_OUTER_GAINHF:
    case AL_AIR_ABSORPTION_FACTOR:
    case AL_ROOM_ROLLOFF_FACTOR:
    case AL_SEC_LENGTH_SOFT:
    case AL_SUPER_STEREO_WIDTH_SOFT:
        return 1; /* 1x float */

    case AL_SAMPLE_RW_OFFSETS_SOFT:
        if(sBufferSubDataCompat)
            return 2;
        break;

    case AL_AUXILIARY_SEND_FILTER:
        return 3;

    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION:
        return 3; /* 3x float */

    case AL_ORIENTATION:
        return 6; /* 6x float */

    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
    case AL_STEREO_ANGLES:
        break; /* i64 only */
    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
        break; /* double only */
    }

    return 0;
}

constexpr auto Int64ValsByProp(ALenum prop) -> ALuint
{
    switch(SourceProp{prop})
    {
    case AL_SOURCE_STATE:
    case AL_SOURCE_TYPE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
    case AL_BUFFER:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
    case AL_DIRECT_FILTER:
    case AL_DIRECT_FILTER_GAINHF_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
    case AL_DIRECT_CHANNELS_SOFT:
    case AL_DISTANCE_MODEL:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_STEREO_MODE_SOFT:
    case AL_PANNING_ENABLED_SOFT:
    case AL_PAN_SOFT:
        return 1;

    case AL_SOURCE_RADIUS: /*AL_BYTE_RW_OFFSETS_SOFT:*/
        if(sBufferSubDataCompat)
            return 2;
        [[fallthrough]];
    case AL_CONE_INNER_ANGLE:
    case AL_CONE_OUTER_ANGLE:
    case AL_PITCH:
    case AL_GAIN:
    case AL_MIN_GAIN:
    case AL_MAX_GAIN:
    case AL_REFERENCE_DISTANCE:
    case AL_ROLLOFF_FACTOR:
    case AL_CONE_OUTER_GAIN:
    case AL_MAX_DISTANCE:
    case AL_SEC_OFFSET:
    case AL_DOPPLER_FACTOR:
    case AL_CONE_OUTER_GAINHF:
    case AL_AIR_ABSORPTION_FACTOR:
    case AL_ROOM_ROLLOFF_FACTOR:
    case AL_SEC_LENGTH_SOFT:
    case AL_SUPER_STEREO_WIDTH_SOFT:
        return 1; /* 1x float */

    case AL_SAMPLE_RW_OFFSETS_SOFT:
        if(sBufferSubDataCompat)
            return 2;
        break;

    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
    case AL_STEREO_ANGLES:
        return 2;

    case AL_AUXILIARY_SEND_FILTER:
        return 3;

    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION:
        return 3; /* 3x float */

    case AL_ORIENTATION:
        return 6; /* 6x float */

    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
        break; /* double only */
    }

    return 0;
}

constexpr auto FloatValsByProp(ALenum prop) -> ALuint
{
    switch(SourceProp{prop})
    {
    case AL_PITCH:
    case AL_GAIN:
    case AL_MIN_GAIN:
    case AL_MAX_GAIN:
    case AL_MAX_DISTANCE:
    case AL_ROLLOFF_FACTOR:
    case AL_DOPPLER_FACTOR:
    case AL_CONE_OUTER_GAIN:
    case AL_SEC_OFFSET:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
    case AL_CONE_INNER_ANGLE:
    case AL_CONE_OUTER_ANGLE:
    case AL_REFERENCE_DISTANCE:
    case AL_CONE_OUTER_GAINHF:
    case AL_AIR_ABSORPTION_FACTOR:
    case AL_ROOM_ROLLOFF_FACTOR:
    case AL_DIRECT_FILTER_GAINHF_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
    case AL_DIRECT_CHANNELS_SOFT:
    case AL_DISTANCE_MODEL:
    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
    case AL_SOURCE_STATE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
    case AL_SOURCE_TYPE:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SEC_LENGTH_SOFT:
    case AL_STEREO_MODE_SOFT:
    case AL_SUPER_STEREO_WIDTH_SOFT:
    case AL_PANNING_ENABLED_SOFT:
    case AL_PAN_SOFT:
        return 1;

    case AL_SOURCE_RADIUS: /*AL_BYTE_RW_OFFSETS_SOFT:*/
        if(!sBufferSubDataCompat)
            return 1;
        [[fallthrough]];
    case AL_SAMPLE_RW_OFFSETS_SOFT:
        break;

    case AL_STEREO_ANGLES:
        return 2;

    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION:
        return 3;

    case AL_ORIENTATION:
        return 6;

    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
        break; /* Double only */

    case AL_BUFFER:
    case AL_DIRECT_FILTER:
    case AL_AUXILIARY_SEND_FILTER:
        break; /* i/i64 only */
    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
        break; /* i64 only */
    }
    return 0;
}
constexpr auto DoubleValsByProp(ALenum prop) -> ALuint
{
    switch(SourceProp{prop})
    {
    case AL_PITCH:
    case AL_GAIN:
    case AL_MIN_GAIN:
    case AL_MAX_GAIN:
    case AL_MAX_DISTANCE:
    case AL_ROLLOFF_FACTOR:
    case AL_DOPPLER_FACTOR:
    case AL_CONE_OUTER_GAIN:
    case AL_SEC_OFFSET:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
    case AL_CONE_INNER_ANGLE:
    case AL_CONE_OUTER_ANGLE:
    case AL_REFERENCE_DISTANCE:
    case AL_CONE_OUTER_GAINHF:
    case AL_AIR_ABSORPTION_FACTOR:
    case AL_ROOM_ROLLOFF_FACTOR:
    case AL_DIRECT_FILTER_GAINHF_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
    case AL_DIRECT_CHANNELS_SOFT:
    case AL_DISTANCE_MODEL:
    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
    case AL_SOURCE_STATE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
    case AL_SOURCE_TYPE:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SEC_LENGTH_SOFT:
    case AL_STEREO_MODE_SOFT:
    case AL_SUPER_STEREO_WIDTH_SOFT:
    case AL_PANNING_ENABLED_SOFT:
    case AL_PAN_SOFT:
        return 1;

    case AL_SOURCE_RADIUS: /*AL_BYTE_RW_OFFSETS_SOFT:*/
        if(!sBufferSubDataCompat)
            return 1;
        [[fallthrough]];
    case AL_SAMPLE_RW_OFFSETS_SOFT:
        break;

    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
    case AL_STEREO_ANGLES:
        return 2;

    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION:
        return 3;

    case AL_ORIENTATION:
        return 6;

    case AL_BUFFER:
    case AL_DIRECT_FILTER:
    case AL_AUXILIARY_SEND_FILTER:
        break; /* i/i64 only */
    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
        break; /* i64 only */
    }
    return 0;
}


void UpdateSourceProps(gsl::not_null<ALsource*> source, gsl::not_null<ALCcontext*> context)
{
    if(!context->mDeferUpdates)
    {
        if(auto *voice = GetSourceVoice(source, context))
        {
            UpdateSourceProps(source, voice, context);
            return;
        }
    }
    source->mPropsDirty = true;
}
#if ALSOFT_EAX
void CommitAndUpdateSourceProps(gsl::not_null<ALsource*> source,
    gsl::not_null<ALCcontext*> context)
{
    if(!context->mDeferUpdates)
    {
        if(context->hasEax())
            source->eaxCommit();
        if(auto *voice = GetSourceVoice(source, context))
        {
            UpdateSourceProps(source, voice, context);
            return;
        }
    }
    source->mPropsDirty = true;
}

#else

inline void CommitAndUpdateSourceProps(gsl::not_null<ALsource*> source,
    gsl::not_null<ALCcontext*> context)
{ UpdateSourceProps(source, context); }
#endif


template<typename T>
auto PropTypeName() -> std::string_view = delete;
template<>
auto PropTypeName<ALint>() -> std::string_view { return "integer"sv; }
template<>
auto PropTypeName<ALint64SOFT>() -> std::string_view { return "int64"sv; }
template<>
auto PropTypeName<ALfloat>() -> std::string_view { return "float"sv; }
template<>
auto PropTypeName<ALdouble>() -> std::string_view { return "double"sv; }


/**
 * Returns a pair of lambdas to check the following setter.
 *
 * The first lambda checks the size of the span is valid for the required size,
 * throwing a context error if it fails.
 *
 * The second lambda tests the validity of the value check, throwing a context
 * error if it failed.
 */
template<typename T, typename U>
struct PairStruct { T First; U Second; };
template<typename T, typename U>
PairStruct(T,U) -> PairStruct<T,U>;

template<typename T, size_t N>
auto GetCheckers(gsl::not_null<ALCcontext*> context, const SourceProp prop, const std::span<T,N> values)
{
    return PairStruct{
        [=](size_t expect) -> void
        {
            if(values.size() == expect) return;
            context->throw_error(AL_INVALID_ENUM, "Property {:#04x} expects {} value{}, got {}",
                as_unsigned(al::to_underlying(prop)), expect, (expect==1) ? "" : "s",
                values.size());
        },
        [context](bool passed) -> void
        {
            if(passed) return;
            context->throw_error(AL_INVALID_VALUE, "Value out of range");
        }
    };
}

template<typename T>
NOINLINE void SetProperty(const gsl::not_null<ALsource*> Source,
    const gsl::not_null<ALCcontext*> Context, const SourceProp prop,
    const std::span<const T> values)
{
    static constexpr auto is_finite = [](auto&& v) -> bool
    { return std::isfinite(gsl::narrow_cast<float>(std::forward<decltype(v)>(v))); };
    auto [CheckSize, CheckValue] = GetCheckers(Context, prop, values);
    auto *device = Context->mALDevice.get();

    switch(prop)
    {
    case AL_SOURCE_STATE:
    case AL_SOURCE_TYPE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
        if constexpr(std::is_integral_v<T>)
        {
            /* Query only */
            Context->throw_error(AL_INVALID_OPERATION,
                "Setting read-only source property {:#04x}", as_unsigned(al::to_underlying(prop)));
        }
        break;

    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SEC_LENGTH_SOFT:
    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
        /* Query only */
        Context->throw_error(AL_INVALID_OPERATION, "Setting read-only source property {:#04x}",
            as_unsigned(al::to_underlying(prop)));

    case AL_PITCH:
        CheckSize(1);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(values[0] >= T{0} && is_finite(values[0]));
        else
            CheckValue(values[0] >= T{0});

        Source->Pitch = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_CONE_INNER_ANGLE:
        CheckSize(1);
        CheckValue(values[0] >= T{0} && values[0] <= T{360});

        Source->InnerAngle = gsl::narrow_cast<float>(values[0]);
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_CONE_OUTER_ANGLE:
        CheckSize(1);
        CheckValue(values[0] >= T{0} && values[0] <= T{360});

        Source->OuterAngle = gsl::narrow_cast<float>(values[0]);
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_GAIN:
        CheckSize(1);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(values[0] >= T{0} && is_finite(values[0]));
        else
            CheckValue(values[0] >= T{0});

        Source->Gain = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_MAX_DISTANCE:
        CheckSize(1);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(values[0] >= T{0} && is_finite(values[0]));
        else
            CheckValue(values[0] >= T{0});

        Source->MaxDistance = gsl::narrow_cast<float>(values[0]);
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_ROLLOFF_FACTOR:
        CheckSize(1);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(values[0] >= T{0} && is_finite(values[0]));
        else
            CheckValue(values[0] >= T{0});

        Source->RolloffFactor = gsl::narrow_cast<float>(values[0]);
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_REFERENCE_DISTANCE:
        CheckSize(1);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(values[0] >= T{0} && is_finite(values[0]));
        else
            CheckValue(values[0] >= T{0});

        Source->RefDistance = gsl::narrow_cast<float>(values[0]);
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_MIN_GAIN:
        CheckSize(1);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(values[0] >= T{0} && is_finite(values[0]));
        else
            CheckValue(values[0] >= T{0});

        Source->MinGain = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_MAX_GAIN:
        CheckSize(1);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(values[0] >= T{0} && is_finite(values[0]));
        else
            CheckValue(values[0] >= T{0});

        Source->MaxGain = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_CONE_OUTER_GAIN:
        CheckSize(1);
        CheckValue(values[0] >= T{0} && values[0] <= T{1});

        Source->OuterGain = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_CONE_OUTER_GAINHF:
        CheckSize(1);
        CheckValue(values[0] >= T{0} && values[0] <= T{1});

        Source->OuterGainHF = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_AIR_ABSORPTION_FACTOR:
        CheckSize(1);
        CheckValue(values[0] >= T{0} && values[0] <= T{10});

        Source->AirAbsorptionFactor = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_ROOM_ROLLOFF_FACTOR:
        CheckSize(1);
        CheckValue(values[0] >= T{0} && values[0] <= T{1});

        Source->RoomRolloffFactor = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_DOPPLER_FACTOR:
        CheckSize(1);
        CheckValue(values[0] >= T{0} && values[0] <= T{1});

        Source->DopplerFactor = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);


    case AL_SOURCE_RELATIVE:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            CheckValue(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->HeadRelative = values[0] != AL_FALSE;
            return CommitAndUpdateSourceProps(Source, Context);
        }
        break;

    case AL_LOOPING:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            CheckValue(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->Looping = values[0] != AL_FALSE;
            if(Voice *voice{GetSourceVoice(Source, Context)})
            {
                if(Source->Looping)
                    voice->mLoopBuffer.store(&Source->mQueue.front(), std::memory_order_release);
                else
                    voice->mLoopBuffer.store(nullptr, std::memory_order_release);

                /* If the source is playing, wait for the current mix to finish
                 * to ensure it isn't currently looping back or reaching the
                 * end.
                 */
                std::ignore = device->waitForMix();
            }
            return;
        }
        break;

    case AL_BUFFER:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            if(const auto state = GetSourceState(Source, GetSourceVoice(Source, Context));
                state == AL_PLAYING || state == AL_PAUSED)
                Context->throw_error(AL_INVALID_OPERATION,
                    "Setting buffer on playing or paused source {}", Source->id);

            if(values[0])
            {
                auto buflock = std::lock_guard{device->BufferLock};
                auto buffer = LookupBuffer(Context, as_unsigned(values[0]));
                if(buffer->MappedAccess && !(buffer->MappedAccess&AL_MAP_PERSISTENT_BIT_SOFT))
                    Context->throw_error(AL_INVALID_OPERATION,
                        "Setting non-persistently mapped buffer {}", buffer->id);
                if(buffer->mCallback && buffer->mRef.load(std::memory_order_relaxed) != 0)
                    Context->throw_error(AL_INVALID_OPERATION,
                        "Setting already-set callback buffer {}", buffer->id);

                /* Add the selected buffer to a one-item queue */
                auto newlist = std::deque<ALbufferQueueItem>{};
                auto &item = newlist.emplace_back();
                item.mBuffer = buffer->newReference();
                item.mCallback = buffer->mCallback;
                item.mUserData = buffer->mUserData;
                item.mBlockAlign = buffer->mBlockAlign;
                item.mSampleLen = buffer->mSampleLen;
                item.mLoopStart = buffer->mLoopStart;
                item.mLoopEnd = buffer->mLoopEnd;
                item.mSamples = buffer->mData;

                /* Source is now Static */
                Source->SourceType = AL_STATIC;
                Source->mQueue = std::move(newlist);
            }
            else
            {
                /* Source is now Undetermined */
                Source->SourceType = AL_UNDETERMINED;
                std::deque<ALbufferQueueItem>{}.swap(Source->mQueue);
            }
            return;
        }
        break;


    case AL_SEC_OFFSET:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
        CheckSize(1);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(std::isfinite(values[0]));

        if(auto *voice = GetSourceVoice(Source, Context))
        {
            auto vpos = GetSampleOffset(Source->mQueue, prop, gsl::narrow_cast<double>(values[0]));
            if(!vpos) Context->throw_error(AL_INVALID_VALUE, "Invalid offset");

            if(SetVoiceOffset(voice, *vpos, Source, Context, Context->mALDevice.get()))
                return;
        }
        Source->OffsetType = prop;
        Source->Offset = gsl::narrow_cast<double>(values[0]);
        return;

    case AL_SAMPLE_RW_OFFSETS_SOFT:
        if(sBufferSubDataCompat)
        {
            if constexpr(std::is_integral_v<T>)
            {
                /* Query only */
                Context->throw_error(AL_INVALID_OPERATION,
                    "Setting read-only source property {:#04x}",
                    as_unsigned(al::to_underlying(prop)));
            }
        }
        break;

    case AL_SOURCE_RADIUS: /*AL_BYTE_RW_OFFSETS_SOFT:*/
        if(sBufferSubDataCompat)
        {
            if constexpr(std::is_integral_v<T>)
            {
                /* Query only */
                Context->throw_error(AL_INVALID_OPERATION,
                    "Setting read-only source property {:#04x}",
                    as_unsigned(al::to_underlying(prop)));
            }
            break;
        }
        CheckSize(1);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(values[0] >= T{0} && is_finite(values[0]));
        else
            CheckValue(values[0] >= T{0});

        Source->Radius = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_SUPER_STEREO_WIDTH_SOFT:
        CheckSize(1);
        CheckValue(values[0] >= T{0} && values[0] <= T{1});

        Source->EnhWidth = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_PANNING_ENABLED_SOFT:
        CheckSize(1);
        CheckValue(values[0] == AL_FALSE || values[0] == AL_TRUE);

        Source->mPanningEnabled = values[0] != AL_FALSE;
        return UpdateSourceProps(Source, Context);

    case AL_PAN_SOFT:
        CheckSize(1);
        CheckValue(values[0] >= T{-1} && values[0] <= T{1});

        Source->mPan = gsl::narrow_cast<float>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_STEREO_ANGLES:
        CheckSize(2);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(std::ranges::all_of(values, is_finite));

        Source->StereoPan[0] = gsl::narrow_cast<float>(values[0]);
        Source->StereoPan[1] = gsl::narrow_cast<float>(values[1]);
        return UpdateSourceProps(Source, Context);


    case AL_POSITION:
        CheckSize(3);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(std::ranges::all_of(values, is_finite));

        Source->Position[0] = gsl::narrow_cast<float>(values[0]);
        Source->Position[1] = gsl::narrow_cast<float>(values[1]);
        Source->Position[2] = gsl::narrow_cast<float>(values[2]);
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_VELOCITY:
        CheckSize(3);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(std::ranges::all_of(values, is_finite));

        Source->Velocity[0] = gsl::narrow_cast<float>(values[0]);
        Source->Velocity[1] = gsl::narrow_cast<float>(values[1]);
        Source->Velocity[2] = gsl::narrow_cast<float>(values[2]);
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_DIRECTION:
        CheckSize(3);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(std::ranges::all_of(values, is_finite));

        Source->Direction[0] = gsl::narrow_cast<float>(values[0]);
        Source->Direction[1] = gsl::narrow_cast<float>(values[1]);
        Source->Direction[2] = gsl::narrow_cast<float>(values[2]);
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_ORIENTATION:
        CheckSize(6);
        if constexpr(std::is_floating_point_v<T>)
            CheckValue(std::ranges::all_of(values, is_finite));

        Source->OrientAt[0] = gsl::narrow_cast<float>(values[0]);
        Source->OrientAt[1] = gsl::narrow_cast<float>(values[1]);
        Source->OrientAt[2] = gsl::narrow_cast<float>(values[2]);
        Source->OrientUp[0] = gsl::narrow_cast<float>(values[3]);
        Source->OrientUp[1] = gsl::narrow_cast<float>(values[4]);
        Source->OrientUp[2] = gsl::narrow_cast<float>(values[5]);
        return UpdateSourceProps(Source, Context);


    case AL_DIRECT_FILTER:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            const auto filterid = as_unsigned(values[0]);
            if(values[0])
            {
                const auto filterlock = std::lock_guard{device->FilterLock};
                const auto *filter = LookupFilter(device, filterid);
                if(!filter)
                    Context->throw_error(AL_INVALID_VALUE, "Invalid filter ID {}", filterid);
                Source->Direct.Gain = filter->Gain;
                Source->Direct.GainHF = filter->GainHF;
                Source->Direct.HFReference = filter->HFReference;
                Source->Direct.GainLF = filter->GainLF;
                Source->Direct.LFReference = filter->LFReference;
            }
            else
            {
                Source->Direct.Gain = 1.0f;
                Source->Direct.GainHF = 1.0f;
                Source->Direct.HFReference = LowPassFreqRef;
                Source->Direct.GainLF = 1.0f;
                Source->Direct.LFReference = HighPassFreqRef;
            }
            return UpdateSourceProps(Source, Context);
        }
        break;

    case AL_DIRECT_FILTER_GAINHF_AUTO:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            CheckValue(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->DryGainHFAuto = values[0] != AL_FALSE;
            return UpdateSourceProps(Source, Context);
        }
        break;

    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            CheckValue(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->WetGainAuto = values[0] != AL_FALSE;
            return UpdateSourceProps(Source, Context);
        }
        break;

    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            CheckValue(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->WetGainHFAuto = values[0] != AL_FALSE;
            return UpdateSourceProps(Source, Context);
        }
        break;

    case AL_DIRECT_CHANNELS_SOFT:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            if(auto mode = DirectModeFromEnum(values[0]))
            {
                Source->DirectChannels = *mode;
                return UpdateSourceProps(Source, Context);
            }
            Context->throw_error(AL_INVALID_VALUE, "Invalid direct channels mode: {:#x}",
                as_unsigned(values[0]));
        }
        break;

    case AL_DISTANCE_MODEL:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            if(auto model = DistanceModelFromALenum(values[0]))
            {
                Source->mDistanceModel = *model;
                if(Context->mSourceDistanceModel)
                    UpdateSourceProps(Source, Context);
                return;
            }
            Context->throw_error(AL_INVALID_VALUE, "Invalid distance model: {:#x}",
                as_unsigned(values[0]));
        }
        break;

    case AL_SOURCE_RESAMPLER_SOFT:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            CheckValue(values[0] >= 0 && values[0] <= al::to_underlying(Resampler::Max));

            Source->mResampler = gsl::narrow_cast<Resampler>(values[0]);
            return UpdateSourceProps(Source, Context);
        }
        break;

    case AL_SOURCE_SPATIALIZE_SOFT:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            if(auto mode = SpatializeModeFromEnum(values[0]))
            {
                Source->mSpatialize = *mode;
                return UpdateSourceProps(Source, Context);
            }
            Context->throw_error(AL_INVALID_VALUE, "Invalid source spatialize mode: {}",
                values[0]);
        }
        break;

    case AL_STEREO_MODE_SOFT:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            if(const ALenum state{GetSourceState(Source, GetSourceVoice(Source, Context))};
                state == AL_PLAYING || state == AL_PAUSED)
                Context->throw_error(AL_INVALID_OPERATION,
                    "Modifying stereo mode on playing or paused source {}", Source->id);

            if(auto mode = StereoModeFromEnum(values[0]))
            {
                Source->mStereoMode = *mode;
                return;
            }
            Context->throw_error(AL_INVALID_VALUE, "Invalid stereo mode: {:#x}",
                as_unsigned(values[0]));
        }
        break;

    case AL_AUXILIARY_SEND_FILTER:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(3);
            const auto slotid = as_unsigned(values[0]);
            const auto sendidx = as_unsigned(values[1]);
            const auto filterid = as_unsigned(values[2]);

            const auto slotlock = std::unique_lock{Context->mEffectSlotLock};
            auto slot = al::intrusive_ptr<ALeffectslot>{};
            if(slotid)
            {
                auto auxslot = LookupEffectSlot(Context, slotid);
                if(!auxslot)
                    Context->throw_error(AL_INVALID_VALUE, "Invalid effect ID {}", slotid);
                slot = auxslot->newReference();
            }

            if(sendidx >= device->NumAuxSends)
                Context->throw_error(AL_INVALID_VALUE, "Invalid send {}", sendidx);
            auto &send = Source->Send[gsl::narrow_cast<size_t>(sendidx)];

            if(filterid)
            {
                const auto filterlock = std::lock_guard{device->FilterLock};
                const auto *filter = LookupFilter(device, filterid);
                if(!filter)
                    Context->throw_error(AL_INVALID_VALUE, "Invalid filter ID {}", filterid);

                send.mGain = filter->Gain;
                send.mGainHF = filter->GainHF;
                send.mHFReference = filter->HFReference;
                send.mGainLF = filter->GainLF;
                send.mLFReference = filter->LFReference;
            }
            else
            {
                /* Disable filter */
                send.mGain = 1.0f;
                send.mGainHF = 1.0f;
                send.mHFReference = LowPassFreqRef;
                send.mGainLF = 1.0f;
                send.mLFReference = HighPassFreqRef;
            }

            /* We must force an update if the current auxiliary slot is valid
             * and about to be changed on an active source, in case the old
             * slot is about to be deleted.
             */
            if(send.mSlot && slot != send.mSlot && IsPlayingOrPaused(Source))
            {
                send.mSlot = std::move(slot);

                Voice *voice{GetSourceVoice(Source, Context)};
                if(voice) UpdateSourceProps(Source, voice, Context);
                else Source->mPropsDirty = true;
            }
            else
            {
                send.mSlot = std::move(slot);
                UpdateSourceProps(Source, Context);
            }
            return;
        }
        break;
    }

    Context->throw_error(AL_INVALID_ENUM, "Invalid source {} property {:#04x}", PropTypeName<T>(),
        as_unsigned(al::to_underlying(prop)));
}


template<typename T, size_t N>
auto GetSizeChecker(gsl::not_null<ALCcontext*> context, const SourceProp prop,
    const std::span<T,N> values)
{
    return [=](size_t expect) -> void
    {
        if(values.size() == expect) [[likely]] return;
        context->throw_error(AL_INVALID_ENUM, "Property {:#04x} expects {} value{}, got {}",
            as_unsigned(al::to_underlying(prop)), expect, (expect==1) ? "" : "s", values.size());
    };
}

template<typename T>
NOINLINE void GetProperty(const gsl::not_null<ALsource*> Source,
    const gsl::not_null<ALCcontext*> Context, const SourceProp prop,
    const std::span<T> values)
{
    using std::chrono::duration_cast;
    auto CheckSize = GetSizeChecker(Context, prop, values);
    auto *device = Context->mALDevice.get();

    switch(prop)
    {
    case AL_GAIN:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->Gain);
        return;

    case AL_PITCH:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->Pitch);
        return;

    case AL_MAX_DISTANCE:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->MaxDistance);
        return;

    case AL_ROLLOFF_FACTOR:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->RolloffFactor);
        return;

    case AL_REFERENCE_DISTANCE:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->RefDistance);
        return;

    case AL_CONE_INNER_ANGLE:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->InnerAngle);
        return;

    case AL_CONE_OUTER_ANGLE:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->OuterAngle);
        return;

    case AL_MIN_GAIN:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->MinGain);
        return;

    case AL_MAX_GAIN:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->MaxGain);
        return;

    case AL_CONE_OUTER_GAIN:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->OuterGain);
        return;

    case AL_SEC_OFFSET:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
        CheckSize(1);
        values[0] = GetSourceOffset<T>(Source, prop, Context);
        return;

    case AL_CONE_OUTER_GAINHF:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->OuterGainHF);
        return;

    case AL_AIR_ABSORPTION_FACTOR:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->AirAbsorptionFactor);
        return;

    case AL_ROOM_ROLLOFF_FACTOR:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->RoomRolloffFactor);
        return;

    case AL_DOPPLER_FACTOR:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->DopplerFactor);
        return;

    case AL_SAMPLE_RW_OFFSETS_SOFT:
        if constexpr(std::is_integral_v<T>)
        {
            if(sBufferSubDataCompat)
            {
                CheckSize(2);
                values[0] = GetSourceOffset<T>(Source, AL_SAMPLE_OFFSET, Context);
                /* FIXME: values[1] should be ahead of values[0] by the device
                 * update time. It needs to clamp or wrap the length of the
                 * buffer queue.
                 */
                values[1] = values[0];
                return;
            }
        }
        break;
    case AL_SOURCE_RADIUS: /*AL_BYTE_RW_OFFSETS_SOFT:*/
        if constexpr(std::is_floating_point_v<T>)
        {
            if(sBufferSubDataCompat)
                break;

            CheckSize(1);
            values[0] = gsl::narrow_cast<T>(Source->Radius);
            return;
        }
        else
        {
            if(sBufferSubDataCompat)
            {
                CheckSize(2);
                values[0] = GetSourceOffset<T>(Source, AL_BYTE_OFFSET, Context);
                /* FIXME: values[1] should be ahead of values[0] by the device
                 * update time. It needs to clamp or wrap the length of the
                 * buffer queue.
                 */
                values[1] = values[0];
                return;
            }
            break;
        }

    case AL_SUPER_STEREO_WIDTH_SOFT:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->EnhWidth);
        return;

    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SEC_LENGTH_SOFT:
        CheckSize(1);
        values[0] = GetSourceLength<T>(Source, prop);
        return;

    case AL_PANNING_ENABLED_SOFT:
        CheckSize(1);
        values[0] = Source->mPanningEnabled;
        return;

    case AL_PAN_SOFT:
        CheckSize(1);
        values[0] = gsl::narrow_cast<T>(Source->mPan);
        return;

    case AL_STEREO_ANGLES:
        if constexpr(std::is_floating_point_v<T>)
        {
            CheckSize(2);
            values[0] = gsl::narrow_cast<T>(Source->StereoPan[0]);
            values[1] = gsl::narrow_cast<T>(Source->StereoPan[1]);
            return;
        }
        break;

    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
        if constexpr(std::is_same_v<T,int64_t>)
        {
            CheckSize(2);
            /* Get the source offset with the clock time first. Then get the
             * clock time with the device latency. Order is important.
             */
            auto srcclock = nanoseconds{};
            values[0] = GetSourceSampleOffset(Source, Context, &srcclock);
            const auto clocktime = std::invoke([device]() -> ClockLatency
            {
                auto statelock = std::lock_guard{device->StateLock};
                return GetClockLatency(device, device->Backend.get());
            });
            if(srcclock == clocktime.ClockTime)
                values[1] = nanoseconds{clocktime.Latency}.count();
            else
            {
                /* If the clock time incremented, reduce the latency by that
                 * much since it's that much closer to the source offset it got
                 * earlier.
                 */
                const auto diff = std::min(clocktime.Latency, clocktime.ClockTime-srcclock);
                values[1] = nanoseconds{clocktime.Latency - diff}.count();
            }
            return;
        }
        break;

    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
        if constexpr(std::is_same_v<T,int64_t>)
        {
            CheckSize(2);
            auto srcclock = nanoseconds{};
            values[0] = GetSourceSampleOffset(Source, Context, &srcclock);
            values[1] = srcclock.count();
            return;
        }
        break;

    case AL_SEC_OFFSET_LATENCY_SOFT:
        if constexpr(std::is_same_v<T,double>)
        {
            CheckSize(2);
            /* Get the source offset with the clock time first. Then get the
             * clock time with the device latency. Order is important.
             */
            auto srcclock = nanoseconds{};
            values[0] = GetSourceSecOffset(Source, Context, &srcclock);
            const auto clocktime = std::invoke([device]() -> ClockLatency
            {
                auto statelock = std::lock_guard{device->StateLock};
                return GetClockLatency(device, device->Backend.get());
            });
            if(srcclock == clocktime.ClockTime)
                values[1] = duration_cast<seconds_d>(clocktime.Latency).count();
            else
            {
                /* If the clock time incremented, reduce the latency by that
                 * much since it's that much closer to the source offset it got
                 * earlier.
                 */
                const auto diff = std::min(clocktime.Latency, clocktime.ClockTime-srcclock);
                values[1] = duration_cast<seconds_d>(clocktime.Latency - diff).count();
            }
            return;
        }
        break;

    case AL_SEC_OFFSET_CLOCK_SOFT:
        if constexpr(std::is_same_v<T,double>)
        {
            CheckSize(2);
            nanoseconds srcclock{};
            values[0] = GetSourceSecOffset(Source, Context, &srcclock);
            values[1] = duration_cast<seconds_d>(srcclock).count();
            return;
        }
        break;

    case AL_POSITION:
        CheckSize(3);
        values[0] = gsl::narrow_cast<T>(Source->Position[0]);
        values[1] = gsl::narrow_cast<T>(Source->Position[1]);
        values[2] = gsl::narrow_cast<T>(Source->Position[2]);
        return;

    case AL_VELOCITY:
        CheckSize(3);
        values[0] = gsl::narrow_cast<T>(Source->Velocity[0]);
        values[1] = gsl::narrow_cast<T>(Source->Velocity[1]);
        values[2] = gsl::narrow_cast<T>(Source->Velocity[2]);
        return;

    case AL_DIRECTION:
        CheckSize(3);
        values[0] = gsl::narrow_cast<T>(Source->Direction[0]);
        values[1] = gsl::narrow_cast<T>(Source->Direction[1]);
        values[2] = gsl::narrow_cast<T>(Source->Direction[2]);
        return;

    case AL_ORIENTATION:
        CheckSize(6);
        values[0] = gsl::narrow_cast<T>(Source->OrientAt[0]);
        values[1] = gsl::narrow_cast<T>(Source->OrientAt[1]);
        values[2] = gsl::narrow_cast<T>(Source->OrientAt[2]);
        values[3] = gsl::narrow_cast<T>(Source->OrientUp[0]);
        values[4] = gsl::narrow_cast<T>(Source->OrientUp[1]);
        values[5] = gsl::narrow_cast<T>(Source->OrientUp[2]);
        return;


    case AL_SOURCE_RELATIVE:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = Source->HeadRelative;
            return;
        }
        break;

    case AL_LOOPING:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = Source->Looping;
            return;
        }
        break;

    case AL_BUFFER:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            const ALbufferQueueItem *BufferList{};
            /* HACK: This query should technically only return the buffer set
             * on a static source. However, some apps had used it to detect
             * when a streaming source changed buffers, so report the current
             * buffer's ID when playing.
             */
            if(Source->SourceType == AL_STATIC || Source->state == AL_INITIAL)
            {
                if(!Source->mQueue.empty())
                    BufferList = &Source->mQueue.front();
            }
            else if(Voice *voice{GetSourceVoice(Source, Context)})
            {
                auto *Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);
                const auto iter = std::ranges::find_if(Source->mQueue,
                    [Current](const ALbufferQueueItem &item) noexcept -> bool
                    { return &item == Current; });
                BufferList = (iter != Source->mQueue.end()) ? &*iter : nullptr;
            }
            auto *buffer = BufferList ? BufferList->mBuffer.get() : nullptr;
            values[0] = buffer ? static_cast<T>(buffer->id) : T{0};
            return;
        }
        break;

    case AL_SOURCE_STATE:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = GetSourceState(Source, GetSourceVoice(Source, Context));
            return;
        }
        break;

    case AL_BUFFERS_QUEUED:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = gsl::narrow_cast<T>(Source->mQueue.size());
            return;
        }
        break;

    case AL_BUFFERS_PROCESSED:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            if(Source->Looping || Source->SourceType != AL_STREAMING)
            {
                /* Buffers on a looping source are in a perpetual state of
                 * PENDING, so don't report any as PROCESSED
                 */
                values[0] = 0;
            }
            else
            {
                auto played = 0;
                if(Source->state != AL_INITIAL)
                {
                    const auto Current = std::invoke([Source,Context]() -> const VoiceBufferItem*
                    {
                        if(Voice *voice{GetSourceVoice(Source, Context)})
                            return voice->mCurrentBuffer.load(std::memory_order_relaxed);
                        return nullptr;
                    });
                    std::ignore = std::ranges::find_if(Source->mQueue,
                        [Current,&played](const ALbufferQueueItem &item) noexcept -> bool
                    {
                        if(&item == Current)
                            return true;
                        ++played;
                        return false;
                    });
                }
                values[0] = played;
            }
            return;
        }
        break;

    case AL_SOURCE_TYPE:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = Source->SourceType;
            return;
        }
        break;

    case AL_DIRECT_FILTER_GAINHF_AUTO:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = Source->DryGainHFAuto;
            return;
        }
        break;

    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = Source->WetGainAuto;
            return;
        }
        break;

    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = Source->WetGainHFAuto;
            return;
        }
        break;

    case AL_DIRECT_CHANNELS_SOFT:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = EnumFromDirectMode(Source->DirectChannels);
            return;
        }
        break;

    case AL_DISTANCE_MODEL:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = ALenumFromDistanceModel(Source->mDistanceModel);
            return;
        }
        break;

    case AL_SOURCE_RESAMPLER_SOFT:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = T{al::to_underlying(Source->mResampler)};
            return;
        }
        break;

    case AL_SOURCE_SPATIALIZE_SOFT:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = EnumFromSpatializeMode(Source->mSpatialize);
            return;
        }
        break;

    case AL_STEREO_MODE_SOFT:
        if constexpr(std::is_integral_v<T>)
        {
            CheckSize(1);
            values[0] = EnumFromStereoMode(Source->mStereoMode);
            return;
        }
        break;

    case AL_DIRECT_FILTER:
    case AL_AUXILIARY_SEND_FILTER:
        break;
    }

    Context->throw_error(AL_INVALID_ENUM, "Invalid source {} query property {:#04x}",
        PropTypeName<T>(), as_unsigned(al::to_underlying(prop)));
}


void StartSources(const gsl::not_null<ALCcontext*> context,
    const std::span<ALsource*> srchandles, const nanoseconds start_time=nanoseconds::min())
{
    auto *device = context->mALDevice.get();
    /* If the device is disconnected, and voices stop on disconnect, go right
     * to stopped.
     */
    if(!device->Connected.load(std::memory_order_acquire)) [[unlikely]]
    {
        if(context->mStopVoicesOnDisconnect.load(std::memory_order_acquire))
        {
            for(ALsource *source : srchandles)
            {
                /* TODO: Send state change event? */
                source->Offset = 0.0;
                source->OffsetType = AL_NONE;
                source->state = AL_STOPPED;
            }
            return;
        }
    }

    /* Count the number of reusable voices. */
    auto voicelist = context->getVoicesSpan();
    auto free_voices = 0_uz;
    std::ignore = std::ranges::find_if(voicelist, [srchandles,&free_voices](const Voice *voice)
    {
        free_voices += (voice->mPlayState.load(std::memory_order_acquire) == Voice::Stopped
            && voice->mSourceID.load(std::memory_order_relaxed) == 0u
            && voice->mPendingChange.load(std::memory_order_relaxed) == false);
        return (free_voices == srchandles.size());
    });
    if(srchandles.size() != free_voices) [[unlikely]]
    {
        const auto inc_amount = srchandles.size() - free_voices;
        auto &allvoices = *context->mVoices.load(std::memory_order_relaxed);
        if(inc_amount > allvoices.size() - voicelist.size())
        {
            /* Increase the number of voices to handle the request. */
            context->allocVoices(inc_amount - (allvoices.size() - voicelist.size()));
        }
        context->mActiveVoiceCount.fetch_add(inc_amount, std::memory_order_release);
        voicelist = context->getVoicesSpan();
    }

    auto voiceiter = voicelist.begin();
    auto vidx = 0u;
    auto tail = LPVoiceChange{};
    auto cur = LPVoiceChange{};
    std::ranges::for_each(srchandles, [&](ALsource *source)
    {
        /* Check that there is a queue containing at least one valid, non zero
         * length buffer.
         */
        const auto BufferList = std::ranges::find_if(source->mQueue, [](ALbufferQueueItem &entry)
        { return entry.mSampleLen != 0 || entry.mCallback != nullptr; });

        /* If there's nothing to play, go right to stopped. */
        if(BufferList == source->mQueue.end()) [[unlikely]]
        {
            /* NOTE: A source without any playable buffers should not have a
             * Voice since it shouldn't be in a playing or paused state. So
             * there's no need to look up its voice and clear the source.
             */
            source->Offset = 0.0;
            source->OffsetType = AL_NONE;
            source->state = AL_STOPPED;
            return;
        }

        if(!cur)
            cur = tail = GetVoiceChanger(context);
        else
        {
            cur->mNext.store(GetVoiceChanger(context), std::memory_order_relaxed);
            cur = cur->mNext.load(std::memory_order_relaxed);
        }

        auto *voice = GetSourceVoice(source, context);
        switch(GetSourceState(source, voice))
        {
        case AL_PAUSED:
            /* A source that's paused simply resumes. If there's no voice, it
             * was lost from a disconnect, so just start over with a new one.
             */
            cur->mOldVoice = nullptr;
            if(!voice) break;
            cur->mVoice = voice;
            cur->mSourceID = source->id;
            cur->mState = VChangeState::Play;
            source->state = AL_PLAYING;
#if ALSOFT_EAX
            if(context->hasEax())
                source->eaxCommit();
#endif // ALSOFT_EAX
            return;

        case AL_PLAYING:
            /* A source that's already playing is restarted from the beginning.
             * Stop the current voice and start a new one so it properly cross-
             * fades back to the beginning.
             */
            if(voice)
                voice->mPendingChange.store(true, std::memory_order_relaxed);
            cur->mOldVoice = voice;
            voice = nullptr;
            break;

        default:
            Expects(voice == nullptr);
            cur->mOldVoice = nullptr;
#if ALSOFT_EAX
            if(context->hasEax())
                source->eaxCommit();
#endif // ALSOFT_EAX
            break;
        }

        /* Find the next unused voice to play this source with. */
        for(;voiceiter != voicelist.end();++voiceiter,++vidx)
        {
            Voice *v{*voiceiter};
            if(v->mPlayState.load(std::memory_order_acquire) == Voice::Stopped
                && v->mSourceID.load(std::memory_order_relaxed) == 0u
                && v->mPendingChange.load(std::memory_order_relaxed) == false)
            {
                voice = v;
                break;
            }
        }
        Ensures(voice != nullptr);

        voice->mPosition.store(0, std::memory_order_relaxed);
        voice->mPositionFrac.store(0, std::memory_order_relaxed);
        voice->mCurrentBuffer.store(&source->mQueue.front(), std::memory_order_relaxed);
        voice->mStartTime = start_time;
        voice->mFlags.reset();
        /* A source that's not playing or paused has any offset applied when it
         * starts playing.
         */
        if(const ALenum offsettype{source->OffsetType})
        {
            const double offset{source->Offset};
            source->OffsetType = AL_NONE;
            source->Offset = 0.0;
            if(auto vpos = GetSampleOffset(source->mQueue, offsettype, offset))
            {
                voice->mPosition.store(vpos->pos, std::memory_order_relaxed);
                voice->mPositionFrac.store(vpos->frac, std::memory_order_relaxed);
                voice->mCurrentBuffer.store(vpos->bufferitem, std::memory_order_relaxed);
                if(vpos->pos > 0 || (vpos->pos == 0 && vpos->frac > 0)
                    || vpos->bufferitem != &source->mQueue.front())
                    voice->mFlags.set(VoiceIsFading);
            }
        }
        InitVoice(voice, source, &*BufferList, context, device);

        source->VoiceIdx = vidx;
        source->state = AL_PLAYING;

        cur->mVoice = voice;
        cur->mSourceID = source->id;
        cur->mState = VChangeState::Play;
    });
    if(tail) [[likely]]
        SendVoiceChanges(context, tail);
}


void AL_APIENTRY alGenSources(gsl::not_null<ALCcontext*> context, ALsizei n, ALuint *sources)
    noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Generating {} sources", n);
    if(n <= 0) [[unlikely]] return;

    auto srclock = std::unique_lock{context->mSourceLock};
    auto *device = context->mALDevice.get();

    const auto sids = std::span{sources, gsl::narrow_cast<ALuint>(n)};
    if(context->mNumSources > device->SourcesMax
        || sids.size() > device->SourcesMax-context->mNumSources)
        context->throw_error(AL_OUT_OF_MEMORY, "Exceeding {} source limit ({} + {})",
            device->SourcesMax, context->mNumSources, n);
    if(!EnsureSources(context, sids.size()))
        context->throw_error(AL_OUT_OF_MEMORY, "Failed to allocate {} source{}", n,
            (n==1) ? "" : "s");

    std::ranges::generate(sids, [context]{ return AllocSource(context)->id; });
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alDeleteSources(gsl::not_null<ALCcontext*> context, ALsizei n,
    const ALuint *sources) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Deleting {} sources", n);
    if(n <= 0) [[unlikely]] return;

    auto srclock = std::lock_guard{context->mSourceLock};

    /* Check that all Sources are valid */
    const auto sids = std::span{sources, gsl::narrow_cast<ALuint>(n)};
    std::ranges::for_each(sids, [context](const ALuint sid)
    { std::ignore = LookupSource(context, sid); });

    /* All good. Delete source IDs. */
    std::ranges::for_each(sids, [context](const ALuint sid) -> void
    {
        if(auto *src = LookupSource(std::nothrow, context, sid))
            FreeSource(context, src);
    });
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

auto AL_APIENTRY alIsSource(gsl::not_null<ALCcontext*> context, ALuint source) noexcept
    -> ALboolean
{
    auto srclock = std::lock_guard{context->mSourceLock};
    if(LookupSource(std::nothrow, context, source) != nullptr)
        return AL_TRUE;
    return AL_FALSE;
}


void AL_APIENTRY alSourcef(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALfloat value) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    SetProperty<float>(LookupSource(context, source), context, SourceProp{param}, {&value, 1u});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSource3f(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALfloat value1, ALfloat value2, ALfloat value3) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    const auto fvals = std::array{value1, value2, value3};
    SetProperty<float>(LookupSource(context, source), context, SourceProp{param}, fvals);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourcefv(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    const ALfloat *values) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto count = FloatValsByProp(param);
    SetProperty(Source, context, SourceProp{param}, std::span{values, count});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alSourcedSOFT(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALdouble value) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    SetProperty<double>(LookupSource(context, source), context, SourceProp{param}, {&value, 1});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSource3dSOFT(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALdouble value1, ALdouble value2, ALdouble value3) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    const auto dvals = std::array{value1, value2, value3};
    SetProperty<double>(LookupSource(context, source), context, SourceProp{param}, dvals);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourcedvSOFT(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    const ALdouble *values) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto count = DoubleValsByProp(param);
    SetProperty(Source, context, SourceProp{param}, std::span{values, count});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alSourcei(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALint value) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    SetProperty<int>(LookupSource(context, source), context, SourceProp{param}, {&value, 1u});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSource3i(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALint value1, ALint value2, ALint value3) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    const auto ivals = std::array{value1, value2, value3};
    SetProperty<int>(LookupSource(context, source), context, SourceProp{param}, ivals);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourceiv(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    const ALint *values) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto count = IntValsByProp(param);
    SetProperty(Source, context, SourceProp{param}, std::span{values, count});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alSourcei64SOFT(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALint64SOFT value) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    SetProperty<int64_t>(LookupSource(context, source), context, SourceProp{param}, {&value, 1u});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSource3i64SOFT(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALint64SOFT value1, ALint64SOFT value2, ALint64SOFT value3) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    const auto i64vals = std::array{value1, value2, value3};
    SetProperty<int64_t>(LookupSource(context, source), context, SourceProp{param}, i64vals);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourcei64vSOFT(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    const ALint64SOFT *values) noexcept
try {
    auto proplock = std::lock_guard{context->mPropLock};
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto count = Int64ValsByProp(param);
    SetProperty(Source, context, SourceProp{param}, std::span{values, count});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alGetSourcef(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALfloat *value) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!value)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    GetProperty(Source, context, SourceProp{param}, std::span{value, 1u});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alGetSource3f(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALfloat *value1, ALfloat *value2, ALfloat *value3) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!(value1 && value2 && value3))
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    auto fvals = std::array<float,3>{};
    GetProperty<float>(Source, context, SourceProp{param}, fvals);
    *value1 = fvals[0];
    *value2 = fvals[1];
    *value3 = fvals[2];
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alGetSourcefv(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALfloat *values) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto count = FloatValsByProp(param);
    GetProperty(Source, context, SourceProp{param}, std::span{values, count});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alGetSourcedSOFT(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALdouble *value) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!value)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    GetProperty(Source, context, SourceProp{param}, std::span{value, 1u});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alGetSource3dSOFT(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALdouble *value1, ALdouble *value2, ALdouble *value3) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!(value1 && value2 && value3))
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    auto dvals = std::array<double,3>{};
    GetProperty<double>(Source, context, SourceProp{param}, dvals);
    *value1 = dvals[0];
    *value2 = dvals[1];
    *value3 = dvals[2];
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alGetSourcedvSOFT(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALdouble *values) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto count = DoubleValsByProp(param);
    GetProperty(Source, context, SourceProp{param}, std::span{values, count});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alGetSourcei(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALint *value) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!value)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    GetProperty(Source, context, SourceProp{param}, std::span{value, 1u});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alGetSource3i(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALint *value1, ALint *value2, ALint *value3) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!(value1 && value2 && value3))
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    auto ivals = std::array<int,3>{};
    GetProperty<int>(Source, context, SourceProp{param}, ivals);
    *value1 = ivals[0];
    *value2 = ivals[1];
    *value3 = ivals[2];
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alGetSourceiv(gsl::not_null<ALCcontext*> context, ALuint source, ALenum param,
    ALint *values) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto count = IntValsByProp(param);
    GetProperty(Source, context, SourceProp{param}, std::span{values, count});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alGetSourcei64SOFT(gsl::not_null<ALCcontext*> context, ALuint source,
    ALenum param, ALint64SOFT *value) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!value)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    GetProperty(Source, context, SourceProp{param}, std::span{value, 1u});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alGetSource3i64SOFT(gsl::not_null<ALCcontext*> context, ALuint source,
    ALenum param, ALint64SOFT *value1, ALint64SOFT *value2, ALint64SOFT *value3) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!(value1 && value2 && value3))
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    auto i64vals = std::array<int64_t,3>{};
    GetProperty<int64_t>(Source, context, SourceProp{param}, i64vals);
    *value1 = i64vals[0];
    *value2 = i64vals[1];
    *value3 = i64vals[2];
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alGetSourcei64vSOFT(gsl::not_null<ALCcontext*> context, ALuint source,
    ALenum param, ALint64SOFT *values) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto const Source = LookupSource(context, source);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto count = Int64ValsByProp(param);
    GetProperty(Source, context, SourceProp{param}, std::span{values, count});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alSourcePlayv(gsl::not_null<ALCcontext*> context, ALsizei n,
    const ALuint *sources) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Playing {} sources", n);
    if(n <= 0) [[unlikely]] return;

    const auto sids = std::span{sources, gsl::narrow_cast<ALuint>(n)};
    auto source_store = source_store_variant{};
    const auto srchandles = get_srchandles(source_store, sids.size());

    auto srclock = std::lock_guard{context->mSourceLock};
    std::ranges::transform(sids, srchandles.begin(), [context](const ALuint sid) -> ALsource*
    { return LookupSource(context, sid); });

    StartSources(context, srchandles);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourcePlay(gsl::not_null<ALCcontext*> context, ALuint source) noexcept
try {
    auto srclock = std::lock_guard{context->mSourceLock};

    auto *Source = LookupSource(context, source).get();
    StartSources(context, {&Source, 1});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourcePlayAtTimevSOFT(gsl::not_null<ALCcontext*> context, ALsizei n,
    const ALuint *sources, ALint64SOFT start_time) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Playing {} sources", n);
    if(n <= 0) [[unlikely]] return;

    if(start_time < 0)
        context->throw_error(AL_INVALID_VALUE, "Invalid time point {}", start_time);

    const auto sids = std::span{sources, gsl::narrow_cast<ALuint>(n)};
    auto source_store = source_store_variant{};
    const auto srchandles = get_srchandles(source_store, sids.size());

    const auto srclock = std::lock_guard{context->mSourceLock};
    std::ranges::transform(sids, srchandles.begin(), [context](const ALuint sid) -> ALsource*
    { return LookupSource(context, sid); });

    StartSources(context, srchandles, nanoseconds{start_time});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourcePlayAtTimeSOFT(gsl::not_null<ALCcontext*> context, ALuint source,
    ALint64SOFT start_time) noexcept
try {
    if(start_time < 0)
        context->throw_error(AL_INVALID_VALUE, "Invalid time point {}", start_time);

    auto srclock = std::lock_guard{context->mSourceLock};
    auto *Source = LookupSource(context, source).get();
    StartSources(context, {&Source, 1}, nanoseconds{start_time});
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alSourcePausev(gsl::not_null<ALCcontext*> context, ALsizei n,
    const ALuint *sources) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Pausing {} sources", n);
    if(n <= 0) [[unlikely]] return;

    const auto sids = std::span{sources, gsl::narrow_cast<ALuint>(n)};
    auto source_store = source_store_variant{};
    const auto srchandles = get_srchandles(source_store, sids.size());

    auto srclock = std::lock_guard{context->mSourceLock};
    std::ranges::transform(sids, srchandles.begin(), [context](const ALuint sid) -> ALsource*
    { return LookupSource(context, sid); });

    /* Pausing has to be done in two steps. First, for each source that's
     * detected to be playing, change the voice (asynchronously) to
     * stopping/paused.
     */
    auto tail = LPVoiceChange{};
    auto cur = LPVoiceChange{};
    std::ranges::for_each(srchandles, [context,&tail,&cur](ALsource *source)
    {
        auto *voice = GetSourceVoice(source, context);
        if(GetSourceState(source, voice) == AL_PLAYING)
        {
            if(!cur)
                cur = tail = GetVoiceChanger(context);
            else
            {
                cur->mNext.store(GetVoiceChanger(context), std::memory_order_relaxed);
                cur = cur->mNext.load(std::memory_order_relaxed);
            }
            cur->mVoice = voice;
            cur->mSourceID = source->id;
            cur->mState = VChangeState::Pause;
        }
    });
    if(tail) [[likely]]
    {
        SendVoiceChanges(context, tail);
        /* Second, now that the voice changes have been sent, because it's
         * possible that the voice stopped after it was detected playing and
         * before the voice got paused, recheck that the source is still
         * considered playing and set it to paused if so.
         */
        std::ranges::for_each(srchandles, [context](ALsource *source)
        {
            Voice *voice{GetSourceVoice(source, context)};
            if(GetSourceState(source, voice) == AL_PLAYING)
                source->state = AL_PAUSED;
        });
    }
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourcePause(gsl::not_null<ALCcontext*> context, ALuint source) noexcept
{ alSourcePausev(context, 1, &source); }


void AL_APIENTRY alSourceStopv(gsl::not_null<ALCcontext*> context, ALsizei n,
    const ALuint *sources) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Stopping {} sources", n);
    if(n <= 0) [[unlikely]] return;

    const auto sids = std::span{sources, gsl::narrow_cast<ALuint>(n)};
    auto source_store = source_store_variant{};
    const auto srchandles = get_srchandles(source_store, sids.size());

    auto srclock = std::lock_guard{context->mSourceLock};
    std::ranges::transform(sids, srchandles.begin(), [context](const ALuint sid) -> ALsource*
    { return LookupSource(context, sid); });

    auto tail = LPVoiceChange{};
    auto cur = LPVoiceChange{};
    std::ranges::for_each(srchandles, [context,&tail,&cur](ALsource *source)
    {
        if(auto *voice = GetSourceVoice(source, context))
        {
            if(!cur)
                cur = tail = GetVoiceChanger(context);
            else
            {
                cur->mNext.store(GetVoiceChanger(context), std::memory_order_relaxed);
                cur = cur->mNext.load(std::memory_order_relaxed);
            }
            voice->mPendingChange.store(true, std::memory_order_relaxed);
            cur->mVoice = voice;
            cur->mSourceID = source->id;
            cur->mState = VChangeState::Stop;
            source->state = AL_STOPPED;
        }
        source->Offset = 0.0;
        source->OffsetType = AL_NONE;
        source->VoiceIdx = InvalidVoiceIndex;
    });
    if(tail) [[likely]]
        SendVoiceChanges(context, tail);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourceStop(gsl::not_null<ALCcontext*> context, ALuint source) noexcept
{ alSourceStopv(context, 1, &source); }


void AL_APIENTRY alSourceRewindv(gsl::not_null<ALCcontext*> context, ALsizei n,
    const ALuint *sources) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Rewinding {} sources", n);
    if(n <= 0) [[unlikely]] return;

    const auto sids = std::span{sources, gsl::narrow_cast<ALuint>(n)};
    auto source_store = source_store_variant{};
    const auto srchandles = get_srchandles(source_store, sids.size());

    auto srclock = std::lock_guard{context->mSourceLock};
    std::ranges::transform(sids, srchandles.begin(), [context](const ALuint sid) -> ALsource*
    { return LookupSource(context, sid); });

    auto tail = LPVoiceChange{};
    auto cur = LPVoiceChange{};
    std::ranges::for_each(srchandles, [context,&tail,&cur](ALsource *source)
    {
        auto *voice = GetSourceVoice(source, context);
        if(source->state != AL_INITIAL)
        {
            if(!cur)
                cur = tail = GetVoiceChanger(context);
            else
            {
                cur->mNext.store(GetVoiceChanger(context), std::memory_order_relaxed);
                cur = cur->mNext.load(std::memory_order_relaxed);
            }
            if(voice)
                voice->mPendingChange.store(true, std::memory_order_relaxed);
            cur->mVoice = voice;
            cur->mSourceID = source->id;
            cur->mState = VChangeState::Reset;
            source->state = AL_INITIAL;
        }
        source->Offset = 0.0;
        source->OffsetType = AL_NONE;
        source->VoiceIdx = InvalidVoiceIndex;
    });
    if(tail) [[likely]]
        SendVoiceChanges(context, tail);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourceRewind(gsl::not_null<ALCcontext*> context, ALuint source) noexcept
{ alSourceRewindv(context, 1, &source); }


void AL_APIENTRY alSourceQueueBuffers(gsl::not_null<ALCcontext*> context, ALuint src, ALsizei nb,
    const ALuint *buffers) noexcept
try {
    if(nb < 0)
        context->throw_error(AL_INVALID_VALUE, "Queueing {} buffers", nb);
    if(nb <= 0) [[unlikely]] return;

    auto srclock = std::lock_guard{context->mSourceLock};
    auto const source = LookupSource(context, src);

    /* Can't queue on a Static Source */
    if(source->SourceType == AL_STATIC)
        context->throw_error(AL_INVALID_OPERATION, "Queueing onto static source {}", src);

    /* Check for a valid Buffer, for its frequency and format */
    auto *device = context->mALDevice.get();
    auto BufferFmt = std::invoke([source]() -> ALbuffer*
    {
        const auto iter = std::ranges::find_if(source->mQueue, HasBuffer);
        if(iter != source->mQueue.end())
            return iter->mBuffer.get();
        return nullptr;
    });

    auto buflock = std::unique_lock{device->BufferLock};
    const auto bids = std::span{buffers, gsl::narrow_cast<ALuint>(nb)};
    const auto NewListStart = std::ssize(source->mQueue);
    try {
        ALbufferQueueItem *BufferList{nullptr};
        std::ranges::for_each(bids,[context,source,&BufferFmt,&BufferList](const ALuint bid)
        {
            auto *buffer = bid ? LookupBuffer(context, bid).get() : nullptr;
            if(buffer)
            {
                if(buffer->mSampleRate < 1)
                    context->throw_error(AL_INVALID_OPERATION,
                        "Queueing buffer {} with no format", buffer->id);

                if(buffer->mCallback)
                    context->throw_error(AL_INVALID_OPERATION, "Queueing callback buffer {}",
                        buffer->id);

                if(buffer->MappedAccess != 0 && !(buffer->MappedAccess&AL_MAP_PERSISTENT_BIT_SOFT))
                    context->throw_error(AL_INVALID_OPERATION,
                        "Queueing non-persistently mapped buffer {}", buffer->id);
            }

            source->mQueue.emplace_back();
            if(!BufferList)
                BufferList = &source->mQueue.back();
            else
            {
                auto &item = source->mQueue.back();
                BufferList->mNext.store(&item, std::memory_order_relaxed);
                BufferList = &item;
            }
            if(!buffer) return;
            BufferList->mBuffer = buffer->newReference();
            BufferList->mBlockAlign = buffer->mBlockAlign;
            BufferList->mSampleLen = buffer->mSampleLen;
            BufferList->mLoopEnd = buffer->mSampleLen;
            BufferList->mSamples = buffer->mData;

            if(BufferFmt == nullptr)
                BufferFmt = buffer;
            else
            {
                auto fmt_mismatch = false;
                fmt_mismatch |= BufferFmt->mSampleRate != buffer->mSampleRate;
                fmt_mismatch |= BufferFmt->mChannels != buffer->mChannels;
                fmt_mismatch |= BufferFmt->mType != buffer->mType;
                if(BufferFmt->isBFormat())
                {
                    fmt_mismatch |= BufferFmt->mAmbiLayout != buffer->mAmbiLayout;
                    fmt_mismatch |= BufferFmt->mAmbiScaling != buffer->mAmbiScaling;
                }
                fmt_mismatch |= BufferFmt->mAmbiOrder != buffer->mAmbiOrder;
                if(fmt_mismatch)
                    context->throw_error(AL_INVALID_OPERATION,
                        "Queueing buffer with mismatched format\n"
                        "  Expected: {}hz, {}, {} ; Got: {}hz, {}, {}\n", BufferFmt->mSampleRate,
                        NameFromFormat(BufferFmt->mType), NameFromFormat(BufferFmt->mChannels),
                        buffer->mSampleRate, NameFromFormat(buffer->mType),
                        NameFromFormat(buffer->mChannels));
            }
        });
    }
    catch(...) {
        /* A buffer failed (invalid ID or format), or there was some other
         * unexpected error, so release the buffers we had.
         */
        source->mQueue.resize(gsl::narrow_cast<size_t>(NewListStart));
        throw;
    }
    /* All buffers good. */
    buflock.unlock();

    /* Source is now streaming */
    source->SourceType = AL_STREAMING;

    if(NewListStart > 0)
    {
        auto iter = std::next(source->mQueue.begin(), NewListStart);
        (iter-1)->mNext.store(&*iter, std::memory_order_release);
    }
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alSourceUnqueueBuffers(gsl::not_null<ALCcontext*> context, ALuint src, ALsizei nb,
    ALuint *buffers) noexcept
try {
    if(nb < 0)
        context->throw_error(AL_INVALID_VALUE, "Unqueueing {} buffers", nb);
    if(nb <= 0) [[unlikely]] return;

    auto srclock = std::lock_guard{context->mSourceLock};

    auto const source = LookupSource(context, src);
    if(source->SourceType != AL_STREAMING)
        context->throw_error(AL_INVALID_VALUE, "Unqueueing from a non-streaming source {}", src);
    if(source->Looping)
        context->throw_error(AL_INVALID_VALUE, "Unqueueing from looping source {}", src);

    /* Make sure enough buffers have been processed to unqueue. */
    const auto bids = std::span{buffers, gsl::narrow_cast<ALuint>(nb)};
    auto processed = 0_uz;
    if(source->state != AL_INITIAL) [[likely]]
    {
        const auto Current = std::invoke([source,context]() -> const VoiceBufferItem*
        {
            if(auto *voice = GetSourceVoice(source, context))
                return voice->mCurrentBuffer.load(std::memory_order_relaxed);
            return nullptr;
        });
        std::ignore = std::ranges::find_if(source->mQueue,
           [Current,&processed](const ALbufferQueueItem &item) noexcept -> bool
        {
            if(&item == Current)
                return true;
            ++processed;
            return false;
        });
    }
    if(processed < bids.size())
        context->throw_error(AL_INVALID_VALUE, "Unqueueing {} buffer{} (only {} processed)",
            nb, (nb==1) ? "" : "s", processed);

    std::ranges::generate(bids, [source]() noexcept -> ALuint
    {
        auto bid = 0u;
        if(auto *buffer = source->mQueue.front().mBuffer.get())
            bid = buffer->id;
        source->mQueue.pop_front();
        return bid;
    });
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

} // namespace

AL_API DECL_FUNC2(void, alGenSources, ALsizei,n, ALuint*,sources)
AL_API DECL_FUNC2(void, alDeleteSources, ALsizei,n, const ALuint*,sources)
AL_API DECL_FUNC1(ALboolean, alIsSource, ALuint,source)

AL_API DECL_FUNC3(void, alSourcef, ALuint,source, ALenum,param, ALfloat,value)
AL_API DECL_FUNC5(void, alSource3f, ALuint,source, ALenum,param, ALfloat,value1, ALfloat,value2, ALfloat,value3)
AL_API DECL_FUNC3(void, alSourcefv, ALuint,source, ALenum,param, const ALfloat*,values)

AL_API DECL_FUNCEXT3(void, alSourced,SOFT, ALuint,source, ALenum,param, ALdouble,value)
AL_API DECL_FUNCEXT5(void, alSource3d,SOFT, ALuint,source, ALenum,param, ALdouble,value1, ALdouble,value2, ALdouble,value3)
AL_API DECL_FUNCEXT3(void, alSourcedv,SOFT, ALuint,source, ALenum,param, const ALdouble*,values)

AL_API DECL_FUNC3(void, alSourcei, ALuint,source, ALenum,param, ALint,value)
AL_API DECL_FUNC5(void, alSource3i, ALuint,buffer, ALenum,param, ALint,value1, ALint,value2, ALint,value3)
AL_API DECL_FUNC3(void, alSourceiv, ALuint,source, ALenum,param, const ALint*,values)

AL_API DECL_FUNCEXT3(void, alSourcei64,SOFT, ALuint,source, ALenum,param, ALint64SOFT,value)
AL_API DECL_FUNCEXT5(void, alSource3i64,SOFT, ALuint,source, ALenum,param, ALint64SOFT,value1, ALint64SOFT,value2, ALint64SOFT,value3)
AL_API DECL_FUNCEXT3(void, alSourcei64v,SOFT, ALuint,source, ALenum,param, const ALint64SOFT*,values)

AL_API DECL_FUNC3(void, alGetSourcef, ALuint,source, ALenum,param, ALfloat*,value)
AL_API DECL_FUNC5(void, alGetSource3f, ALuint,source, ALenum,param, ALfloat*,value1, ALfloat*,value2, ALfloat*,value3)
AL_API DECL_FUNC3(void, alGetSourcefv, ALuint,source, ALenum,param, ALfloat*,values)

AL_API DECL_FUNCEXT3(void, alGetSourced,SOFT, ALuint,source, ALenum,param, ALdouble*,value)
AL_API DECL_FUNCEXT5(void, alGetSource3d,SOFT, ALuint,source, ALenum,param, ALdouble*,value1, ALdouble*,value2, ALdouble*,value3)
AL_API DECL_FUNCEXT3(void, alGetSourcedv,SOFT, ALuint,source, ALenum,param, ALdouble*,values)

AL_API DECL_FUNC3(void, alGetSourcei, ALuint,source, ALenum,param, ALint*,value)
AL_API DECL_FUNC5(void, alGetSource3i, ALuint,source, ALenum,param, ALint*,value1, ALint*,value2, ALint*,value3)
AL_API DECL_FUNC3(void, alGetSourceiv, ALuint,source, ALenum,param, ALint*,values)

AL_API DECL_FUNCEXT3(void, alGetSourcei64,SOFT, ALuint,source, ALenum,param, ALint64SOFT*,value)
AL_API DECL_FUNCEXT5(void, alGetSource3i64,SOFT, ALuint,source, ALenum,param, ALint64SOFT*,value1, ALint64SOFT*,value2, ALint64SOFT*,value3)
AL_API DECL_FUNCEXT3(void, alGetSourcei64v,SOFT, ALuint,source, ALenum,param, ALint64SOFT*,values)

AL_API DECL_FUNC1(void, alSourcePlay, ALuint,source)
FORCE_ALIGN DECL_FUNCEXT2(void, alSourcePlayAtTime,SOFT, ALuint,source, ALint64SOFT,start_time)
AL_API DECL_FUNC2(void, alSourcePlayv, ALsizei,n, const ALuint*,sources)
FORCE_ALIGN DECL_FUNCEXT3(void, alSourcePlayAtTimev,SOFT, ALsizei,n, const ALuint*,sources, ALint64SOFT,start_time)

AL_API DECL_FUNC1(void, alSourcePause, ALuint,source)
AL_API DECL_FUNC2(void, alSourcePausev, ALsizei,n, const ALuint*,sources)

AL_API DECL_FUNC1(void, alSourceStop, ALuint,source)
AL_API DECL_FUNC2(void, alSourceStopv, ALsizei,n, const ALuint*,sources)

AL_API DECL_FUNC1(void, alSourceRewind, ALuint,source)
AL_API DECL_FUNC2(void, alSourceRewindv, ALsizei,n, const ALuint*,sources)

AL_API DECL_FUNC3(void, alSourceQueueBuffers, ALuint,source, ALsizei,nb, const ALuint*,buffers)
AL_API DECL_FUNC3(void, alSourceUnqueueBuffers, ALuint,source, ALsizei,nb, ALuint*,buffers)


AL_API void AL_APIENTRY alSourceQueueBufferLayersSOFT(ALuint, ALsizei, const ALuint*) noexcept
{
    const auto context = GetContextRef();
    if(!context) [[unlikely]] return;

    context->setError(AL_INVALID_OPERATION, "alSourceQueueBufferLayersSOFT not supported");
}


ALsource::ALsource() noexcept
{
    Direct.Gain = 1.0f;
    Direct.GainHF = 1.0f;
    Direct.HFReference = LowPassFreqRef;
    Direct.GainLF = 1.0f;
    Direct.LFReference = HighPassFreqRef;
    Send.fill(SendData{.mSlot={}, .mGain=1.0f,
        .mGainHF=1.0f, .mHFReference=LowPassFreqRef,
        .mGainLF=1.0f, .mLFReference=HighPassFreqRef});
}

ALsource::~ALsource() = default;


void UpdateAllSourceProps(ALCcontext *context)
{
    auto srclock = std::lock_guard{context->mSourceLock};
    auto voicelist = context->getVoicesSpan();
    auto vidx = 0u;
    for(Voice *voice : voicelist)
    {
        auto sid = voice->mSourceID.load(std::memory_order_acquire);
        auto *source = sid ? LookupSource(std::nothrow, context, sid) : nullptr;
        if(source && source->VoiceIdx == vidx)
        {
            if(std::exchange(source->mPropsDirty, false))
                UpdateSourceProps(source, voice, context);
        }
        ++vidx;
    }
}

void ALsource::SetName(gsl::not_null<ALCcontext*> context, ALuint id, std::string_view name)
{
    auto srclock = std::lock_guard{context->mSourceLock};

    std::ignore = LookupSource(context, id);
    context->mSourceNames.insert_or_assign(id, name);
}


SourceSubList::~SourceSubList()
{
    if(!Sources)
        return;

    auto usemask = ~FreeMask;
    while(usemask)
    {
        const auto idx = std::countr_zero(usemask);
        usemask &= ~(1_u64 << idx);
        std::destroy_at(std::to_address(Sources->begin() + idx));
    }
    FreeMask = ~usemask;
    SubListAllocator{}.deallocate(Sources, 1);
    Sources = nullptr;
}


#if ALSOFT_EAX
void ALsource::eaxInitialize(gsl::not_null<ALCcontext*> context) noexcept
{
    mEaxAlContext = context;

    mEaxPrimaryFxSlotId = context->eaxGetPrimaryFxSlotIndex();
    eax_set_defaults();

    eax1_translate(mEax1.i, mEax);
    mEaxVersion = 1;
    mEaxChanged = true;
}

ALsource* ALsource::EaxLookupSource(ALCcontext& al_context, ALuint source_id) noexcept
{
    return LookupSource(std::nothrow, &al_context, source_id);
}

[[noreturn]]
void ALsource::eax_fail(const std::string_view message) { throw Exception{message}; }

[[noreturn]]
void ALsource::eax_fail_unknown_property_id() { eax_fail("Unknown property id."); }

[[noreturn]]
void ALsource::eax_fail_unknown_version() { eax_fail("Unknown version."); }

[[noreturn]]
void ALsource::eax_fail_unknown_active_fx_slot_id() { eax_fail("Unknown active FX slot ID."); }

[[noreturn]]
void ALsource::eax_fail_unknown_receiving_fx_slot_id() {eax_fail("Unknown receiving FX slot ID.");}

void ALsource::eax_set_sends_defaults(EaxSends& sends, const EaxFxSlotIds& ids) noexcept
{
    for(size_t i{0};i < EAX_MAX_FXSLOTS;++i)
    {
        auto& send = sends[i];
        send.guidReceivingFXSlotID = *(ids[i]);
        send.mSend.lSend = EAXSOURCE_DEFAULTSEND;
        send.mSend.lSendHF = EAXSOURCE_DEFAULTSENDHF;
        send.mOcclusion.lOcclusion = EAXSOURCE_DEFAULTOCCLUSION;
        send.mOcclusion.flOcclusionLFRatio = EAXSOURCE_DEFAULTOCCLUSIONLFRATIO;
        send.mOcclusion.flOcclusionRoomRatio = EAXSOURCE_DEFAULTOCCLUSIONROOMRATIO;
        send.mOcclusion.flOcclusionDirectRatio = EAXSOURCE_DEFAULTOCCLUSIONDIRECTRATIO;
        send.mExclusion.lExclusion = EAXSOURCE_DEFAULTEXCLUSION;
        send.mExclusion.flExclusionLFRatio = EAXSOURCE_DEFAULTEXCLUSIONLFRATIO;
    }
}

void ALsource::eax1_set_defaults(EAXBUFFER_REVERBPROPERTIES& props) noexcept
{
    props.fMix = EAX_REVERBMIX_USEDISTANCE;
}

void ALsource::eax1_set_defaults() noexcept
{
    eax1_set_defaults(mEax1.i);
    mEax1.d = mEax1.i;
}

void ALsource::eax2_set_defaults(EAX20BUFFERPROPERTIES& props) noexcept
{
    props.lDirect = EAXSOURCE_DEFAULTDIRECT;
    props.lDirectHF = EAXSOURCE_DEFAULTDIRECTHF;
    props.lRoom = EAXSOURCE_DEFAULTROOM;
    props.lRoomHF = EAXSOURCE_DEFAULTROOMHF;
    props.flRoomRolloffFactor = EAXSOURCE_DEFAULTROOMROLLOFFFACTOR;
    props.lObstruction = EAXSOURCE_DEFAULTOBSTRUCTION;
    props.flObstructionLFRatio = EAXSOURCE_DEFAULTOBSTRUCTIONLFRATIO;
    props.lOcclusion = EAXSOURCE_DEFAULTOCCLUSION;
    props.flOcclusionLFRatio = EAXSOURCE_DEFAULTOCCLUSIONLFRATIO;
    props.flOcclusionRoomRatio = EAXSOURCE_DEFAULTOCCLUSIONROOMRATIO;
    props.lOutsideVolumeHF = EAXSOURCE_DEFAULTOUTSIDEVOLUMEHF;
    props.flAirAbsorptionFactor = EAXSOURCE_DEFAULTAIRABSORPTIONFACTOR;
    props.dwFlags = EAXSOURCE_DEFAULTFLAGS;
}

void ALsource::eax2_set_defaults() noexcept
{
    eax2_set_defaults(mEax2.i);
    mEax2.d = mEax2.i;
}

void ALsource::eax3_set_defaults(EAX30SOURCEPROPERTIES& props) noexcept
{
    props.lDirect = EAXSOURCE_DEFAULTDIRECT;
    props.lDirectHF = EAXSOURCE_DEFAULTDIRECTHF;
    props.lRoom = EAXSOURCE_DEFAULTROOM;
    props.lRoomHF = EAXSOURCE_DEFAULTROOMHF;
    props.mObstruction.lObstruction = EAXSOURCE_DEFAULTOBSTRUCTION;
    props.mObstruction.flObstructionLFRatio = EAXSOURCE_DEFAULTOBSTRUCTIONLFRATIO;
    props.mOcclusion.lOcclusion = EAXSOURCE_DEFAULTOCCLUSION;
    props.mOcclusion.flOcclusionLFRatio = EAXSOURCE_DEFAULTOCCLUSIONLFRATIO;
    props.mOcclusion.flOcclusionRoomRatio = EAXSOURCE_DEFAULTOCCLUSIONROOMRATIO;
    props.mOcclusion.flOcclusionDirectRatio = EAXSOURCE_DEFAULTOCCLUSIONDIRECTRATIO;
    props.mExclusion.lExclusion = EAXSOURCE_DEFAULTEXCLUSION;
    props.mExclusion.flExclusionLFRatio = EAXSOURCE_DEFAULTEXCLUSIONLFRATIO;
    props.lOutsideVolumeHF = EAXSOURCE_DEFAULTOUTSIDEVOLUMEHF;
    props.flDopplerFactor = EAXSOURCE_DEFAULTDOPPLERFACTOR;
    props.flRolloffFactor = EAXSOURCE_DEFAULTROLLOFFFACTOR;
    props.flRoomRolloffFactor = EAXSOURCE_DEFAULTROOMROLLOFFFACTOR;
    props.flAirAbsorptionFactor = EAXSOURCE_DEFAULTAIRABSORPTIONFACTOR;
    props.ulFlags = EAXSOURCE_DEFAULTFLAGS;
}

void ALsource::eax3_set_defaults() noexcept
{
    eax3_set_defaults(mEax3.i);
    mEax3.d = mEax3.i;
}

void ALsource::eax4_set_sends_defaults(EaxSends& sends) noexcept
{
    eax_set_sends_defaults(sends, eax4_fx_slot_ids);
}

void ALsource::eax4_set_active_fx_slots_defaults(EAX40ACTIVEFXSLOTS& slots) noexcept
{
    slots = EAX40SOURCE_DEFAULTACTIVEFXSLOTID;
}

void ALsource::eax4_set_defaults() noexcept
{
    eax3_set_defaults(mEax4.i.source);
    eax4_set_sends_defaults(mEax4.i.sends);
    eax4_set_active_fx_slots_defaults(mEax4.i.active_fx_slots);
    mEax4.d = mEax4.i;
}

void ALsource::eax5_set_source_defaults(EAX50SOURCEPROPERTIES& props) noexcept
{
    eax3_set_defaults(static_cast<EAX30SOURCEPROPERTIES&>(props));
    props.flMacroFXFactor = EAXSOURCE_DEFAULTMACROFXFACTOR;
}

void ALsource::eax5_set_sends_defaults(EaxSends& sends) noexcept
{
    eax_set_sends_defaults(sends, eax5_fx_slot_ids);
}

void ALsource::eax5_set_active_fx_slots_defaults(EAX50ACTIVEFXSLOTS& slots) noexcept
{
    slots = EAX50SOURCE_3DDEFAULTACTIVEFXSLOTID;
}

void ALsource::eax5_set_speaker_levels_defaults(EaxSpeakerLevels& speaker_levels) noexcept
{
    for(size_t i{0};i < eax_max_speakers;++i)
    {
        auto& speaker_level = speaker_levels[i];
        speaker_level.lSpeakerID = gsl::narrow_cast<long>(EAXSPEAKER_FRONT_LEFT + i);
        speaker_level.lLevel = EAXSOURCE_DEFAULTSPEAKERLEVEL;
    }
}

void ALsource::eax5_set_defaults(Eax5Props& props) noexcept
{
    eax5_set_source_defaults(props.source);
    eax5_set_sends_defaults(props.sends);
    eax5_set_active_fx_slots_defaults(props.active_fx_slots);
    eax5_set_speaker_levels_defaults(props.speaker_levels);
}

void ALsource::eax5_set_defaults() noexcept
{
    eax5_set_defaults(mEax5.i);
    mEax5.d = mEax5.i;
}

void ALsource::eax_set_defaults() noexcept
{
    eax1_set_defaults();
    eax2_set_defaults();
    eax3_set_defaults();
    eax4_set_defaults();
    eax5_set_defaults();
}

void ALsource::eax1_translate(const EAXBUFFER_REVERBPROPERTIES& src, Eax5Props& dst) noexcept
{
    eax5_set_defaults(dst);

    if (src.fMix == EAX_REVERBMIX_USEDISTANCE)
    {
        dst.source.ulFlags |= EAXSOURCEFLAGS_ROOMAUTO;
        dst.sends[0].mSend.lSend = 0;
    }
    else
    {
        dst.source.ulFlags &= ~EAXSOURCEFLAGS_ROOMAUTO;
        dst.sends[0].mSend.lSend = std::clamp(gsl::narrow_cast<long>(gain_to_level_mb(src.fMix)),
            EAXSOURCE_MINSEND, EAXSOURCE_MAXSEND);
    }
}

void ALsource::eax2_translate(const EAX20BUFFERPROPERTIES& src, Eax5Props& dst) noexcept
{
    // Source.
    //
    dst.source.lDirect = src.lDirect;
    dst.source.lDirectHF = src.lDirectHF;
    dst.source.lRoom = src.lRoom;
    dst.source.lRoomHF = src.lRoomHF;
    dst.source.mObstruction.lObstruction = src.lObstruction;
    dst.source.mObstruction.flObstructionLFRatio = src.flObstructionLFRatio;
    dst.source.mOcclusion.lOcclusion = src.lOcclusion;
    dst.source.mOcclusion.flOcclusionLFRatio = src.flOcclusionLFRatio;
    dst.source.mOcclusion.flOcclusionRoomRatio = src.flOcclusionRoomRatio;
    dst.source.mOcclusion.flOcclusionDirectRatio = EAXSOURCE_DEFAULTOCCLUSIONDIRECTRATIO;
    dst.source.mExclusion.lExclusion = EAXSOURCE_DEFAULTEXCLUSION;
    dst.source.mExclusion.flExclusionLFRatio = EAXSOURCE_DEFAULTEXCLUSIONLFRATIO;
    dst.source.lOutsideVolumeHF = src.lOutsideVolumeHF;
    dst.source.flDopplerFactor = EAXSOURCE_DEFAULTDOPPLERFACTOR;
    dst.source.flRolloffFactor = EAXSOURCE_DEFAULTROLLOFFFACTOR;
    dst.source.flRoomRolloffFactor = src.flRoomRolloffFactor;
    dst.source.flAirAbsorptionFactor = src.flAirAbsorptionFactor;
    dst.source.ulFlags = src.dwFlags;
    dst.source.flMacroFXFactor = EAXSOURCE_DEFAULTMACROFXFACTOR;

    // Set everything else to defaults.
    //
    eax5_set_sends_defaults(dst.sends);
    eax5_set_active_fx_slots_defaults(dst.active_fx_slots);
    eax5_set_speaker_levels_defaults(dst.speaker_levels);
}

void ALsource::eax3_translate(const EAX30SOURCEPROPERTIES& src, Eax5Props& dst) noexcept
{
    // Source.
    //
    static_cast<EAX30SOURCEPROPERTIES&>(dst.source) = src;
    dst.source.flMacroFXFactor = EAXSOURCE_DEFAULTMACROFXFACTOR;

    // Set everything else to defaults.
    //
    eax5_set_sends_defaults(dst.sends);
    eax5_set_active_fx_slots_defaults(dst.active_fx_slots);
    eax5_set_speaker_levels_defaults(dst.speaker_levels);
}

void ALsource::eax4_translate(const Eax4Props& src, Eax5Props& dst) noexcept
{
    // Source.
    //
    static_cast<EAX30SOURCEPROPERTIES&>(dst.source) = src.source;
    dst.source.flMacroFXFactor = EAXSOURCE_DEFAULTMACROFXFACTOR;

    // Sends.
    //
    dst.sends = src.sends;

    for(size_t i{0};i < EAX_MAX_FXSLOTS;++i)
        dst.sends[i].guidReceivingFXSlotID = *(eax5_fx_slot_ids[i]);

    // Active FX slots.
    //
    const auto src_slots = std::span{src.active_fx_slots.guidActiveFXSlots};
    const auto dst_slots = std::span{dst.active_fx_slots.guidActiveFXSlots};
    auto dstiter = std::ranges::transform(src_slots, dst_slots.begin(), [](const GUID &src_id)
        -> GUID
    {
        if(src_id == EAX_NULL_GUID)
            return EAX_NULL_GUID;
        if(src_id == EAX_PrimaryFXSlotID)
            return EAX_PrimaryFXSlotID;
        if(src_id == EAXPROPERTYID_EAX40_FXSlot0)
            return EAXPROPERTYID_EAX50_FXSlot0;
        if(src_id == EAXPROPERTYID_EAX40_FXSlot1)
            return EAXPROPERTYID_EAX50_FXSlot1;
        if(src_id == EAXPROPERTYID_EAX40_FXSlot2)
            return EAXPROPERTYID_EAX50_FXSlot2;
        if(src_id == EAXPROPERTYID_EAX40_FXSlot3)
            return EAXPROPERTYID_EAX50_FXSlot3;

        [[unlikely]]
        ERR("Unexpected active FX slot ID");
        return EAX_NULL_GUID;
    }).out;
    std::ranges::fill(dstiter, dst_slots.end(), EAX_NULL_GUID);

    // Speaker levels.
    //
    eax5_set_speaker_levels_defaults(dst.speaker_levels);
}

auto ALsource::eax_calculate_dst_occlusion_mb(long src_occlusion_mb, float path_ratio,
    float lf_ratio) noexcept -> float
{
    const auto ratio_1 = path_ratio + lf_ratio - 1.0f;
    const auto ratio_2 = path_ratio * lf_ratio;
    return gsl::narrow_cast<float>(src_occlusion_mb) * std::max(ratio_2, ratio_1);
}

auto ALsource::eax_create_direct_filter_param() const noexcept -> EaxAlLowPassParam
{
    const auto &source = mEax.source;

    auto gain_mb = gsl::narrow_cast<float>(source.mObstruction.lObstruction)
        * source.mObstruction.flObstructionLFRatio;
    auto gainhf_mb = gsl::narrow_cast<float>(source.mObstruction.lObstruction);

    for(const auto i : std::views::iota(0_uz, size_t{EAX_MAX_FXSLOTS}))
    {
        if(!mEaxActiveFxSlots.test(i))
            continue;

        const auto &fx_slot = mEaxAlContext->eaxGetFxSlot(i);
        const auto &fx_slot_eax = fx_slot.eax_get_eax_fx_slot();
        if(!(fx_slot_eax.ulFlags&EAXFXSLOTFLAGS_ENVIRONMENT))
            continue;

        if(mEaxPrimaryFxSlotId.value_or(-1) == fx_slot.eax_get_index()
            && source.mOcclusion.lOcclusion != 0)
        {
            gain_mb += eax_calculate_dst_occlusion_mb(source.mOcclusion.lOcclusion,
                source.mOcclusion.flOcclusionDirectRatio, source.mOcclusion.flOcclusionLFRatio);

            gainhf_mb += gsl::narrow_cast<float>(source.mOcclusion.lOcclusion)
                * source.mOcclusion.flOcclusionDirectRatio;
        }

        const auto &send = mEax.sends[i];
        if(send.mOcclusion.lOcclusion != 0)
        {
            gain_mb += eax_calculate_dst_occlusion_mb(send.mOcclusion.lOcclusion,
                send.mOcclusion.flOcclusionDirectRatio, send.mOcclusion.flOcclusionLFRatio);

            gainhf_mb += gsl::narrow_cast<float>(send.mOcclusion.lOcclusion)
                * send.mOcclusion.flOcclusionDirectRatio;
        }
    }

    /* gainhf_mb is the absolute mBFS of the filter's high-frequency volume,
     * and gain_mb is the absolute mBFS of the filter's low-frequency volume.
     * Adjust the HF volume to be relative to the LF volume, to make the
     * appropriate main and relative HF filter volumes.
     *
     * Also add the Direct and DirectHF properties to the filter, which are
     * already the main and relative HF volumes.
     */
    gainhf_mb -= gain_mb;

    gain_mb += gsl::narrow_cast<float>(source.lDirect);
    gainhf_mb += gsl::narrow_cast<float>(source.lDirectHF);

    return EaxAlLowPassParam{level_mb_to_gain(gain_mb), level_mb_to_gain(gainhf_mb)};
}

auto ALsource::eax_create_room_filter_param(const ALeffectslot &fx_slot,
    const EAXSOURCEALLSENDPROPERTIES& send) const noexcept -> EaxAlLowPassParam
{
    auto gain_mb = 0.0f;
    auto gainhf_mb = 0.0f;

    const auto &fx_slot_eax = fx_slot.eax_get_eax_fx_slot();
    if((fx_slot_eax.ulFlags & EAXFXSLOTFLAGS_ENVIRONMENT) != 0)
    {
        gain_mb += gsl::narrow_cast<float>(fx_slot_eax.lOcclusion)*fx_slot_eax.flOcclusionLFRatio
            + eax_calculate_dst_occlusion_mb(send.mOcclusion.lOcclusion,
                send.mOcclusion.flOcclusionRoomRatio, send.mOcclusion.flOcclusionLFRatio)
            + gsl::narrow_cast<float>(send.mExclusion.lExclusion)
                *send.mExclusion.flExclusionLFRatio;

        gainhf_mb += gsl::narrow_cast<float>(fx_slot_eax.lOcclusion)
            + gsl::narrow_cast<float>(send.mOcclusion.lOcclusion)
                *send.mOcclusion.flOcclusionRoomRatio
            + gsl::narrow_cast<float>(send.mExclusion.lExclusion);

        const auto &source = mEax.source;
        if(mEaxPrimaryFxSlotId.value_or(-1) == fx_slot.eax_get_index())
        {
            gain_mb += eax_calculate_dst_occlusion_mb(source.mOcclusion.lOcclusion,
                source.mOcclusion.flOcclusionRoomRatio, source.mOcclusion.flOcclusionLFRatio);
            gain_mb += gsl::narrow_cast<float>(source.mExclusion.lExclusion)
                * source.mExclusion.flExclusionLFRatio;

            gainhf_mb += gsl::narrow_cast<float>(source.mOcclusion.lOcclusion)
                * source.mOcclusion.flOcclusionRoomRatio;
            gainhf_mb += gsl::narrow_cast<float>(source.mExclusion.lExclusion);
        }

        gainhf_mb -= gain_mb;

        gain_mb += gsl::narrow_cast<float>(source.lRoom);
        gainhf_mb += gsl::narrow_cast<float>(source.lRoomHF);
    }

    gain_mb += gsl::narrow_cast<float>(send.mSend.lSend);
    gainhf_mb += gsl::narrow_cast<float>(send.mSend.lSendHF);

    return EaxAlLowPassParam{level_mb_to_gain(gain_mb), level_mb_to_gain(gainhf_mb)};
}

void ALsource::eax_update_direct_filter()
{
    const auto& direct_param = eax_create_direct_filter_param();
    Direct.Gain = direct_param.gain;
    Direct.GainHF = direct_param.gain_hf;
    Direct.HFReference = LowPassFreqRef;
    Direct.GainLF = 1.0f;
    Direct.LFReference = HighPassFreqRef;
    mPropsDirty = true;
}

void ALsource::eax_update_room_filters()
{
    for(const auto i : std::views::iota(0_uz, size_t{EAX_MAX_FXSLOTS}))
    {
        if(!mEaxActiveFxSlots.test(i))
            continue;

        auto &fx_slot = mEaxAlContext->eaxGetFxSlot(i);
        const auto &send = mEax.sends[i];
        const auto &room_param = eax_create_room_filter_param(fx_slot, send);
        eax_set_al_source_send(fx_slot.newReference(), i, room_param);
    }
}

void ALsource::eax_set_efx_outer_gain_hf()
{
    OuterGainHF = std::clamp(
        level_mb_to_gain(gsl::narrow_cast<float>(mEax.source.lOutsideVolumeHF)),
        AL_MIN_CONE_OUTER_GAINHF,
        AL_MAX_CONE_OUTER_GAINHF);
}

void ALsource::eax_set_efx_doppler_factor()
{
    DopplerFactor = mEax.source.flDopplerFactor;
}

void ALsource::eax_set_efx_rolloff_factor()
{
    RolloffFactor2 = mEax.source.flRolloffFactor;
}

void ALsource::eax_set_efx_room_rolloff_factor()
{
    RoomRolloffFactor = mEax.source.flRoomRolloffFactor;
}

void ALsource::eax_set_efx_air_absorption_factor()
{
    AirAbsorptionFactor = mEax.source.flAirAbsorptionFactor;
}

void ALsource::eax_set_efx_dry_gain_hf_auto()
{
    DryGainHFAuto = ((mEax.source.ulFlags & EAXSOURCEFLAGS_DIRECTHFAUTO) != 0);
}

void ALsource::eax_set_efx_wet_gain_auto()
{
    WetGainAuto = ((mEax.source.ulFlags & EAXSOURCEFLAGS_ROOMAUTO) != 0);
}

void ALsource::eax_set_efx_wet_gain_hf_auto()
{
    WetGainHFAuto = ((mEax.source.ulFlags & EAXSOURCEFLAGS_ROOMHFAUTO) != 0);
}

void ALsource::eax1_set(const EaxCall& call, EAXBUFFER_REVERBPROPERTIES& props)
{
    switch(call.get_property_id())
    {
        case DSPROPERTY_EAXBUFFER_ALL:
            eax_defer(call, props, Eax1SourceAllValidator{});
            break;

        case DSPROPERTY_EAXBUFFER_REVERBMIX:
            eax_defer(call, props.fMix, Eax1SourceReverbMixValidator{});
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax2_set(const EaxCall& call, EAX20BUFFERPROPERTIES& props)
{
    switch(call.get_property_id())
    {
        case DSPROPERTY_EAX20BUFFER_NONE:
            break;

        case DSPROPERTY_EAX20BUFFER_ALLPARAMETERS:
            eax_defer(call, props, Eax2SourceAllValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_DIRECT:
            eax_defer(call, props.lDirect, Eax2SourceDirectValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_DIRECTHF:
            eax_defer(call, props.lDirectHF, Eax2SourceDirectHfValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_ROOM:
            eax_defer(call, props.lRoom, Eax2SourceRoomValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_ROOMHF:
            eax_defer(call, props.lRoomHF, Eax2SourceRoomHfValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
            eax_defer(call, props.flRoomRolloffFactor, Eax2SourceRoomRolloffFactorValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
            eax_defer(call, props.lObstruction, Eax2SourceObstructionValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
            eax_defer(call, props.flObstructionLFRatio, Eax2SourceObstructionLfRatioValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_OCCLUSION:
            eax_defer(call, props.lOcclusion, Eax2SourceOcclusionValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
            eax_defer(call, props.flOcclusionLFRatio, Eax2SourceOcclusionLfRatioValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
            eax_defer(call, props.flOcclusionRoomRatio, Eax2SourceOcclusionRoomRatioValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
            eax_defer(call, props.lOutsideVolumeHF, Eax2SourceOutsideVolumeHfValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
            eax_defer(call, props.flAirAbsorptionFactor, Eax2SourceAirAbsorptionFactorValidator{});
            break;

        case DSPROPERTY_EAX20BUFFER_FLAGS:
            eax_defer(call, props.dwFlags, Eax2SourceFlagsValidator{});
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax3_set(const EaxCall& call, EAX30SOURCEPROPERTIES& props)
{
    switch(call.get_property_id())
    {
        case EAXSOURCE_NONE:
            break;

        case EAXSOURCE_ALLPARAMETERS:
            eax_defer(call, props, Eax3SourceAllValidator{});
            break;

        case EAXSOURCE_OBSTRUCTIONPARAMETERS:
            eax_defer(call, props.mObstruction, Eax4ObstructionValidator{});
            break;

        case EAXSOURCE_OCCLUSIONPARAMETERS:
            eax_defer(call, props.mOcclusion, Eax4OcclusionValidator{});
            break;

        case EAXSOURCE_EXCLUSIONPARAMETERS:
            eax_defer(call, props.mExclusion, Eax4ExclusionValidator{});
            break;

        case EAXSOURCE_DIRECT:
            eax_defer(call, props.lDirect, Eax2SourceDirectValidator{});
            break;

        case EAXSOURCE_DIRECTHF:
            eax_defer(call, props.lDirectHF, Eax2SourceDirectHfValidator{});
            break;

        case EAXSOURCE_ROOM:
            eax_defer(call, props.lRoom, Eax2SourceRoomValidator{});
            break;

        case EAXSOURCE_ROOMHF:
            eax_defer(call, props.lRoomHF, Eax2SourceRoomHfValidator{});
            break;

        case EAXSOURCE_OBSTRUCTION:
            eax_defer(call, props.mObstruction.lObstruction, Eax2SourceObstructionValidator{});
            break;

        case EAXSOURCE_OBSTRUCTIONLFRATIO:
            eax_defer(call, props.mObstruction.flObstructionLFRatio,
                Eax2SourceObstructionLfRatioValidator{});
            break;

        case EAXSOURCE_OCCLUSION:
            eax_defer(call, props.mOcclusion.lOcclusion, Eax2SourceOcclusionValidator{});
            break;

        case EAXSOURCE_OCCLUSIONLFRATIO:
            eax_defer(call, props.mOcclusion.flOcclusionLFRatio,
                Eax2SourceOcclusionLfRatioValidator{});
            break;

        case EAXSOURCE_OCCLUSIONROOMRATIO:
            eax_defer(call, props.mOcclusion.flOcclusionRoomRatio,
                Eax2SourceOcclusionRoomRatioValidator{});
            break;

        case EAXSOURCE_OCCLUSIONDIRECTRATIO:
            eax_defer(call, props.mOcclusion.flOcclusionDirectRatio,
                Eax3SourceOcclusionDirectRatioValidator{});
            break;

        case EAXSOURCE_EXCLUSION:
            eax_defer(call, props.mExclusion.lExclusion, Eax3SourceExclusionValidator{});
            break;

        case EAXSOURCE_EXCLUSIONLFRATIO:
            eax_defer(call, props.mExclusion.flExclusionLFRatio,
                Eax3SourceExclusionLfRatioValidator{});
            break;

        case EAXSOURCE_OUTSIDEVOLUMEHF:
            eax_defer(call, props.lOutsideVolumeHF, Eax2SourceOutsideVolumeHfValidator{});
            break;

        case EAXSOURCE_DOPPLERFACTOR:
            eax_defer(call, props.flDopplerFactor, Eax3SourceDopplerFactorValidator{});
            break;

        case EAXSOURCE_ROLLOFFFACTOR:
            eax_defer(call, props.flRolloffFactor, Eax3SourceRolloffFactorValidator{});
            break;

        case EAXSOURCE_ROOMROLLOFFFACTOR:
            eax_defer(call, props.flRoomRolloffFactor, Eax2SourceRoomRolloffFactorValidator{});
            break;

        case EAXSOURCE_AIRABSORPTIONFACTOR:
            eax_defer(call, props.flAirAbsorptionFactor, Eax2SourceAirAbsorptionFactorValidator{});
            break;

        case EAXSOURCE_FLAGS:
            eax_defer(call, props.ulFlags, Eax2SourceFlagsValidator{});
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax4_set(const EaxCall& call, Eax4Props& props)
{
    switch(call.get_property_id())
    {
        case EAXSOURCE_NONE:
        case EAXSOURCE_ALLPARAMETERS:
        case EAXSOURCE_OBSTRUCTIONPARAMETERS:
        case EAXSOURCE_OCCLUSIONPARAMETERS:
        case EAXSOURCE_EXCLUSIONPARAMETERS:
        case EAXSOURCE_DIRECT:
        case EAXSOURCE_DIRECTHF:
        case EAXSOURCE_ROOM:
        case EAXSOURCE_ROOMHF:
        case EAXSOURCE_OBSTRUCTION:
        case EAXSOURCE_OBSTRUCTIONLFRATIO:
        case EAXSOURCE_OCCLUSION:
        case EAXSOURCE_OCCLUSIONLFRATIO:
        case EAXSOURCE_OCCLUSIONROOMRATIO:
        case EAXSOURCE_OCCLUSIONDIRECTRATIO:
        case EAXSOURCE_EXCLUSION:
        case EAXSOURCE_EXCLUSIONLFRATIO:
        case EAXSOURCE_OUTSIDEVOLUMEHF:
        case EAXSOURCE_DOPPLERFACTOR:
        case EAXSOURCE_ROLLOFFFACTOR:
        case EAXSOURCE_ROOMROLLOFFFACTOR:
        case EAXSOURCE_AIRABSORPTIONFACTOR:
        case EAXSOURCE_FLAGS:
            eax3_set(call, props.source);
            break;

        case EAXSOURCE_SENDPARAMETERS:
            eax4_defer_sends<EAXSOURCESENDPROPERTIES>(call, props.sends, Eax4SendValidator{});
            break;

        case EAXSOURCE_ALLSENDPARAMETERS:
            eax4_defer_sends<EAXSOURCEALLSENDPROPERTIES>(call, props.sends,
                Eax4AllSendValidator{});
            break;

        case EAXSOURCE_OCCLUSIONSENDPARAMETERS:
            eax4_defer_sends<EAXSOURCEOCCLUSIONSENDPROPERTIES>(call, props.sends,
                Eax4OcclusionSendValidator{});
            break;

        case EAXSOURCE_EXCLUSIONSENDPARAMETERS:
            eax4_defer_sends<EAXSOURCEEXCLUSIONSENDPROPERTIES>(call, props.sends,
                Eax4ExclusionSendValidator{});
            break;

        case EAXSOURCE_ACTIVEFXSLOTID:
            eax4_defer_active_fx_slot_id(call, props.active_fx_slots.guidActiveFXSlots);
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax5_defer_all_2d(const EaxCall& call, EAX50SOURCEPROPERTIES& props)
{
    const auto &src_props = call.load<const EAXSOURCE2DPROPERTIES>();
    Eax5SourceAll2dValidator{}(src_props);
    props.lDirect = src_props.lDirect;
    props.lDirectHF = src_props.lDirectHF;
    props.lRoom = src_props.lRoom;
    props.lRoomHF = src_props.lRoomHF;
    props.ulFlags = src_props.ulFlags;
}

void ALsource::eax5_defer_speaker_levels(const EaxCall& call, EaxSpeakerLevels& props)
{
    const auto values = call.as_span<const EAXSPEAKERLEVELPROPERTIES>(eax_max_speakers);
    std::ranges::for_each(values, Eax5SpeakerAllValidator{});

    for(const auto &value : values)
    {
        const auto index = gsl::narrow_cast<size_t>(value.lSpeakerID - EAXSPEAKER_FRONT_LEFT);
        props[index].lLevel = value.lLevel;
    }
}

void ALsource::eax5_set(const EaxCall& call, Eax5Props& props)
{
    switch(call.get_property_id())
    {
    case EAXSOURCE_NONE:
        break;

    case EAXSOURCE_ALLPARAMETERS:
        eax_defer(call, props.source, Eax5SourceAllValidator{});
        break;

    case EAXSOURCE_OBSTRUCTIONPARAMETERS:
    case EAXSOURCE_OCCLUSIONPARAMETERS:
    case EAXSOURCE_EXCLUSIONPARAMETERS:
    case EAXSOURCE_DIRECT:
    case EAXSOURCE_DIRECTHF:
    case EAXSOURCE_ROOM:
    case EAXSOURCE_ROOMHF:
    case EAXSOURCE_OBSTRUCTION:
    case EAXSOURCE_OBSTRUCTIONLFRATIO:
    case EAXSOURCE_OCCLUSION:
    case EAXSOURCE_OCCLUSIONLFRATIO:
    case EAXSOURCE_OCCLUSIONROOMRATIO:
    case EAXSOURCE_OCCLUSIONDIRECTRATIO:
    case EAXSOURCE_EXCLUSION:
    case EAXSOURCE_EXCLUSIONLFRATIO:
    case EAXSOURCE_OUTSIDEVOLUMEHF:
    case EAXSOURCE_DOPPLERFACTOR:
    case EAXSOURCE_ROLLOFFFACTOR:
    case EAXSOURCE_ROOMROLLOFFFACTOR:
    case EAXSOURCE_AIRABSORPTIONFACTOR:
        eax3_set(call, props.source);
        break;

    case EAXSOURCE_FLAGS:
        eax_defer(call, props.source.ulFlags, Eax5SourceFlagsValidator{});
        break;

    case EAXSOURCE_SENDPARAMETERS:
        eax5_defer_sends<EAXSOURCESENDPROPERTIES>(call, props.sends, Eax5SendValidator{});
        break;

    case EAXSOURCE_ALLSENDPARAMETERS:
        eax5_defer_sends<EAXSOURCEALLSENDPROPERTIES>(call, props.sends,
            Eax5AllSendValidator{});
        break;

    case EAXSOURCE_OCCLUSIONSENDPARAMETERS:
        eax5_defer_sends<EAXSOURCEOCCLUSIONSENDPROPERTIES>(call, props.sends,
            Eax5OcclusionSendValidator{});
        break;

    case EAXSOURCE_EXCLUSIONSENDPARAMETERS:
        eax5_defer_sends<EAXSOURCEEXCLUSIONSENDPROPERTIES>(call, props.sends,
            Eax5ExclusionSendValidator{});
        break;

    case EAXSOURCE_ACTIVEFXSLOTID:
        eax5_defer_active_fx_slot_id(call, props.active_fx_slots.guidActiveFXSlots);
        break;

    case EAXSOURCE_MACROFXFACTOR:
        eax_defer(call, props.source.flMacroFXFactor, Eax5SourceMacroFXFactorValidator{});
        break;

    case EAXSOURCE_SPEAKERLEVELS:
        eax5_defer_speaker_levels(call, props.speaker_levels);
        break;

    case EAXSOURCE_ALL2DPARAMETERS:
        eax5_defer_all_2d(call, props.source);
        break;

    default:
        eax_fail_unknown_property_id();
    }
}

void ALsource::eax_set(const EaxCall& call)
{
    const auto eax_version = call.get_version();
    switch(eax_version)
    {
    case 1: eax1_set(call, mEax1.d); break;
    case 2: eax2_set(call, mEax2.d); break;
    case 3: eax3_set(call, mEax3.d); break;
    case 4: eax4_set(call, mEax4.d); break;
    case 5: eax5_set(call, mEax5.d); break;
    default: eax_fail_unknown_property_id();
    }
    mEaxChanged = true;
    mEaxVersion = eax_version;
}

void ALsource::eax_get_active_fx_slot_id(const EaxCall& call, const std::span<const GUID> srcids)
{
    Expects(srcids.size()==EAX40_MAX_ACTIVE_FXSLOTS || srcids.size()==EAX50_MAX_ACTIVE_FXSLOTS);
    const auto dst_ids = call.as_span<GUID>(srcids.size());
    std::uninitialized_copy_n(srcids.begin(), dst_ids.size(), dst_ids.begin());
}

void ALsource::eax1_get(const EaxCall& call, const EAXBUFFER_REVERBPROPERTIES& props)
{
    switch(call.get_property_id())
    {
    case DSPROPERTY_EAXBUFFER_ALL:
    case DSPROPERTY_EAXBUFFER_REVERBMIX:
        call.store(props.fMix);
        break;

    default:
        eax_fail_unknown_property_id();
    }
}

void ALsource::eax2_get(const EaxCall& call, const EAX20BUFFERPROPERTIES& props)
{
    switch(call.get_property_id())
    {
    case DSPROPERTY_EAX20BUFFER_NONE: break;
    case DSPROPERTY_EAX20BUFFER_ALLPARAMETERS: call.store(props); break;
    case DSPROPERTY_EAX20BUFFER_DIRECT: call.store(props.lDirect); break;
    case DSPROPERTY_EAX20BUFFER_DIRECTHF: call.store(props.lDirectHF); break;
    case DSPROPERTY_EAX20BUFFER_ROOM: call.store(props.lRoom); break;
    case DSPROPERTY_EAX20BUFFER_ROOMHF: call.store(props.lRoomHF); break;
    case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR: call.store(props.flRoomRolloffFactor); break;
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTION: call.store(props.lObstruction); break;
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO: call.store(props.flObstructionLFRatio); break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSION: call.store(props.lOcclusion); break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO: call.store(props.flOcclusionLFRatio); break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO: call.store(props.flOcclusionRoomRatio); break;
    case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF: call.store(props.lOutsideVolumeHF); break;
    case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR: call.store(props.flAirAbsorptionFactor);break;
    case DSPROPERTY_EAX20BUFFER_FLAGS: call.store(props.dwFlags); break;
    default: eax_fail_unknown_property_id();
    }
}

void ALsource::eax3_get(const EaxCall &call, const EAX30SOURCEPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXSOURCE_NONE: break;
    case EAXSOURCE_ALLPARAMETERS: call.store(props); break;
    case EAXSOURCE_OBSTRUCTIONPARAMETERS: call.store(props.mObstruction); break;
    case EAXSOURCE_OCCLUSIONPARAMETERS: call.store(props.mOcclusion); break;
    case EAXSOURCE_EXCLUSIONPARAMETERS: call.store(props.mExclusion); break;
    case EAXSOURCE_DIRECT: call.store(props.lDirect); break;
    case EAXSOURCE_DIRECTHF: call.store(props.lDirectHF); break;
    case EAXSOURCE_ROOM: call.store(props.lRoom); break;
    case EAXSOURCE_ROOMHF: call.store(props.lRoomHF); break;
    case EAXSOURCE_OBSTRUCTION: call.store(props.mObstruction.lObstruction); break;
    case EAXSOURCE_OBSTRUCTIONLFRATIO: call.store(props.mObstruction.flObstructionLFRatio); break;
    case EAXSOURCE_OCCLUSION: call.store(props.mOcclusion.lOcclusion); break;
    case EAXSOURCE_OCCLUSIONLFRATIO: call.store(props.mOcclusion.flOcclusionLFRatio); break;
    case EAXSOURCE_OCCLUSIONROOMRATIO: call.store(props.mOcclusion.flOcclusionRoomRatio); break;
    case EAXSOURCE_OCCLUSIONDIRECTRATIO: call.store(props.mOcclusion.flOcclusionDirectRatio);break;
    case EAXSOURCE_EXCLUSION: call.store(props.mExclusion.lExclusion); break;
    case EAXSOURCE_EXCLUSIONLFRATIO: call.store(props.mExclusion.flExclusionLFRatio); break;
    case EAXSOURCE_OUTSIDEVOLUMEHF: call.store(props.lOutsideVolumeHF); break;
    case EAXSOURCE_DOPPLERFACTOR: call.store(props.flDopplerFactor); break;
    case EAXSOURCE_ROLLOFFFACTOR: call.store(props.flRolloffFactor); break;
    case EAXSOURCE_ROOMROLLOFFFACTOR: call.store(props.flRoomRolloffFactor); break;
    case EAXSOURCE_AIRABSORPTIONFACTOR: call.store(props.flAirAbsorptionFactor); break;
    case EAXSOURCE_FLAGS: call.store(props.ulFlags); break;
    default: eax_fail_unknown_property_id();
    }
}

void ALsource::eax4_get(const EaxCall &call, const Eax4Props &props)
{
    switch(call.get_property_id())
    {
    case EAXSOURCE_NONE:
        break;

    case EAXSOURCE_ALLPARAMETERS:
    case EAXSOURCE_OBSTRUCTIONPARAMETERS:
    case EAXSOURCE_OCCLUSIONPARAMETERS:
    case EAXSOURCE_EXCLUSIONPARAMETERS:
    case EAXSOURCE_DIRECT:
    case EAXSOURCE_DIRECTHF:
    case EAXSOURCE_ROOM:
    case EAXSOURCE_ROOMHF:
    case EAXSOURCE_OBSTRUCTION:
    case EAXSOURCE_OBSTRUCTIONLFRATIO:
    case EAXSOURCE_OCCLUSION:
    case EAXSOURCE_OCCLUSIONLFRATIO:
    case EAXSOURCE_OCCLUSIONROOMRATIO:
    case EAXSOURCE_OCCLUSIONDIRECTRATIO:
    case EAXSOURCE_EXCLUSION:
    case EAXSOURCE_EXCLUSIONLFRATIO:
    case EAXSOURCE_OUTSIDEVOLUMEHF:
    case EAXSOURCE_DOPPLERFACTOR:
    case EAXSOURCE_ROLLOFFFACTOR:
    case EAXSOURCE_ROOMROLLOFFFACTOR:
    case EAXSOURCE_AIRABSORPTIONFACTOR:
    case EAXSOURCE_FLAGS:
        eax3_get(call, props.source);
        break;

    case EAXSOURCE_SENDPARAMETERS:
        eax_get_sends<EAXSOURCESENDPROPERTIES>(call, props.sends);
        break;

    case EAXSOURCE_ALLSENDPARAMETERS:
        eax_get_sends<EAXSOURCEALLSENDPROPERTIES>(call, props.sends);
        break;

    case EAXSOURCE_OCCLUSIONSENDPARAMETERS:
        eax_get_sends<EAXSOURCEOCCLUSIONSENDPROPERTIES>(call, props.sends);
        break;

    case EAXSOURCE_EXCLUSIONSENDPARAMETERS:
        eax_get_sends<EAXSOURCEEXCLUSIONSENDPROPERTIES>(call, props.sends);
        break;

    case EAXSOURCE_ACTIVEFXSLOTID:
        eax_get_active_fx_slot_id(call, props.active_fx_slots.guidActiveFXSlots);
        break;

    default:
        eax_fail_unknown_property_id();
    }
}

void ALsource::eax5_get_all_2d(const EaxCall &call, const EAX50SOURCEPROPERTIES &props)
{
    auto &subprops = call.load<EAXSOURCE2DPROPERTIES>();
    subprops.lDirect = props.lDirect;
    subprops.lDirectHF = props.lDirectHF;
    subprops.lRoom = props.lRoom;
    subprops.lRoomHF = props.lRoomHF;
    subprops.ulFlags = props.ulFlags;
}

void ALsource::eax5_get_speaker_levels(const EaxCall &call, const EaxSpeakerLevels &props)
{
    const auto subprops = call.as_span<EAXSPEAKERLEVELPROPERTIES>(eax_max_speakers);
    std::uninitialized_copy_n(props.cbegin(), subprops.size(), subprops.begin());
}

void ALsource::eax5_get(const EaxCall &call, const Eax5Props &props)
{
    switch(call.get_property_id())
    {
    case EAXSOURCE_NONE:
        break;

    case EAXSOURCE_ALLPARAMETERS:
    case EAXSOURCE_OBSTRUCTIONPARAMETERS:
    case EAXSOURCE_OCCLUSIONPARAMETERS:
    case EAXSOURCE_EXCLUSIONPARAMETERS:
    case EAXSOURCE_DIRECT:
    case EAXSOURCE_DIRECTHF:
    case EAXSOURCE_ROOM:
    case EAXSOURCE_ROOMHF:
    case EAXSOURCE_OBSTRUCTION:
    case EAXSOURCE_OBSTRUCTIONLFRATIO:
    case EAXSOURCE_OCCLUSION:
    case EAXSOURCE_OCCLUSIONLFRATIO:
    case EAXSOURCE_OCCLUSIONROOMRATIO:
    case EAXSOURCE_OCCLUSIONDIRECTRATIO:
    case EAXSOURCE_EXCLUSION:
    case EAXSOURCE_EXCLUSIONLFRATIO:
    case EAXSOURCE_OUTSIDEVOLUMEHF:
    case EAXSOURCE_DOPPLERFACTOR:
    case EAXSOURCE_ROLLOFFFACTOR:
    case EAXSOURCE_ROOMROLLOFFFACTOR:
    case EAXSOURCE_AIRABSORPTIONFACTOR:
    case EAXSOURCE_FLAGS:
        eax3_get(call, props.source);
        break;

    case EAXSOURCE_SENDPARAMETERS:
        eax_get_sends<EAXSOURCESENDPROPERTIES>(call, props.sends);
        break;

    case EAXSOURCE_ALLSENDPARAMETERS:
        eax_get_sends<EAXSOURCEALLSENDPROPERTIES>(call, props.sends);
        break;

    case EAXSOURCE_OCCLUSIONSENDPARAMETERS:
        eax_get_sends<EAXSOURCEOCCLUSIONSENDPROPERTIES>(call, props.sends);
        break;

    case EAXSOURCE_EXCLUSIONSENDPARAMETERS:
        eax_get_sends<EAXSOURCEEXCLUSIONSENDPROPERTIES>(call, props.sends);
        break;

    case EAXSOURCE_ACTIVEFXSLOTID:
        eax_get_active_fx_slot_id(call, props.active_fx_slots.guidActiveFXSlots);
        break;

    case EAXSOURCE_MACROFXFACTOR:
        call.store(props.source.flMacroFXFactor);
        break;

    case EAXSOURCE_SPEAKERLEVELS:
        call.store(props.speaker_levels);
        break;

    case EAXSOURCE_ALL2DPARAMETERS:
        eax5_get_all_2d(call, props.source);
        break;

    default:
        eax_fail_unknown_property_id();
    }
}

void ALsource::eax_get(const EaxCall &call) const
{
    switch(call.get_version())
    {
    case 1: eax1_get(call, mEax1.i); break;
    case 2: eax2_get(call, mEax2.i); break;
    case 3: eax3_get(call, mEax3.i); break;
    case 4: eax4_get(call, mEax4.i); break;
    case 5: eax5_get(call, mEax5.i); break;
    default: eax_fail_unknown_version();
    }
}

void ALsource::eax_set_al_source_send(al::intrusive_ptr<ALeffectslot> slot, size_t sendidx,
    const EaxAlLowPassParam &filter)
{
    if(sendidx >= EAX_MAX_FXSLOTS)
        return;

    auto &send = Send[sendidx];
    send.mSlot = std::move(slot);
    send.mGain = filter.gain;
    send.mGainHF = filter.gain_hf;
    send.mHFReference = LowPassFreqRef;
    send.mGainLF = 1.0f;
    send.mLFReference = HighPassFreqRef;

    mPropsDirty = true;
}

void ALsource::eax_commit_active_fx_slots()
{
    // Clear all slots to an inactive state.
    mEaxActiveFxSlots.reset();

    // Mark the set slots as active.
    for(const auto& slot_id : mEax.active_fx_slots.guidActiveFXSlots)
    {
        if(slot_id == EAX_NULL_GUID)
        {
        }
        else if(slot_id == EAX_PrimaryFXSlotID)
        {
            // Mark primary FX slot as active.
            if(mEaxPrimaryFxSlotId.has_value())
                mEaxActiveFxSlots.set(*mEaxPrimaryFxSlotId);
        }
        else if(slot_id == EAXPROPERTYID_EAX50_FXSlot0)
            mEaxActiveFxSlots.set(0);
        else if(slot_id == EAXPROPERTYID_EAX50_FXSlot1)
            mEaxActiveFxSlots.set(1);
        else if(slot_id == EAXPROPERTYID_EAX50_FXSlot2)
            mEaxActiveFxSlots.set(2);
        else if(slot_id == EAXPROPERTYID_EAX50_FXSlot3)
            mEaxActiveFxSlots.set(3);
    }

    // Deactivate EFX auxiliary effect slots for inactive slots. Active slots
    // will be updated with the room filters.
    for(const auto i : std::views::iota(0_uz, size_t{EAX_MAX_FXSLOTS}))
    {
        if(!mEaxActiveFxSlots.test(i))
            eax_set_al_source_send({}, i, EaxAlLowPassParam{1.0f, 1.0f});
    }
}

void ALsource::eax_commit_filters()
{
    eax_update_direct_filter();
    eax_update_room_filters();
}

void ALsource::eaxCommit()
{
    const auto primary_fx_slot_id = mEaxAlContext->eaxGetPrimaryFxSlotIndex();
    const auto is_primary_fx_slot_id_changed = (mEaxPrimaryFxSlotId != primary_fx_slot_id);

    if(!mEaxChanged && !is_primary_fx_slot_id_changed)
        return;

    mEaxPrimaryFxSlotId = primary_fx_slot_id;
    mEaxChanged = false;

    switch(mEaxVersion)
    {
    case 1:
        mEax1.i = mEax1.d;
        eax1_translate(mEax1.i, mEax);
        break;
    case 2:
        mEax2.i = mEax2.d;
        eax2_translate(mEax2.i, mEax);
        break;
    case 3:
        mEax3.i = mEax3.d;
        eax3_translate(mEax3.i, mEax);
        break;
    case 4:
        mEax4.i = mEax4.d;
        eax4_translate(mEax4.i, mEax);
        break;
    case 5:
        mEax5.i = mEax5.d;
        mEax = mEax5.d;
        break;
    }

    eax_set_efx_outer_gain_hf();
    eax_set_efx_doppler_factor();
    eax_set_efx_rolloff_factor();
    eax_set_efx_room_rolloff_factor();
    eax_set_efx_air_absorption_factor();
    eax_set_efx_dry_gain_hf_auto();
    eax_set_efx_wet_gain_auto();
    eax_set_efx_wet_gain_hf_auto();

    eax_commit_active_fx_slots();
    eax_commit_filters();
}

#endif // ALSOFT_EAX
