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
#include <cassert>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx.h"

#include "albit.h"
#include "alc/alu.h"
#include "alc/backends/base.h"
#include "alc/context.h"
#include "alc/device.h"
#include "alc/inprogext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "aloptional.h"
#include "alspan.h"
#include "atomic.h"
#include "auxeffectslot.h"
#include "buffer.h"
#include "core/ambidefs.h"
#include "core/bformatdec.h"
#include "core/except.h"
#include "core/filters/nfc.h"
#include "core/filters/splitter.h"
#include "core/logging.h"
#include "core/voice_change.h"
#include "event.h"
#include "filter.h"
#include "opthelpers.h"
#include "ringbuffer.h"
#include "threads.h"

#ifdef ALSOFT_EAX
#include <cassert>
#endif // ALSOFT_EAX

namespace {

using namespace std::placeholders;
using std::chrono::nanoseconds;

Voice *GetSourceVoice(ALsource *source, ALCcontext *context)
{
    auto voicelist = context->getVoicesSpan();
    ALuint idx{source->VoiceIdx};
    if(idx < voicelist.size())
    {
        ALuint sid{source->id};
        Voice *voice = voicelist[idx];
        if(voice->mSourceID.load(std::memory_order_acquire) == sid)
            return voice;
    }
    source->VoiceIdx = INVALID_VOICE_IDX;
    return nullptr;
}


void UpdateSourceProps(const ALsource *source, Voice *voice, ALCcontext *context)
{
    /* Get an unused property container, or allocate a new one as needed. */
    VoicePropsItem *props{context->mFreeVoiceProps.load(std::memory_order_acquire)};
    if(!props)
    {
        context->allocVoiceProps();
        props = context->mFreeVoiceProps.load(std::memory_order_acquire);
    }
    VoicePropsItem *next;
    do {
        next = props->next.load(std::memory_order_relaxed);
    } while(unlikely(context->mFreeVoiceProps.compare_exchange_weak(props, next,
        std::memory_order_acq_rel, std::memory_order_acquire) == false));

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
#ifdef ALSOFT_EAX
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

    props->Direct.Gain = source->Direct.Gain;
    props->Direct.GainHF = source->Direct.GainHF;
    props->Direct.HFReference = source->Direct.HFReference;
    props->Direct.GainLF = source->Direct.GainLF;
    props->Direct.LFReference = source->Direct.LFReference;

    auto copy_send = [](const ALsource::SendData &srcsend) noexcept -> VoiceProps::SendData
    {
        VoiceProps::SendData ret{};
        ret.Slot = srcsend.Slot ? &srcsend.Slot->mSlot : nullptr;
        ret.Gain = srcsend.Gain;
        ret.GainHF = srcsend.GainHF;
        ret.HFReference = srcsend.HFReference;
        ret.GainLF = srcsend.GainLF;
        ret.LFReference = srcsend.LFReference;
        return ret;
    };
    std::transform(source->Send.cbegin(), source->Send.cend(), props->Send, copy_send);
    if(!props->Send[0].Slot && context->mDefaultSlot)
        props->Send[0].Slot = &context->mDefaultSlot->mSlot;

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
int64_t GetSourceSampleOffset(ALsource *Source, ALCcontext *context, nanoseconds *clocktime)
{
    ALCdevice *device{context->mALDevice.get()};
    const VoiceBufferItem *Current{};
    uint64_t readPos{};
    ALuint refcount;
    Voice *voice;

    do {
        refcount = device->waitForMix();
        *clocktime = GetDeviceClockTime(device);
        voice = GetSourceVoice(Source, context);
        if(voice)
        {
            Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);

            readPos  = uint64_t{voice->mPosition.load(std::memory_order_relaxed)} << 32;
            readPos |= uint64_t{voice->mPositionFrac.load(std::memory_order_relaxed)} <<
                       (32-MixerFracBits);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->MixCount.load(std::memory_order_relaxed));

    if(!voice)
        return 0;

    for(auto &item : Source->mQueue)
    {
        if(&item == Current) break;
        readPos += uint64_t{item.mSampleLen} << 32;
    }
    return static_cast<int64_t>(minu64(readPos, 0x7fffffffffffffff_u64));
}

/* GetSourceSecOffset
 *
 * Gets the current read offset for the given Source, in seconds. The offset is
 * relative to the start of the queue (not the start of the current buffer).
 */
double GetSourceSecOffset(ALsource *Source, ALCcontext *context, nanoseconds *clocktime)
{
    ALCdevice *device{context->mALDevice.get()};
    const VoiceBufferItem *Current{};
    uint64_t readPos{};
    ALuint refcount;
    Voice *voice;

    do {
        refcount = device->waitForMix();
        *clocktime = GetDeviceClockTime(device);
        voice = GetSourceVoice(Source, context);
        if(voice)
        {
            Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);

            readPos  = uint64_t{voice->mPosition.load(std::memory_order_relaxed)} << MixerFracBits;
            readPos |= voice->mPositionFrac.load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->MixCount.load(std::memory_order_relaxed));

    if(!voice)
        return 0.0f;

    const ALbuffer *BufferFmt{nullptr};
    auto BufferList = Source->mQueue.cbegin();
    while(BufferList != Source->mQueue.cend() && std::addressof(*BufferList) != Current)
    {
        if(!BufferFmt) BufferFmt = BufferList->mBuffer;
        readPos += uint64_t{BufferList->mSampleLen} << MixerFracBits;
        ++BufferList;
    }
    while(BufferList != Source->mQueue.cend() && !BufferFmt)
    {
        BufferFmt = BufferList->mBuffer;
        ++BufferList;
    }
    ASSUME(BufferFmt != nullptr);

    return static_cast<double>(readPos) / double{MixerFracOne} / BufferFmt->mSampleRate;
}

/* GetSourceOffset
 *
 * Gets the current read offset for the given Source, in the appropriate format
 * (Bytes, Samples or Seconds). The offset is relative to the start of the
 * queue (not the start of the current buffer).
 */
double GetSourceOffset(ALsource *Source, ALenum name, ALCcontext *context)
{
    ALCdevice *device{context->mALDevice.get()};
    const VoiceBufferItem *Current{};
    ALuint readPos{};
    ALuint readPosFrac{};
    ALuint refcount;
    Voice *voice;

    do {
        refcount = device->waitForMix();
        voice = GetSourceVoice(Source, context);
        if(voice)
        {
            Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);

            readPos = voice->mPosition.load(std::memory_order_relaxed);
            readPosFrac = voice->mPositionFrac.load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->MixCount.load(std::memory_order_relaxed));

    if(!voice)
        return 0.0;

    const ALbuffer *BufferFmt{nullptr};
    auto BufferList = Source->mQueue.cbegin();
    while(BufferList != Source->mQueue.cend() && std::addressof(*BufferList) != Current)
    {
        if(!BufferFmt) BufferFmt = BufferList->mBuffer;
        readPos += BufferList->mSampleLen;
        ++BufferList;
    }
    while(BufferList != Source->mQueue.cend() && !BufferFmt)
    {
        BufferFmt = BufferList->mBuffer;
        ++BufferList;
    }
    ASSUME(BufferFmt != nullptr);

    double offset{};
    switch(name)
    {
    case AL_SEC_OFFSET:
        offset = (readPos + readPosFrac/double{MixerFracOne}) / BufferFmt->mSampleRate;
        break;

    case AL_SAMPLE_OFFSET:
        offset = readPos + readPosFrac/double{MixerFracOne};
        break;

    case AL_BYTE_OFFSET:
        if(BufferFmt->OriginalType == UserFmtIMA4)
        {
            ALuint FrameBlockSize{BufferFmt->OriginalAlign};
            ALuint align{(BufferFmt->OriginalAlign-1)/2 + 4};
            ALuint BlockSize{align * BufferFmt->channelsFromFmt()};

            /* Round down to nearest ADPCM block */
            offset = static_cast<double>(readPos / FrameBlockSize * BlockSize);
        }
        else if(BufferFmt->OriginalType == UserFmtMSADPCM)
        {
            ALuint FrameBlockSize{BufferFmt->OriginalAlign};
            ALuint align{(FrameBlockSize-2)/2 + 7};
            ALuint BlockSize{align * BufferFmt->channelsFromFmt()};

            /* Round down to nearest ADPCM block */
            offset = static_cast<double>(readPos / FrameBlockSize * BlockSize);
        }
        else
        {
            const ALuint FrameSize{BufferFmt->frameSizeFromFmt()};
            offset = static_cast<double>(readPos * FrameSize);
        }
        break;
    }
    return offset;
}

/* GetSourceLength
 *
 * Gets the length of the given Source's buffer queue, in the appropriate
 * format (Bytes, Samples or Seconds).
 */
double GetSourceLength(const ALsource *source, ALenum name)
{
    uint64_t length{0};
    const ALbuffer *BufferFmt{nullptr};
    for(auto &listitem : source->mQueue)
    {
        if(!BufferFmt)
            BufferFmt = listitem.mBuffer;
        length += listitem.mSampleLen;
    }
    if(length == 0)
        return 0.0;

    ASSUME(BufferFmt != nullptr);
    switch(name)
    {
    case AL_SEC_LENGTH_SOFT:
        return static_cast<double>(length) / BufferFmt->mSampleRate;

    case AL_SAMPLE_LENGTH_SOFT:
        return static_cast<double>(length);

    case AL_BYTE_LENGTH_SOFT:
        if(BufferFmt->OriginalType == UserFmtIMA4)
        {
            ALuint FrameBlockSize{BufferFmt->OriginalAlign};
            ALuint align{(BufferFmt->OriginalAlign-1)/2 + 4};
            ALuint BlockSize{align * BufferFmt->channelsFromFmt()};

            /* Round down to nearest ADPCM block */
            return static_cast<double>(length / FrameBlockSize) * BlockSize;
        }
        else if(BufferFmt->OriginalType == UserFmtMSADPCM)
        {
            ALuint FrameBlockSize{BufferFmt->OriginalAlign};
            ALuint align{(FrameBlockSize-2)/2 + 7};
            ALuint BlockSize{align * BufferFmt->channelsFromFmt()};

            /* Round down to nearest ADPCM block */
            return static_cast<double>(length / FrameBlockSize) * BlockSize;
        }
        return static_cast<double>(length) * BufferFmt->frameSizeFromFmt();
    }
    return 0.0;
}


struct VoicePos {
    ALuint pos, frac;
    ALbufferQueueItem *bufferitem;
};

/**
 * GetSampleOffset
 *
 * Retrieves the voice position, fixed-point fraction, and bufferlist item
 * using the givem offset type and offset. If the offset is out of range,
 * returns an empty optional.
 */
al::optional<VoicePos> GetSampleOffset(al::deque<ALbufferQueueItem> &BufferList, ALenum OffsetType,
    double Offset)
{
    /* Find the first valid Buffer in the Queue */
    const ALbuffer *BufferFmt{nullptr};
    for(auto &item : BufferList)
    {
        BufferFmt = item.mBuffer;
        if(BufferFmt) break;
    }
    if(!BufferFmt || BufferFmt->mCallback)
        return al::nullopt;

    /* Get sample frame offset */
    ALuint offset{0u}, frac{0u};
    double dbloff, dblfrac;
    switch(OffsetType)
    {
    case AL_SEC_OFFSET:
        dblfrac = std::modf(Offset*BufferFmt->mSampleRate, &dbloff);
        offset = static_cast<ALuint>(mind(dbloff, std::numeric_limits<ALuint>::max()));
        frac = static_cast<ALuint>(mind(dblfrac*MixerFracOne, MixerFracOne-1.0));
        break;

    case AL_SAMPLE_OFFSET:
        dblfrac = std::modf(Offset, &dbloff);
        offset = static_cast<ALuint>(mind(dbloff, std::numeric_limits<ALuint>::max()));
        frac = static_cast<ALuint>(mind(dblfrac*MixerFracOne, MixerFracOne-1.0));
        break;

    case AL_BYTE_OFFSET:
        /* Determine the ByteOffset (and ensure it is block aligned) */
        offset = static_cast<ALuint>(Offset);
        if(BufferFmt->OriginalType == UserFmtIMA4)
        {
            const ALuint align{(BufferFmt->OriginalAlign-1)/2 + 4};
            offset /= align * BufferFmt->channelsFromFmt();
            offset *= BufferFmt->OriginalAlign;
        }
        else if(BufferFmt->OriginalType == UserFmtMSADPCM)
        {
            const ALuint align{(BufferFmt->OriginalAlign-2)/2 + 7};
            offset /= align * BufferFmt->channelsFromFmt();
            offset *= BufferFmt->OriginalAlign;
        }
        else
            offset /= BufferFmt->frameSizeFromFmt();
        frac = 0;
        break;
    }

    /* Find the bufferlist item this offset belongs to. */
    ALuint totalBufferLen{0u};
    for(auto &item : BufferList)
    {
        if(totalBufferLen > offset)
            break;
        if(item.mSampleLen > offset-totalBufferLen)
        {
            /* Offset is in this buffer */
            return VoicePos{offset-totalBufferLen, frac, &item};
        }
        totalBufferLen += item.mSampleLen;
    }

    /* Offset is out of range of the queue */
    return al::nullopt;
}


void InitVoice(Voice *voice, ALsource *source, ALbufferQueueItem *BufferList, ALCcontext *context,
    ALCdevice *device)
{
    voice->mLoopBuffer.store(source->Looping ? &source->mQueue.front() : nullptr,
        std::memory_order_relaxed);

    ALbuffer *buffer{BufferList->mBuffer};
    voice->mFrequency = buffer->mSampleRate;
    voice->mFmtChannels =
        (buffer->mChannels == FmtStereo && source->mStereoMode == SourceStereo::Enhanced) ?
        FmtSuperStereo : buffer->mChannels;
    voice->mFmtType = buffer->mType;
    voice->mFrameStep = buffer->channelsFromFmt();
    voice->mFrameSize = buffer->frameSizeFromFmt();
    voice->mAmbiLayout = IsUHJ(voice->mFmtChannels) ? AmbiLayout::FuMa : buffer->mAmbiLayout;
    voice->mAmbiScaling = IsUHJ(voice->mFmtChannels) ? AmbiScaling::UHJ : buffer->mAmbiScaling;
    voice->mAmbiOrder = (voice->mFmtChannels == FmtSuperStereo) ? 1 : buffer->mAmbiOrder;

    if(buffer->mCallback) voice->mFlags.set(VoiceIsCallback);
    else if(source->SourceType == AL_STATIC) voice->mFlags.set(VoiceIsStatic);
    voice->mNumCallbackSamples = 0;

    voice->prepare(device);

    source->mPropsDirty = false;
    UpdateSourceProps(source, voice, context);

    voice->mSourceID.store(source->id, std::memory_order_release);
}


VoiceChange *GetVoiceChanger(ALCcontext *ctx)
{
    VoiceChange *vchg{ctx->mVoiceChangeTail};
    if UNLIKELY(vchg == ctx->mCurrentVoiceChange.load(std::memory_order_acquire))
    {
        ctx->allocVoiceChanges();
        vchg = ctx->mVoiceChangeTail;
    }

    ctx->mVoiceChangeTail = vchg->mNext.exchange(nullptr, std::memory_order_relaxed);

    return vchg;
}

void SendVoiceChanges(ALCcontext *ctx, VoiceChange *tail)
{
    ALCdevice *device{ctx->mALDevice.get()};

    VoiceChange *oldhead{ctx->mCurrentVoiceChange.load(std::memory_order_acquire)};
    while(VoiceChange *next{oldhead->mNext.load(std::memory_order_relaxed)})
        oldhead = next;
    oldhead->mNext.store(tail, std::memory_order_release);

    const bool connected{device->Connected.load(std::memory_order_acquire)};
    device->waitForMix();
    if UNLIKELY(!connected)
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


bool SetVoiceOffset(Voice *oldvoice, const VoicePos &vpos, ALsource *source, ALCcontext *context,
    ALCdevice *device)
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
    if(unlikely(!newvoice))
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
    newvoice->mFlags.reset();
    if(vpos.pos > 0 || vpos.frac > 0 || vpos.bufferitem != &source->mQueue.front())
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
    if LIKELY(oldvoice->mSourceID.load(std::memory_order_acquire) != 0u)
        return true;

    /* Otherwise, if the new voice's state is not pending, the change-over
     * already happened.
     */
    if(newvoice->mPlayState.load(std::memory_order_acquire) != Voice::Pending)
        return true;

    /* Otherwise, wait for any current mix to finish and check one last time. */
    device->waitForMix();
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
    size_t count{std::accumulate(context->mSourceList.cbegin(), context->mSourceList.cend(),
        size_t{0},
        [](size_t cur, const SourceSubList &sublist) noexcept -> size_t
        { return cur + static_cast<ALuint>(al::popcount(sublist.FreeMask)); })};

    while(needed > count)
    {
        if UNLIKELY(context->mSourceList.size() >= 1<<25)
            return false;

        context->mSourceList.emplace_back();
        auto sublist = context->mSourceList.end() - 1;
        sublist->FreeMask = ~0_u64;
        sublist->Sources = static_cast<ALsource*>(al_calloc(alignof(ALsource), sizeof(ALsource)*64));
        if UNLIKELY(!sublist->Sources)
        {
            context->mSourceList.pop_back();
            return false;
        }
        count += 64;
    }
    return true;
}

ALsource *AllocSource(ALCcontext *context)
{
    auto sublist = std::find_if(context->mSourceList.begin(), context->mSourceList.end(),
        [](const SourceSubList &entry) noexcept -> bool
        { return entry.FreeMask != 0; });
    auto lidx = static_cast<ALuint>(std::distance(context->mSourceList.begin(), sublist));
    auto slidx = static_cast<ALuint>(al::countr_zero(sublist->FreeMask));
    ASSUME(slidx < 64);

    ALsource *source{al::construct_at(sublist->Sources + slidx)};

    /* Add 1 to avoid source ID 0. */
    source->id = ((lidx<<6) | slidx) + 1;

    context->mNumSources += 1;
    sublist->FreeMask &= ~(1_u64 << slidx);

    return source;
}

void FreeSource(ALCcontext *context, ALsource *source)
{
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

    al::destroy_at(source);

    context->mSourceList[lidx].FreeMask |= 1_u64 << slidx;
    context->mNumSources--;
}


inline ALsource *LookupSource(ALCcontext *context, ALuint id) noexcept
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if UNLIKELY(lidx >= context->mSourceList.size())
        return nullptr;
    SourceSubList &sublist{context->mSourceList[lidx]};
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.Sources + slidx;
}

inline ALbuffer *LookupBuffer(ALCdevice *device, ALuint id) noexcept
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if UNLIKELY(lidx >= device->BufferList.size())
        return nullptr;
    BufferSubList &sublist = device->BufferList[lidx];
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.Buffers + slidx;
}

inline ALfilter *LookupFilter(ALCdevice *device, ALuint id) noexcept
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if UNLIKELY(lidx >= device->FilterList.size())
        return nullptr;
    FilterSubList &sublist = device->FilterList[lidx];
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.Filters + slidx;
}

inline ALeffectslot *LookupEffectSlot(ALCcontext *context, ALuint id) noexcept
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if UNLIKELY(lidx >= context->mEffectSlotList.size())
        return nullptr;
    EffectSlotSubList &sublist{context->mEffectSlotList[lidx]};
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.EffectSlots + slidx;
}


al::optional<SourceStereo> StereoModeFromEnum(ALenum mode)
{
    switch(mode)
    {
    case AL_NORMAL_SOFT: return al::make_optional(SourceStereo::Normal);
    case AL_SUPER_STEREO_SOFT: return al::make_optional(SourceStereo::Enhanced);
    }
    WARN("Unsupported stereo mode: 0x%04x\n", mode);
    return al::nullopt;
}
ALenum EnumFromStereoMode(SourceStereo mode)
{
    switch(mode)
    {
    case SourceStereo::Normal: return AL_NORMAL_SOFT;
    case SourceStereo::Enhanced: return AL_SUPER_STEREO_SOFT;
    }
    throw std::runtime_error{"Invalid SourceStereo: "+std::to_string(int(mode))};
}

al::optional<SpatializeMode> SpatializeModeFromEnum(ALenum mode)
{
    switch(mode)
    {
    case AL_FALSE: return al::make_optional(SpatializeMode::Off);
    case AL_TRUE: return al::make_optional(SpatializeMode::On);
    case AL_AUTO_SOFT: return al::make_optional(SpatializeMode::Auto);
    }
    WARN("Unsupported spatialize mode: 0x%04x\n", mode);
    return al::nullopt;
}
ALenum EnumFromSpatializeMode(SpatializeMode mode)
{
    switch(mode)
    {
    case SpatializeMode::Off: return AL_FALSE;
    case SpatializeMode::On: return AL_TRUE;
    case SpatializeMode::Auto: return AL_AUTO_SOFT;
    }
    throw std::runtime_error{"Invalid SpatializeMode: "+std::to_string(int(mode))};
}

al::optional<DirectMode> DirectModeFromEnum(ALenum mode)
{
    switch(mode)
    {
    case AL_FALSE: return al::make_optional(DirectMode::Off);
    case AL_DROP_UNMATCHED_SOFT: return al::make_optional(DirectMode::DropMismatch);
    case AL_REMIX_UNMATCHED_SOFT: return al::make_optional(DirectMode::RemixMismatch);
    }
    WARN("Unsupported direct mode: 0x%04x\n", mode);
    return al::nullopt;
}
ALenum EnumFromDirectMode(DirectMode mode)
{
    switch(mode)
    {
    case DirectMode::Off: return AL_FALSE;
    case DirectMode::DropMismatch: return AL_DROP_UNMATCHED_SOFT;
    case DirectMode::RemixMismatch: return AL_REMIX_UNMATCHED_SOFT;
    }
    throw std::runtime_error{"Invalid DirectMode: "+std::to_string(int(mode))};
}

al::optional<DistanceModel> DistanceModelFromALenum(ALenum model)
{
    switch(model)
    {
    case AL_NONE: return al::make_optional(DistanceModel::Disable);
    case AL_INVERSE_DISTANCE: return al::make_optional(DistanceModel::Inverse);
    case AL_INVERSE_DISTANCE_CLAMPED: return al::make_optional(DistanceModel::InverseClamped);
    case AL_LINEAR_DISTANCE: return al::make_optional(DistanceModel::Linear);
    case AL_LINEAR_DISTANCE_CLAMPED: return al::make_optional(DistanceModel::LinearClamped);
    case AL_EXPONENT_DISTANCE: return al::make_optional(DistanceModel::Exponent);
    case AL_EXPONENT_DISTANCE_CLAMPED: return al::make_optional(DistanceModel::ExponentClamped);
    }
    return al::nullopt;
}
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
    throw std::runtime_error{"Unexpected distance model "+std::to_string(static_cast<int>(model))};
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
};


constexpr size_t MaxValues{6u};

ALuint FloatValsByProp(ALenum prop)
{
    switch(static_cast<SourceProp>(prop))
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
    case AL_SOURCE_RADIUS:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SEC_LENGTH_SOFT:
    case AL_STEREO_MODE_SOFT:
    case AL_SUPER_STEREO_WIDTH_SOFT:
        return 1;

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
ALuint DoubleValsByProp(ALenum prop)
{
    switch(static_cast<SourceProp>(prop))
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
    case AL_SOURCE_RADIUS:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SEC_LENGTH_SOFT:
    case AL_STEREO_MODE_SOFT:
    case AL_SUPER_STEREO_WIDTH_SOFT:
        return 1;

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


void SetSourcefv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<const float> values);
void SetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<const int> values);
void SetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<const int64_t> values);

#define CHECKSIZE(v, s) do { \
    if LIKELY((v).size() == (s) || (v).size() == MaxValues) break;            \
    Context->setError(AL_INVALID_ENUM,                                        \
        "Property 0x%04x expects %d value(s), got %zu", prop, (s),            \
        (v).size());                                                          \
    return;                                                                   \
} while(0)
#define CHECKVAL(x) do {                                                      \
    if LIKELY(x) break;                                                       \
    Context->setError(AL_INVALID_VALUE, "Value out of range");                \
    return;                                                                   \
} while(0)

void UpdateSourceProps(ALsource *source, ALCcontext *context)
{
    if(!context->mDeferUpdates)
    {
        if(Voice *voice{GetSourceVoice(source, context)})
        {
            UpdateSourceProps(source, voice, context);
            return;
        }
    }
    source->mPropsDirty = true;
}
#ifdef ALSOFT_EAX
void CommitAndUpdateSourceProps(ALsource *source, ALCcontext *context)
{
    if(!context->mDeferUpdates)
    {
        if(source->eax_is_initialized())
            source->eax_commit();
        if(Voice *voice{GetSourceVoice(source, context)})
        {
            UpdateSourceProps(source, voice, context);
            return;
        }
    }
    source->mPropsDirty = true;
}

#else

inline void CommitAndUpdateSourceProps(ALsource *source, ALCcontext *context)
{ UpdateSourceProps(source, context); }
#endif


void SetSourcefv(ALsource *Source, ALCcontext *Context, SourceProp prop,
    const al::span<const float> values)
{
    int ival;

    switch(prop)
    {
    case AL_SEC_LENGTH_SOFT:
    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
        /* Query only */
        SETERR_RETURN(Context, AL_INVALID_OPERATION,,
            "Setting read-only source property 0x%04x", prop);

    case AL_PITCH:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f);

        Source->Pitch = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_CONE_INNER_ANGLE:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f && values[0] <= 360.0f);

        Source->InnerAngle = values[0];
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_CONE_OUTER_ANGLE:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f && values[0] <= 360.0f);

        Source->OuterAngle = values[0];
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_GAIN:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f);

        Source->Gain = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_MAX_DISTANCE:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f);

        Source->MaxDistance = values[0];
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_ROLLOFF_FACTOR:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f);

        Source->RolloffFactor = values[0];
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_REFERENCE_DISTANCE:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f);

        Source->RefDistance = values[0];
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_MIN_GAIN:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f);

        Source->MinGain = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_MAX_GAIN:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f);

        Source->MaxGain = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_CONE_OUTER_GAIN:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f && values[0] <= 1.0f);

        Source->OuterGain = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_CONE_OUTER_GAINHF:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f && values[0] <= 1.0f);

        Source->OuterGainHF = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_AIR_ABSORPTION_FACTOR:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f && values[0] <= 10.0f);

        Source->AirAbsorptionFactor = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_ROOM_ROLLOFF_FACTOR:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f && values[0] <= 10.0f);

        Source->RoomRolloffFactor = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_DOPPLER_FACTOR:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f && values[0] <= 1.0f);

        Source->DopplerFactor = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_SEC_OFFSET:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f);

        if(Voice *voice{GetSourceVoice(Source, Context)})
        {
            auto vpos = GetSampleOffset(Source->mQueue, prop, values[0]);
            if(!vpos) SETERR_RETURN(Context, AL_INVALID_VALUE,, "Invalid offset");

            if(SetVoiceOffset(voice, *vpos, Source, Context, Context->mALDevice.get()))
                return;
        }
        Source->OffsetType = prop;
        Source->Offset = values[0];
        return;

    case AL_SOURCE_RADIUS:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f && std::isfinite(values[0]));

        Source->Radius = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_SUPER_STEREO_WIDTH_SOFT:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0.0f && values[0] <= 1.0f);

        Source->EnhWidth = values[0];
        return UpdateSourceProps(Source, Context);

    case AL_STEREO_ANGLES:
        CHECKSIZE(values, 2);
        CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]));

        Source->StereoPan[0] = values[0];
        Source->StereoPan[1] = values[1];
        return UpdateSourceProps(Source, Context);


    case AL_POSITION:
        CHECKSIZE(values, 3);
        CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]));

        Source->Position[0] = values[0];
        Source->Position[1] = values[1];
        Source->Position[2] = values[2];
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_VELOCITY:
        CHECKSIZE(values, 3);
        CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]));

        Source->Velocity[0] = values[0];
        Source->Velocity[1] = values[1];
        Source->Velocity[2] = values[2];
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_DIRECTION:
        CHECKSIZE(values, 3);
        CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]));

        Source->Direction[0] = values[0];
        Source->Direction[1] = values[1];
        Source->Direction[2] = values[2];
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_ORIENTATION:
        CHECKSIZE(values, 6);
        CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2])
            && std::isfinite(values[3]) && std::isfinite(values[4]) && std::isfinite(values[5]));

        Source->OrientAt[0] = values[0];
        Source->OrientAt[1] = values[1];
        Source->OrientAt[2] = values[2];
        Source->OrientUp[0] = values[3];
        Source->OrientUp[1] = values[4];
        Source->OrientUp[2] = values[5];
        return UpdateSourceProps(Source, Context);


    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
    case AL_SOURCE_STATE:
    case AL_SOURCE_TYPE:
    case AL_DISTANCE_MODEL:
    case AL_DIRECT_FILTER_GAINHF_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
    case AL_DIRECT_CHANNELS_SOFT:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_STEREO_MODE_SOFT:
        CHECKSIZE(values, 1);
        ival = static_cast<int>(values[0]);
        return SetSourceiv(Source, Context, prop, {&ival, 1u});

    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
        CHECKSIZE(values, 1);
        ival = static_cast<int>(static_cast<ALuint>(values[0]));
        return SetSourceiv(Source, Context, prop, {&ival, 1u});

    case AL_BUFFER:
    case AL_DIRECT_FILTER:
    case AL_AUXILIARY_SEND_FILTER:
    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
        break;
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    Context->setError(AL_INVALID_ENUM, "Invalid source float property 0x%04x", prop);
    return;
}

void SetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop,
    const al::span<const int> values)
{
    ALCdevice *device{Context->mALDevice.get()};
    ALeffectslot *slot{nullptr};
    al::deque<ALbufferQueueItem> oldlist;
    std::unique_lock<std::mutex> slotlock;
    float fvals[6];

    switch(prop)
    {
    case AL_SOURCE_STATE:
    case AL_SOURCE_TYPE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
        /* Query only */
        SETERR_RETURN(Context, AL_INVALID_OPERATION,,
            "Setting read-only source property 0x%04x", prop);

    case AL_SOURCE_RELATIVE:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

        Source->HeadRelative = values[0] != AL_FALSE;
        return CommitAndUpdateSourceProps(Source, Context);

    case AL_LOOPING:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

        Source->Looping = values[0] != AL_FALSE;
        if(Voice *voice{GetSourceVoice(Source, Context)})
        {
            if(Source->Looping)
                voice->mLoopBuffer.store(&Source->mQueue.front(), std::memory_order_release);
            else
                voice->mLoopBuffer.store(nullptr, std::memory_order_release);

            /* If the source is playing, wait for the current mix to finish to
             * ensure it isn't currently looping back or reaching the end.
             */
            device->waitForMix();
        }
        return;

    case AL_BUFFER:
        CHECKSIZE(values, 1);
        {
            const ALenum state{GetSourceState(Source, GetSourceVoice(Source, Context))};
            if(state == AL_PLAYING || state == AL_PAUSED)
                SETERR_RETURN(Context, AL_INVALID_OPERATION,,
                    "Setting buffer on playing or paused source %u", Source->id);
        }
        if(values[0])
        {
            std::lock_guard<std::mutex> _{device->BufferLock};
            ALbuffer *buffer{LookupBuffer(device, static_cast<ALuint>(values[0]))};
            if(!buffer)
                SETERR_RETURN(Context, AL_INVALID_VALUE,, "Invalid buffer ID %u",
                    static_cast<ALuint>(values[0]));
            if(buffer->MappedAccess && !(buffer->MappedAccess&AL_MAP_PERSISTENT_BIT_SOFT))
                SETERR_RETURN(Context, AL_INVALID_OPERATION,,
                    "Setting non-persistently mapped buffer %u", buffer->id);
            if(buffer->mCallback && ReadRef(buffer->ref) != 0)
                SETERR_RETURN(Context, AL_INVALID_OPERATION,,
                    "Setting already-set callback buffer %u", buffer->id);

            /* Add the selected buffer to a one-item queue */
            al::deque<ALbufferQueueItem> newlist;
            newlist.emplace_back();
            newlist.back().mCallback = buffer->mCallback;
            newlist.back().mUserData = buffer->mUserData;
            newlist.back().mSampleLen = buffer->mSampleLen;
            newlist.back().mLoopStart = buffer->mLoopStart;
            newlist.back().mLoopEnd = buffer->mLoopEnd;
            newlist.back().mSamples = buffer->mData.data();
            newlist.back().mBuffer = buffer;
            IncrementRef(buffer->ref);

            /* Source is now Static */
            Source->SourceType = AL_STATIC;
            Source->mQueue.swap(oldlist);
            Source->mQueue.swap(newlist);
        }
        else
        {
            /* Source is now Undetermined */
            Source->SourceType = AL_UNDETERMINED;
            Source->mQueue.swap(oldlist);
        }

        /* Delete all elements in the previous queue */
        for(auto &item : oldlist)
        {
            if(ALbuffer *buffer{item.mBuffer})
                DecrementRef(buffer->ref);
        }
        return;

    case AL_SEC_OFFSET:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0);

        if(Voice *voice{GetSourceVoice(Source, Context)})
        {
            auto vpos = GetSampleOffset(Source->mQueue, prop, values[0]);
            if(!vpos) SETERR_RETURN(Context, AL_INVALID_VALUE,, "Invalid source offset");

            if(SetVoiceOffset(voice, *vpos, Source, Context, device))
                return;
        }
        Source->OffsetType = prop;
        Source->Offset = values[0];
        return;

    case AL_DIRECT_FILTER:
        CHECKSIZE(values, 1);
        if(values[0])
        {
            std::lock_guard<std::mutex> _{device->FilterLock};
            ALfilter *filter{LookupFilter(device, static_cast<ALuint>(values[0]))};
            if(!filter)
                SETERR_RETURN(Context, AL_INVALID_VALUE,, "Invalid filter ID %u",
                    static_cast<ALuint>(values[0]));
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
            Source->Direct.HFReference = LOWPASSFREQREF;
            Source->Direct.GainLF = 1.0f;
            Source->Direct.LFReference = HIGHPASSFREQREF;
        }
        return UpdateSourceProps(Source, Context);

    case AL_DIRECT_FILTER_GAINHF_AUTO:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

        Source->DryGainHFAuto = values[0] != AL_FALSE;
        return UpdateSourceProps(Source, Context);

    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

        Source->WetGainAuto = values[0] != AL_FALSE;
        return UpdateSourceProps(Source, Context);

    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

        Source->WetGainHFAuto = values[0] != AL_FALSE;
        return UpdateSourceProps(Source, Context);

    case AL_DIRECT_CHANNELS_SOFT:
        CHECKSIZE(values, 1);
        if(auto mode = DirectModeFromEnum(values[0]))
        {
            Source->DirectChannels = *mode;
            return UpdateSourceProps(Source, Context);
        }
        Context->setError(AL_INVALID_VALUE, "Unsupported AL_DIRECT_CHANNELS_SOFT: 0x%04x\n",
            values[0]);
        return;

    case AL_DISTANCE_MODEL:
        CHECKSIZE(values, 1);
        if(auto model = DistanceModelFromALenum(values[0]))
        {
            Source->mDistanceModel = *model;
            if(Context->mSourceDistanceModel)
                UpdateSourceProps(Source, Context);
            return;
        }
        Context->setError(AL_INVALID_VALUE, "Distance model out of range: 0x%04x", values[0]);
        return;

    case AL_SOURCE_RESAMPLER_SOFT:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] >= 0 && values[0] <= static_cast<int>(Resampler::Max));

        Source->mResampler = static_cast<Resampler>(values[0]);
        return UpdateSourceProps(Source, Context);

    case AL_SOURCE_SPATIALIZE_SOFT:
        CHECKSIZE(values, 1);
        if(auto mode = SpatializeModeFromEnum(values[0]))
        {
            Source->mSpatialize = *mode;
            return UpdateSourceProps(Source, Context);
        }
        Context->setError(AL_INVALID_VALUE, "Unsupported AL_SOURCE_SPATIALIZE_SOFT: 0x%04x\n",
            values[0]);
        return;

    case AL_STEREO_MODE_SOFT:
        CHECKSIZE(values, 1);
        {
            const ALenum state{GetSourceState(Source, GetSourceVoice(Source, Context))};
            if(state == AL_PLAYING || state == AL_PAUSED)
                SETERR_RETURN(Context, AL_INVALID_OPERATION,,
                    "Modifying stereo mode on playing or paused source %u", Source->id);
        }
        if(auto mode = StereoModeFromEnum(values[0]))
        {
            Source->mStereoMode = *mode;
            return;
        }
        Context->setError(AL_INVALID_VALUE, "Unsupported AL_STEREO_MODE_SOFT: 0x%04x\n",
            values[0]);
        return;

    case AL_AUXILIARY_SEND_FILTER:
        CHECKSIZE(values, 3);
        slotlock = std::unique_lock<std::mutex>{Context->mEffectSlotLock};
        if(values[0] && (slot=LookupEffectSlot(Context, static_cast<ALuint>(values[0]))) == nullptr)
            SETERR_RETURN(Context, AL_INVALID_VALUE,, "Invalid effect ID %u", values[0]);
        if(static_cast<ALuint>(values[1]) >= device->NumAuxSends)
            SETERR_RETURN(Context, AL_INVALID_VALUE,, "Invalid send %u", values[1]);

        if(values[2])
        {
            std::lock_guard<std::mutex> _{device->FilterLock};
            ALfilter *filter{LookupFilter(device, static_cast<ALuint>(values[2]))};
            if(!filter)
                SETERR_RETURN(Context, AL_INVALID_VALUE,, "Invalid filter ID %u", values[2]);

            auto &send = Source->Send[static_cast<ALuint>(values[1])];
            send.Gain = filter->Gain;
            send.GainHF = filter->GainHF;
            send.HFReference = filter->HFReference;
            send.GainLF = filter->GainLF;
            send.LFReference = filter->LFReference;
        }
        else
        {
            /* Disable filter */
            auto &send = Source->Send[static_cast<ALuint>(values[1])];
            send.Gain = 1.0f;
            send.GainHF = 1.0f;
            send.HFReference = LOWPASSFREQREF;
            send.GainLF = 1.0f;
            send.LFReference = HIGHPASSFREQREF;
        }

        if(slot != Source->Send[static_cast<ALuint>(values[1])].Slot && IsPlayingOrPaused(Source))
        {
            /* Add refcount on the new slot, and release the previous slot */
            if(slot) IncrementRef(slot->ref);
            if(auto *oldslot = Source->Send[static_cast<ALuint>(values[1])].Slot)
                DecrementRef(oldslot->ref);
            Source->Send[static_cast<ALuint>(values[1])].Slot = slot;

            /* We must force an update if the auxiliary slot changed on an
             * active source, in case the slot is about to be deleted.
             */
            Voice *voice{GetSourceVoice(Source, Context)};
            if(voice) UpdateSourceProps(Source, voice, Context);
            else Source->mPropsDirty = true;
        }
        else
        {
            if(slot) IncrementRef(slot->ref);
            if(auto *oldslot = Source->Send[static_cast<ALuint>(values[1])].Slot)
                DecrementRef(oldslot->ref);
            Source->Send[static_cast<ALuint>(values[1])].Slot = slot;
            UpdateSourceProps(Source, Context);
        }
        return;


    /* 1x float */
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
    case AL_DOPPLER_FACTOR:
    case AL_CONE_OUTER_GAINHF:
    case AL_AIR_ABSORPTION_FACTOR:
    case AL_ROOM_ROLLOFF_FACTOR:
    case AL_SOURCE_RADIUS:
    case AL_SEC_LENGTH_SOFT:
    case AL_SUPER_STEREO_WIDTH_SOFT:
        CHECKSIZE(values, 1);
        fvals[0] = static_cast<float>(values[0]);
        return SetSourcefv(Source, Context, prop, {fvals, 1u});

    /* 3x float */
    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION:
        CHECKSIZE(values, 3);
        fvals[0] = static_cast<float>(values[0]);
        fvals[1] = static_cast<float>(values[1]);
        fvals[2] = static_cast<float>(values[2]);
        return SetSourcefv(Source, Context, prop, {fvals, 3u});

    /* 6x float */
    case AL_ORIENTATION:
        CHECKSIZE(values, 6);
        fvals[0] = static_cast<float>(values[0]);
        fvals[1] = static_cast<float>(values[1]);
        fvals[2] = static_cast<float>(values[2]);
        fvals[3] = static_cast<float>(values[3]);
        fvals[4] = static_cast<float>(values[4]);
        fvals[5] = static_cast<float>(values[5]);
        return SetSourcefv(Source, Context, prop, {fvals, 6u});

    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
    case AL_STEREO_ANGLES:
        break;
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    Context->setError(AL_INVALID_ENUM, "Invalid source integer property 0x%04x", prop);
    return;
}

void SetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop,
    const al::span<const int64_t> values)
{
    float fvals[MaxValues];
    int   ivals[MaxValues];

    switch(prop)
    {
    case AL_SOURCE_TYPE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
    case AL_SOURCE_STATE:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
        /* Query only */
        SETERR_RETURN(Context, AL_INVALID_OPERATION,,
            "Setting read-only source property 0x%04x", prop);

    /* 1x int */
    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
    case AL_SEC_OFFSET:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
    case AL_DIRECT_FILTER_GAINHF_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
    case AL_DIRECT_CHANNELS_SOFT:
    case AL_DISTANCE_MODEL:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_STEREO_MODE_SOFT:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] <= INT_MAX && values[0] >= INT_MIN);

        ivals[0] = static_cast<int>(values[0]);
        return SetSourceiv(Source, Context, prop, {ivals, 1u});

    /* 1x uint */
    case AL_BUFFER:
    case AL_DIRECT_FILTER:
        CHECKSIZE(values, 1);
        CHECKVAL(values[0] <= UINT_MAX && values[0] >= 0);

        ivals[0] = static_cast<int>(values[0]);
        return SetSourceiv(Source, Context, prop, {ivals, 1u});

    /* 3x uint */
    case AL_AUXILIARY_SEND_FILTER:
        CHECKSIZE(values, 3);
        CHECKVAL(values[0] <= UINT_MAX && values[0] >= 0 && values[1] <= UINT_MAX && values[1] >= 0
            && values[2] <= UINT_MAX && values[2] >= 0);

        ivals[0] = static_cast<int>(values[0]);
        ivals[1] = static_cast<int>(values[1]);
        ivals[2] = static_cast<int>(values[2]);
        return SetSourceiv(Source, Context, prop, {ivals, 3u});

    /* 1x float */
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
    case AL_DOPPLER_FACTOR:
    case AL_CONE_OUTER_GAINHF:
    case AL_AIR_ABSORPTION_FACTOR:
    case AL_ROOM_ROLLOFF_FACTOR:
    case AL_SOURCE_RADIUS:
    case AL_SEC_LENGTH_SOFT:
    case AL_SUPER_STEREO_WIDTH_SOFT:
        CHECKSIZE(values, 1);
        fvals[0] = static_cast<float>(values[0]);
        return SetSourcefv(Source, Context, prop, {fvals, 1u});

    /* 3x float */
    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION:
        CHECKSIZE(values, 3);
        fvals[0] = static_cast<float>(values[0]);
        fvals[1] = static_cast<float>(values[1]);
        fvals[2] = static_cast<float>(values[2]);
        return SetSourcefv(Source, Context, prop, {fvals, 3u});

    /* 6x float */
    case AL_ORIENTATION:
        CHECKSIZE(values, 6);
        fvals[0] = static_cast<float>(values[0]);
        fvals[1] = static_cast<float>(values[1]);
        fvals[2] = static_cast<float>(values[2]);
        fvals[3] = static_cast<float>(values[3]);
        fvals[4] = static_cast<float>(values[4]);
        fvals[5] = static_cast<float>(values[5]);
        return SetSourcefv(Source, Context, prop, {fvals, 6u});

    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
    case AL_STEREO_ANGLES:
        break;
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    Context->setError(AL_INVALID_ENUM, "Invalid source integer64 property 0x%04x", prop);
    return;
}

#undef CHECKVAL
#undef CHECKSIZE

#define CHECKSIZE(v, s) do { \
    if LIKELY((v).size() == (s) || (v).size() == MaxValues) break;            \
    Context->setError(AL_INVALID_ENUM,                                        \
        "Property 0x%04x expects %d value(s), got %zu", prop, (s),            \
        (v).size());                                                          \
    return false;                                                             \
} while(0)

bool GetSourcedv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<double> values);
bool GetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<int> values);
bool GetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<int64_t> values);

bool GetSourcedv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<double> values)
{
    ALCdevice *device{Context->mALDevice.get()};
    ClockLatency clocktime;
    nanoseconds srcclock;
    int ivals[MaxValues];
    bool err;

    switch(prop)
    {
    case AL_GAIN:
        CHECKSIZE(values, 1);
        values[0] = Source->Gain;
        return true;

    case AL_PITCH:
        CHECKSIZE(values, 1);
        values[0] = Source->Pitch;
        return true;

    case AL_MAX_DISTANCE:
        CHECKSIZE(values, 1);
        values[0] = Source->MaxDistance;
        return true;

    case AL_ROLLOFF_FACTOR:
        CHECKSIZE(values, 1);
        values[0] = Source->RolloffFactor;
        return true;

    case AL_REFERENCE_DISTANCE:
        CHECKSIZE(values, 1);
        values[0] = Source->RefDistance;
        return true;

    case AL_CONE_INNER_ANGLE:
        CHECKSIZE(values, 1);
        values[0] = Source->InnerAngle;
        return true;

    case AL_CONE_OUTER_ANGLE:
        CHECKSIZE(values, 1);
        values[0] = Source->OuterAngle;
        return true;

    case AL_MIN_GAIN:
        CHECKSIZE(values, 1);
        values[0] = Source->MinGain;
        return true;

    case AL_MAX_GAIN:
        CHECKSIZE(values, 1);
        values[0] = Source->MaxGain;
        return true;

    case AL_CONE_OUTER_GAIN:
        CHECKSIZE(values, 1);
        values[0] = Source->OuterGain;
        return true;

    case AL_SEC_OFFSET:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
        CHECKSIZE(values, 1);
        values[0] = GetSourceOffset(Source, prop, Context);
        return true;

    case AL_CONE_OUTER_GAINHF:
        CHECKSIZE(values, 1);
        values[0] = Source->OuterGainHF;
        return true;

    case AL_AIR_ABSORPTION_FACTOR:
        CHECKSIZE(values, 1);
        values[0] = Source->AirAbsorptionFactor;
        return true;

    case AL_ROOM_ROLLOFF_FACTOR:
        CHECKSIZE(values, 1);
        values[0] = Source->RoomRolloffFactor;
        return true;

    case AL_DOPPLER_FACTOR:
        CHECKSIZE(values, 1);
        values[0] = Source->DopplerFactor;
        return true;

    case AL_SOURCE_RADIUS:
        CHECKSIZE(values, 1);
        values[0] = Source->Radius;
        return true;

    case AL_SUPER_STEREO_WIDTH_SOFT:
        CHECKSIZE(values, 1);
        values[0] = Source->EnhWidth;
        return true;

    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SEC_LENGTH_SOFT:
        CHECKSIZE(values, 1);
        values[0] = GetSourceLength(Source, prop);
        return true;

    case AL_STEREO_ANGLES:
        CHECKSIZE(values, 2);
        values[0] = Source->StereoPan[0];
        values[1] = Source->StereoPan[1];
        return true;

    case AL_SEC_OFFSET_LATENCY_SOFT:
        CHECKSIZE(values, 2);
        /* Get the source offset with the clock time first. Then get the clock
         * time with the device latency. Order is important.
         */
        values[0] = GetSourceSecOffset(Source, Context, &srcclock);
        {
            std::lock_guard<std::mutex> _{device->StateLock};
            clocktime = GetClockLatency(device, device->Backend.get());
        }
        if(srcclock == clocktime.ClockTime)
            values[1] = static_cast<double>(clocktime.Latency.count()) / 1000000000.0;
        else
        {
            /* If the clock time incremented, reduce the latency by that much
             * since it's that much closer to the source offset it got earlier.
             */
            const nanoseconds diff{clocktime.ClockTime - srcclock};
            const nanoseconds latency{clocktime.Latency - std::min(clocktime.Latency, diff)};
            values[1] = static_cast<double>(latency.count()) / 1000000000.0;
        }
        return true;

    case AL_SEC_OFFSET_CLOCK_SOFT:
        CHECKSIZE(values, 2);
        values[0] = GetSourceSecOffset(Source, Context, &srcclock);
        values[1] = static_cast<double>(srcclock.count()) / 1000000000.0;
        return true;

    case AL_POSITION:
        CHECKSIZE(values, 3);
        values[0] = Source->Position[0];
        values[1] = Source->Position[1];
        values[2] = Source->Position[2];
        return true;

    case AL_VELOCITY:
        CHECKSIZE(values, 3);
        values[0] = Source->Velocity[0];
        values[1] = Source->Velocity[1];
        values[2] = Source->Velocity[2];
        return true;

    case AL_DIRECTION:
        CHECKSIZE(values, 3);
        values[0] = Source->Direction[0];
        values[1] = Source->Direction[1];
        values[2] = Source->Direction[2];
        return true;

    case AL_ORIENTATION:
        CHECKSIZE(values, 6);
        values[0] = Source->OrientAt[0];
        values[1] = Source->OrientAt[1];
        values[2] = Source->OrientAt[2];
        values[3] = Source->OrientUp[0];
        values[4] = Source->OrientUp[1];
        values[5] = Source->OrientUp[2];
        return true;

    /* 1x int */
    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
    case AL_SOURCE_STATE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
    case AL_SOURCE_TYPE:
    case AL_DIRECT_FILTER_GAINHF_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
    case AL_DIRECT_CHANNELS_SOFT:
    case AL_DISTANCE_MODEL:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_STEREO_MODE_SOFT:
        CHECKSIZE(values, 1);
        if((err=GetSourceiv(Source, Context, prop, {ivals, 1u})) != false)
            values[0] = static_cast<double>(ivals[0]);
        return err;

    case AL_BUFFER:
    case AL_DIRECT_FILTER:
    case AL_AUXILIARY_SEND_FILTER:
    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
        break;
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    Context->setError(AL_INVALID_ENUM, "Invalid source double property 0x%04x", prop);
    return false;
}

bool GetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<int> values)
{
    double dvals[MaxValues];
    bool err;

    switch(prop)
    {
    case AL_SOURCE_RELATIVE:
        CHECKSIZE(values, 1);
        values[0] = Source->HeadRelative;
        return true;

    case AL_LOOPING:
        CHECKSIZE(values, 1);
        values[0] = Source->Looping;
        return true;

    case AL_BUFFER:
        CHECKSIZE(values, 1);
        {
            ALbufferQueueItem *BufferList{(Source->SourceType == AL_STATIC)
                ? &Source->mQueue.front() : nullptr};
            ALbuffer *buffer{BufferList ? BufferList->mBuffer : nullptr};
            values[0] = buffer ? static_cast<int>(buffer->id) : 0;
        }
        return true;

    case AL_SOURCE_STATE:
        CHECKSIZE(values, 1);
        values[0] = GetSourceState(Source, GetSourceVoice(Source, Context));
        return true;

    case AL_BUFFERS_QUEUED:
        CHECKSIZE(values, 1);
        values[0] = static_cast<int>(Source->mQueue.size());
        return true;

    case AL_BUFFERS_PROCESSED:
        CHECKSIZE(values, 1);
        if(Source->Looping || Source->SourceType != AL_STREAMING)
        {
            /* Buffers on a looping source are in a perpetual state of PENDING,
             * so don't report any as PROCESSED
             */
            values[0] = 0;
        }
        else
        {
            int played{0};
            if(Source->state != AL_INITIAL)
            {
                const VoiceBufferItem *Current{nullptr};
                if(Voice *voice{GetSourceVoice(Source, Context)})
                    Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);
                for(auto &item : Source->mQueue)
                {
                    if(&item == Current)
                        break;
                    ++played;
                }
            }
            values[0] = played;
        }
        return true;

    case AL_SOURCE_TYPE:
        CHECKSIZE(values, 1);
        values[0] = Source->SourceType;
        return true;

    case AL_DIRECT_FILTER_GAINHF_AUTO:
        CHECKSIZE(values, 1);
        values[0] = Source->DryGainHFAuto;
        return true;

    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
        CHECKSIZE(values, 1);
        values[0] = Source->WetGainAuto;
        return true;

    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
        CHECKSIZE(values, 1);
        values[0] = Source->WetGainHFAuto;
        return true;

    case AL_DIRECT_CHANNELS_SOFT:
        CHECKSIZE(values, 1);
        values[0] = EnumFromDirectMode(Source->DirectChannels);
        return true;

    case AL_DISTANCE_MODEL:
        CHECKSIZE(values, 1);
        values[0] = ALenumFromDistanceModel(Source->mDistanceModel);
        return true;

    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SEC_LENGTH_SOFT:
        CHECKSIZE(values, 1);
        values[0] = static_cast<int>(mind(GetSourceLength(Source, prop),
            std::numeric_limits<int>::max()));
        return true;

    case AL_SOURCE_RESAMPLER_SOFT:
        CHECKSIZE(values, 1);
        values[0] = static_cast<int>(Source->mResampler);
        return true;

    case AL_SOURCE_SPATIALIZE_SOFT:
        CHECKSIZE(values, 1);
        values[0] = EnumFromSpatializeMode(Source->mSpatialize);
        return true;

    case AL_STEREO_MODE_SOFT:
        CHECKSIZE(values, 1);
        values[0] = EnumFromStereoMode(Source->mStereoMode);
        return true;

    /* 1x float/double */
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
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
    case AL_DOPPLER_FACTOR:
    case AL_AIR_ABSORPTION_FACTOR:
    case AL_ROOM_ROLLOFF_FACTOR:
    case AL_CONE_OUTER_GAINHF:
    case AL_SOURCE_RADIUS:
    case AL_SUPER_STEREO_WIDTH_SOFT:
        CHECKSIZE(values, 1);
        if((err=GetSourcedv(Source, Context, prop, {dvals, 1u})) != false)
            values[0] = static_cast<int>(dvals[0]);
        return err;

    /* 3x float/double */
    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION:
        CHECKSIZE(values, 3);
        if((err=GetSourcedv(Source, Context, prop, {dvals, 3u})) != false)
        {
            values[0] = static_cast<int>(dvals[0]);
            values[1] = static_cast<int>(dvals[1]);
            values[2] = static_cast<int>(dvals[2]);
        }
        return err;

    /* 6x float/double */
    case AL_ORIENTATION:
        CHECKSIZE(values, 6);
        if((err=GetSourcedv(Source, Context, prop, {dvals, 6u})) != false)
        {
            values[0] = static_cast<int>(dvals[0]);
            values[1] = static_cast<int>(dvals[1]);
            values[2] = static_cast<int>(dvals[2]);
            values[3] = static_cast<int>(dvals[3]);
            values[4] = static_cast<int>(dvals[4]);
            values[5] = static_cast<int>(dvals[5]);
        }
        return err;

    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
        break; /* i64 only */
    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
        break; /* Double only */
    case AL_STEREO_ANGLES:
        break; /* Float/double only */

    case AL_DIRECT_FILTER:
    case AL_AUXILIARY_SEND_FILTER:
        break; /* ??? */
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    Context->setError(AL_INVALID_ENUM, "Invalid source integer property 0x%04x", prop);
    return false;
}

bool GetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<int64_t> values)
{
    ALCdevice *device{Context->mALDevice.get()};
    ClockLatency clocktime;
    nanoseconds srcclock;
    double dvals[MaxValues];
    int ivals[MaxValues];
    bool err;

    switch(prop)
    {
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_SEC_LENGTH_SOFT:
        CHECKSIZE(values, 1);
        values[0] = static_cast<int64_t>(GetSourceLength(Source, prop));
        return true;

    case AL_SAMPLE_OFFSET_LATENCY_SOFT:
        CHECKSIZE(values, 2);
        /* Get the source offset with the clock time first. Then get the clock
         * time with the device latency. Order is important.
         */
        values[0] = GetSourceSampleOffset(Source, Context, &srcclock);
        {
            std::lock_guard<std::mutex> _{device->StateLock};
            clocktime = GetClockLatency(device, device->Backend.get());
        }
        if(srcclock == clocktime.ClockTime)
            values[1] = clocktime.Latency.count();
        else
        {
            /* If the clock time incremented, reduce the latency by that much
             * since it's that much closer to the source offset it got earlier.
             */
            const nanoseconds diff{clocktime.ClockTime - srcclock};
            values[1] = nanoseconds{clocktime.Latency - std::min(clocktime.Latency, diff)}.count();
        }
        return true;

    case AL_SAMPLE_OFFSET_CLOCK_SOFT:
        CHECKSIZE(values, 2);
        values[0] = GetSourceSampleOffset(Source, Context, &srcclock);
        values[1] = srcclock.count();
        return true;

    /* 1x float/double */
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
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
    case AL_DOPPLER_FACTOR:
    case AL_AIR_ABSORPTION_FACTOR:
    case AL_ROOM_ROLLOFF_FACTOR:
    case AL_CONE_OUTER_GAINHF:
    case AL_SOURCE_RADIUS:
    case AL_SUPER_STEREO_WIDTH_SOFT:
        CHECKSIZE(values, 1);
        if((err=GetSourcedv(Source, Context, prop, {dvals, 1u})) != false)
            values[0] = static_cast<int64_t>(dvals[0]);
        return err;

    /* 3x float/double */
    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION:
        CHECKSIZE(values, 3);
        if((err=GetSourcedv(Source, Context, prop, {dvals, 3u})) != false)
        {
            values[0] = static_cast<int64_t>(dvals[0]);
            values[1] = static_cast<int64_t>(dvals[1]);
            values[2] = static_cast<int64_t>(dvals[2]);
        }
        return err;

    /* 6x float/double */
    case AL_ORIENTATION:
        CHECKSIZE(values, 6);
        if((err=GetSourcedv(Source, Context, prop, {dvals, 6u})) != false)
        {
            values[0] = static_cast<int64_t>(dvals[0]);
            values[1] = static_cast<int64_t>(dvals[1]);
            values[2] = static_cast<int64_t>(dvals[2]);
            values[3] = static_cast<int64_t>(dvals[3]);
            values[4] = static_cast<int64_t>(dvals[4]);
            values[5] = static_cast<int64_t>(dvals[5]);
        }
        return err;

    /* 1x int */
    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
    case AL_SOURCE_STATE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
    case AL_SOURCE_TYPE:
    case AL_DIRECT_FILTER_GAINHF_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
    case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
    case AL_DIRECT_CHANNELS_SOFT:
    case AL_DISTANCE_MODEL:
    case AL_SOURCE_RESAMPLER_SOFT:
    case AL_SOURCE_SPATIALIZE_SOFT:
    case AL_STEREO_MODE_SOFT:
        CHECKSIZE(values, 1);
        if((err=GetSourceiv(Source, Context, prop, {ivals, 1u})) != false)
            values[0] = ivals[0];
        return err;

    /* 1x uint */
    case AL_BUFFER:
    case AL_DIRECT_FILTER:
        CHECKSIZE(values, 1);
        if((err=GetSourceiv(Source, Context, prop, {ivals, 1u})) != false)
            values[0] = static_cast<ALuint>(ivals[0]);
        return err;

    /* 3x uint */
    case AL_AUXILIARY_SEND_FILTER:
        CHECKSIZE(values, 3);
        if((err=GetSourceiv(Source, Context, prop, {ivals, 3u})) != false)
        {
            values[0] = static_cast<ALuint>(ivals[0]);
            values[1] = static_cast<ALuint>(ivals[1]);
            values[2] = static_cast<ALuint>(ivals[2]);
        }
        return err;

    case AL_SEC_OFFSET_LATENCY_SOFT:
    case AL_SEC_OFFSET_CLOCK_SOFT:
        break; /* Double only */
    case AL_STEREO_ANGLES:
        break; /* Float/double only */
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    Context->setError(AL_INVALID_ENUM, "Invalid source integer64 property 0x%04x", prop);
    return false;
}

} // namespace

AL_API void AL_APIENTRY alGenSources(ALsizei n, ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Generating %d sources", n);
    if UNLIKELY(n <= 0) return;

#ifdef ALSOFT_EAX
    const bool has_eax{context->has_eax()};
    std::unique_lock<std::mutex> proplock{};
    if(has_eax)
        proplock = std::unique_lock<std::mutex>{context->mPropLock};
#endif
    std::unique_lock<std::mutex> srclock{context->mSourceLock};
    ALCdevice *device{context->mALDevice.get()};
    if(static_cast<ALuint>(n) > device->SourcesMax-context->mNumSources)
    {
        context->setError(AL_OUT_OF_MEMORY, "Exceeding %u source limit (%u + %d)",
            device->SourcesMax, context->mNumSources, n);
        return;
    }
    if(!EnsureSources(context.get(), static_cast<ALuint>(n)))
    {
        context->setError(AL_OUT_OF_MEMORY, "Failed to allocate %d source%s", n, (n==1)?"":"s");
        return;
    }

    if(n == 1)
    {
        ALsource *source{AllocSource(context.get())};
        sources[0] = source->id;

#ifdef ALSOFT_EAX
        if(has_eax)
            source->eax_initialize(context.get());
#endif // ALSOFT_EAX
    }
    else
    {
#ifdef ALSOFT_EAX
        auto eax_sources = al::vector<ALsource*>{};
        if(has_eax)
            eax_sources.reserve(static_cast<ALuint>(n));
#endif // ALSOFT_EAX

        al::vector<ALuint> ids;
        ids.reserve(static_cast<ALuint>(n));
        do {
            ALsource *source{AllocSource(context.get())};
            ids.emplace_back(source->id);

#ifdef ALSOFT_EAX
            if(has_eax)
                eax_sources.emplace_back(source);
#endif // ALSOFT_EAX
        } while(--n);
        std::copy(ids.cbegin(), ids.cend(), sources);

#ifdef ALSOFT_EAX
        for(auto& eax_source : eax_sources)
            eax_source->eax_initialize(context.get());
#endif // ALSOFT_EAX
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alDeleteSources(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        SETERR_RETURN(context, AL_INVALID_VALUE,, "Deleting %d sources", n);

    std::lock_guard<std::mutex> _{context->mSourceLock};

    /* Check that all Sources are valid */
    auto validate_source = [&context](const ALuint sid) -> bool
    { return LookupSource(context.get(), sid) != nullptr; };

    const ALuint *sources_end = sources + n;
    auto invsrc = std::find_if_not(sources, sources_end, validate_source);
    if UNLIKELY(invsrc != sources_end)
    {
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", *invsrc);
        return;
    }

    /* All good. Delete source IDs. */
    auto delete_source = [&context](const ALuint sid) -> void
    {
        ALsource *src{LookupSource(context.get(), sid)};
        if(src) FreeSource(context.get(), src);
    };
    std::for_each(sources, sources_end, delete_source);
}
END_API_FUNC

AL_API ALboolean AL_APIENTRY alIsSource(ALuint source)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if LIKELY(context)
    {
        std::lock_guard<std::mutex> _{context->mSourceLock};
        if(LookupSource(context.get(), source) != nullptr)
            return AL_TRUE;
    }
    return AL_FALSE;
}
END_API_FUNC


AL_API void AL_APIENTRY alSourcef(ALuint source, ALenum param, ALfloat value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), {&value, 1u});
}
END_API_FUNC

AL_API void AL_APIENTRY alSource3f(ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else
    {
        const float fvals[3]{ value1, value2, value3 };
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), fvals);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alSourcefv(ALuint source, ALenum param, const ALfloat *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), {values, MaxValues});
}
END_API_FUNC


AL_API void AL_APIENTRY alSourcedSOFT(ALuint source, ALenum param, ALdouble value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else
    {
        const float fval[1]{static_cast<float>(value)};
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), fval);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alSource3dSOFT(ALuint source, ALenum param, ALdouble value1, ALdouble value2, ALdouble value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else
    {
        const float fvals[3]{static_cast<float>(value1), static_cast<float>(value2),
            static_cast<float>(value3)};
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), fvals);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alSourcedvSOFT(ALuint source, ALenum param, const ALdouble *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
    {
        const ALuint count{DoubleValsByProp(param)};
        float fvals[MaxValues];
        for(ALuint i{0};i < count;i++)
            fvals[i] = static_cast<float>(values[i]);
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), {fvals, count});
    }
}
END_API_FUNC


AL_API void AL_APIENTRY alSourcei(ALuint source, ALenum param, ALint value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else
        SetSourceiv(Source, context.get(), static_cast<SourceProp>(param), {&value, 1u});
}
END_API_FUNC

AL_API void AL_APIENTRY alSource3i(ALuint source, ALenum param, ALint value1, ALint value2, ALint value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else
    {
        const int ivals[3]{ value1, value2, value3 };
        SetSourceiv(Source, context.get(), static_cast<SourceProp>(param), ivals);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alSourceiv(ALuint source, ALenum param, const ALint *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
        SetSourceiv(Source, context.get(), static_cast<SourceProp>(param), {values, MaxValues});
}
END_API_FUNC


AL_API void AL_APIENTRY alSourcei64SOFT(ALuint source, ALenum param, ALint64SOFT value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else
        SetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), {&value, 1u});
}
END_API_FUNC

AL_API void AL_APIENTRY alSource3i64SOFT(ALuint source, ALenum param, ALint64SOFT value1, ALint64SOFT value2, ALint64SOFT value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else
    {
        const int64_t i64vals[3]{ value1, value2, value3 };
        SetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), i64vals);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alSourcei64vSOFT(ALuint source, ALenum param, const ALint64SOFT *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
        SetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), {values, MaxValues});
}
END_API_FUNC


AL_API void AL_APIENTRY alGetSourcef(ALuint source, ALenum param, ALfloat *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!value)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
    {
        double dval[1];
        if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), dval))
            *value = static_cast<float>(dval[0]);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetSource3f(ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!(value1 && value2 && value3))
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
    {
        double dvals[3];
        if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), dvals))
        {
            *value1 = static_cast<float>(dvals[0]);
            *value2 = static_cast<float>(dvals[1]);
            *value3 = static_cast<float>(dvals[2]);
        }
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetSourcefv(ALuint source, ALenum param, ALfloat *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
    {
        const ALuint count{FloatValsByProp(param)};
        double dvals[MaxValues];
        if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), {dvals, count}))
        {
            for(ALuint i{0};i < count;i++)
                values[i] = static_cast<float>(dvals[i]);
        }
    }
}
END_API_FUNC


AL_API void AL_APIENTRY alGetSourcedSOFT(ALuint source, ALenum param, ALdouble *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!value)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
        GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), {value, 1u});
}
END_API_FUNC

AL_API void AL_APIENTRY alGetSource3dSOFT(ALuint source, ALenum param, ALdouble *value1, ALdouble *value2, ALdouble *value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!(value1 && value2 && value3))
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
    {
        double dvals[3];
        if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), dvals))
        {
            *value1 = dvals[0];
            *value2 = dvals[1];
            *value3 = dvals[2];
        }
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetSourcedvSOFT(ALuint source, ALenum param, ALdouble *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
        GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), {values, MaxValues});
}
END_API_FUNC


AL_API void AL_APIENTRY alGetSourcei(ALuint source, ALenum param, ALint *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!value)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
        GetSourceiv(Source, context.get(), static_cast<SourceProp>(param), {value, 1u});
}
END_API_FUNC

AL_API void AL_APIENTRY alGetSource3i(ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!(value1 && value2 && value3))
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
    {
        int ivals[3];
        if(GetSourceiv(Source, context.get(), static_cast<SourceProp>(param), ivals))
        {
            *value1 = ivals[0];
            *value2 = ivals[1];
            *value3 = ivals[2];
        }
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetSourceiv(ALuint source, ALenum param, ALint *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
        GetSourceiv(Source, context.get(), static_cast<SourceProp>(param), {values, MaxValues});
}
END_API_FUNC


AL_API void AL_APIENTRY alGetSourcei64SOFT(ALuint source, ALenum param, ALint64SOFT *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!value)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
        GetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), {value, 1u});
}
END_API_FUNC

AL_API void AL_APIENTRY alGetSource3i64SOFT(ALuint source, ALenum param, ALint64SOFT *value1, ALint64SOFT *value2, ALint64SOFT *value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!(value1 && value2 && value3))
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
    {
        int64_t i64vals[3];
        if(GetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), i64vals))
        {
            *value1 = i64vals[0];
            *value2 = i64vals[1];
            *value3 = i64vals[2];
        }
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetSourcei64vSOFT(ALuint source, ALenum param, ALint64SOFT *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if UNLIKELY(!Source)
        context->setError(AL_INVALID_NAME, "Invalid source ID %u", source);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else
        GetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), {values, MaxValues});
}
END_API_FUNC


AL_API void AL_APIENTRY alSourcePlay(ALuint source)
START_API_FUNC
{ alSourcePlayv(1, &source); }
END_API_FUNC

AL_API void AL_APIENTRY alSourcePlayv(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Playing %d sources", n);
    if UNLIKELY(n <= 0) return;

    al::vector<ALsource*> extra_sources;
    std::array<ALsource*,8> source_storage;
    al::span<ALsource*> srchandles;
    if LIKELY(static_cast<ALuint>(n) <= source_storage.size())
        srchandles = {source_storage.data(), static_cast<ALuint>(n)};
    else
    {
        extra_sources.resize(static_cast<ALuint>(n));
        srchandles = {extra_sources.data(), extra_sources.size()};
    }

    std::lock_guard<std::mutex> _{context->mSourceLock};
    for(auto &srchdl : srchandles)
    {
        srchdl = LookupSource(context.get(), *sources);
        if(!srchdl)
            SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", *sources);
        ++sources;
    }

    ALCdevice *device{context->mALDevice.get()};
    /* If the device is disconnected, and voices stop on disconnect, go right
     * to stopped.
     */
    if UNLIKELY(!device->Connected.load(std::memory_order_acquire))
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
    size_t free_voices{0};
    for(const Voice *voice : voicelist)
    {
        free_voices += (voice->mPlayState.load(std::memory_order_acquire) == Voice::Stopped
            && voice->mSourceID.load(std::memory_order_relaxed) == 0u
            && voice->mPendingChange.load(std::memory_order_relaxed) == false);
        if(free_voices == srchandles.size())
            break;
    }
    if UNLIKELY(srchandles.size() != free_voices)
    {
        const size_t inc_amount{srchandles.size() - free_voices};
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
    ALuint vidx{0};
    VoiceChange *tail{}, *cur{};
    for(ALsource *source : srchandles)
    {
        /* Check that there is a queue containing at least one valid, non zero
         * length buffer.
         */
        auto BufferList = source->mQueue.begin();
        for(;BufferList != source->mQueue.end();++BufferList)
        {
            if(BufferList->mSampleLen != 0 || BufferList->mCallback)
                break;
        }

        /* If there's nothing to play, go right to stopped. */
        if UNLIKELY(BufferList == source->mQueue.end())
        {
            /* NOTE: A source without any playable buffers should not have a
             * Voice since it shouldn't be in a playing or paused state. So
             * there's no need to look up its voice and clear the source.
             */
            source->Offset = 0.0;
            source->OffsetType = AL_NONE;
            source->state = AL_STOPPED;
            continue;
        }

        if(!cur)
            cur = tail = GetVoiceChanger(context.get());
        else
        {
            cur->mNext.store(GetVoiceChanger(context.get()), std::memory_order_relaxed);
            cur = cur->mNext.load(std::memory_order_relaxed);
        }

        Voice *voice{GetSourceVoice(source, context.get())};
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
#ifdef ALSOFT_EAX
            if(source->eax_is_initialized())
                source->eax_commit();
#endif // ALSOFT_EAX
            continue;

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
            assert(voice == nullptr);
            cur->mOldVoice = nullptr;
#ifdef ALSOFT_EAX
            if(source->eax_is_initialized())
                source->eax_commit();
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
        ASSUME(voice != nullptr);

        voice->mPosition.store(0u, std::memory_order_relaxed);
        voice->mPositionFrac.store(0, std::memory_order_relaxed);
        voice->mCurrentBuffer.store(&source->mQueue.front(), std::memory_order_relaxed);
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
                if(vpos->pos!=0 || vpos->frac!=0 || vpos->bufferitem!=&source->mQueue.front())
                    voice->mFlags.set(VoiceIsFading);
            }
        }
        InitVoice(voice, source, std::addressof(*BufferList), context.get(), device);

        source->VoiceIdx = vidx;
        source->state = AL_PLAYING;

        cur->mVoice = voice;
        cur->mSourceID = source->id;
        cur->mState = VChangeState::Play;
    }
    if LIKELY(tail)
        SendVoiceChanges(context.get(), tail);
}
END_API_FUNC


AL_API void AL_APIENTRY alSourcePause(ALuint source)
START_API_FUNC
{ alSourcePausev(1, &source); }
END_API_FUNC

AL_API void AL_APIENTRY alSourcePausev(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Pausing %d sources", n);
    if UNLIKELY(n <= 0) return;

    al::vector<ALsource*> extra_sources;
    std::array<ALsource*,8> source_storage;
    al::span<ALsource*> srchandles;
    if LIKELY(static_cast<ALuint>(n) <= source_storage.size())
        srchandles = {source_storage.data(), static_cast<ALuint>(n)};
    else
    {
        extra_sources.resize(static_cast<ALuint>(n));
        srchandles = {extra_sources.data(), extra_sources.size()};
    }

    std::lock_guard<std::mutex> _{context->mSourceLock};
    for(auto &srchdl : srchandles)
    {
        srchdl = LookupSource(context.get(), *sources);
        if(!srchdl)
            SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", *sources);
        ++sources;
    }

    /* Pausing has to be done in two steps. First, for each source that's
     * detected to be playing, chamge the voice (asynchronously) to
     * stopping/paused.
     */
    VoiceChange *tail{}, *cur{};
    for(ALsource *source : srchandles)
    {
        Voice *voice{GetSourceVoice(source, context.get())};
        if(GetSourceState(source, voice) == AL_PLAYING)
        {
            if(!cur)
                cur = tail = GetVoiceChanger(context.get());
            else
            {
                cur->mNext.store(GetVoiceChanger(context.get()), std::memory_order_relaxed);
                cur = cur->mNext.load(std::memory_order_relaxed);
            }
            cur->mVoice = voice;
            cur->mSourceID = source->id;
            cur->mState = VChangeState::Pause;
        }
    }
    if LIKELY(tail)
    {
        SendVoiceChanges(context.get(), tail);
        /* Second, now that the voice changes have been sent, because it's
         * possible that the voice stopped after it was detected playing and
         * before the voice got paused, recheck that the source is still
         * considered playing and set it to paused if so.
         */
        for(ALsource *source : srchandles)
        {
            Voice *voice{GetSourceVoice(source, context.get())};
            if(GetSourceState(source, voice) == AL_PLAYING)
                source->state = AL_PAUSED;
        }
    }
}
END_API_FUNC


AL_API void AL_APIENTRY alSourceStop(ALuint source)
START_API_FUNC
{ alSourceStopv(1, &source); }
END_API_FUNC

AL_API void AL_APIENTRY alSourceStopv(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Stopping %d sources", n);
    if UNLIKELY(n <= 0) return;

    al::vector<ALsource*> extra_sources;
    std::array<ALsource*,8> source_storage;
    al::span<ALsource*> srchandles;
    if LIKELY(static_cast<ALuint>(n) <= source_storage.size())
        srchandles = {source_storage.data(), static_cast<ALuint>(n)};
    else
    {
        extra_sources.resize(static_cast<ALuint>(n));
        srchandles = {extra_sources.data(), extra_sources.size()};
    }

    std::lock_guard<std::mutex> _{context->mSourceLock};
    for(auto &srchdl : srchandles)
    {
        srchdl = LookupSource(context.get(), *sources);
        if(!srchdl)
            SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", *sources);
        ++sources;
    }

    VoiceChange *tail{}, *cur{};
    for(ALsource *source : srchandles)
    {
        if(Voice *voice{GetSourceVoice(source, context.get())})
        {
            if(!cur)
                cur = tail = GetVoiceChanger(context.get());
            else
            {
                cur->mNext.store(GetVoiceChanger(context.get()), std::memory_order_relaxed);
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
        source->VoiceIdx = INVALID_VOICE_IDX;
    }
    if LIKELY(tail)
        SendVoiceChanges(context.get(), tail);
}
END_API_FUNC


AL_API void AL_APIENTRY alSourceRewind(ALuint source)
START_API_FUNC
{ alSourceRewindv(1, &source); }
END_API_FUNC

AL_API void AL_APIENTRY alSourceRewindv(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Rewinding %d sources", n);
    if UNLIKELY(n <= 0) return;

    al::vector<ALsource*> extra_sources;
    std::array<ALsource*,8> source_storage;
    al::span<ALsource*> srchandles;
    if LIKELY(static_cast<ALuint>(n) <= source_storage.size())
        srchandles = {source_storage.data(), static_cast<ALuint>(n)};
    else
    {
        extra_sources.resize(static_cast<ALuint>(n));
        srchandles = {extra_sources.data(), extra_sources.size()};
    }

    std::lock_guard<std::mutex> _{context->mSourceLock};
    for(auto &srchdl : srchandles)
    {
        srchdl = LookupSource(context.get(), *sources);
        if(!srchdl)
            SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", *sources);
        ++sources;
    }

    VoiceChange *tail{}, *cur{};
    for(ALsource *source : srchandles)
    {
        Voice *voice{GetSourceVoice(source, context.get())};
        if(source->state != AL_INITIAL)
        {
            if(!cur)
                cur = tail = GetVoiceChanger(context.get());
            else
            {
                cur->mNext.store(GetVoiceChanger(context.get()), std::memory_order_relaxed);
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
        source->VoiceIdx = INVALID_VOICE_IDX;
    }
    if LIKELY(tail)
        SendVoiceChanges(context.get(), tail);
}
END_API_FUNC


AL_API void AL_APIENTRY alSourceQueueBuffers(ALuint src, ALsizei nb, const ALuint *buffers)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(nb < 0)
        context->setError(AL_INVALID_VALUE, "Queueing %d buffers", nb);
    if UNLIKELY(nb <= 0) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *source{LookupSource(context.get(),src)};
    if UNLIKELY(!source)
        SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", src);

    /* Can't queue on a Static Source */
    if UNLIKELY(source->SourceType == AL_STATIC)
        SETERR_RETURN(context, AL_INVALID_OPERATION,, "Queueing onto static source %u", src);

    /* Check for a valid Buffer, for its frequency and format */
    ALCdevice *device{context->mALDevice.get()};
    ALbuffer *BufferFmt{nullptr};
    for(auto &item : source->mQueue)
    {
        BufferFmt = item.mBuffer;
        if(BufferFmt) break;
    }

    std::unique_lock<std::mutex> buflock{device->BufferLock};
    const size_t NewListStart{source->mQueue.size()};
    ALbufferQueueItem *BufferList{nullptr};
    for(ALsizei i{0};i < nb;i++)
    {
        bool fmt_mismatch{false};
        ALbuffer *buffer{nullptr};
        if(buffers[i] && (buffer=LookupBuffer(device, buffers[i])) == nullptr)
        {
            context->setError(AL_INVALID_NAME, "Queueing invalid buffer ID %u", buffers[i]);
            goto buffer_error;
        }
        if(buffer && buffer->mCallback)
        {
            context->setError(AL_INVALID_OPERATION, "Queueing callback buffer %u", buffers[i]);
            goto buffer_error;
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
        if(!buffer) continue;
        BufferList->mSampleLen = buffer->mSampleLen;
        BufferList->mLoopEnd = buffer->mSampleLen;
        BufferList->mSamples = buffer->mData.data();
        BufferList->mBuffer = buffer;
        IncrementRef(buffer->ref);

        if(buffer->MappedAccess != 0 && !(buffer->MappedAccess&AL_MAP_PERSISTENT_BIT_SOFT))
        {
            context->setError(AL_INVALID_OPERATION, "Queueing non-persistently mapped buffer %u",
                buffer->id);
            goto buffer_error;
        }

        if(BufferFmt == nullptr)
            BufferFmt = buffer;
        else
        {
            fmt_mismatch |= BufferFmt->mSampleRate != buffer->mSampleRate;
            fmt_mismatch |= BufferFmt->mChannels != buffer->mChannels;
            if(BufferFmt->isBFormat())
            {
                fmt_mismatch |= BufferFmt->mAmbiLayout != buffer->mAmbiLayout;
                fmt_mismatch |= BufferFmt->mAmbiScaling != buffer->mAmbiScaling;
            }
            fmt_mismatch |= BufferFmt->mAmbiOrder != buffer->mAmbiOrder;
            fmt_mismatch |= BufferFmt->OriginalType != buffer->OriginalType;
        }
        if UNLIKELY(fmt_mismatch)
        {
            context->setError(AL_INVALID_OPERATION, "Queueing buffer with mismatched format");

        buffer_error:
            /* A buffer failed (invalid ID or format), so unlock and release
             * each buffer we had.
             */
            auto iter = source->mQueue.begin() + ptrdiff_t(NewListStart);
            for(;iter != source->mQueue.end();++iter)
            {
                if(ALbuffer *buf{iter->mBuffer})
                    DecrementRef(buf->ref);
            }
            source->mQueue.resize(NewListStart);
            return;
        }
    }
    /* All buffers good. */
    buflock.unlock();

    /* Source is now streaming */
    source->SourceType = AL_STREAMING;

    if(NewListStart != 0)
    {
        auto iter = source->mQueue.begin() + ptrdiff_t(NewListStart);
        (iter-1)->mNext.store(std::addressof(*iter), std::memory_order_release);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alSourceUnqueueBuffers(ALuint src, ALsizei nb, ALuint *buffers)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(nb < 0)
        context->setError(AL_INVALID_VALUE, "Unqueueing %d buffers", nb);
    if UNLIKELY(nb <= 0) return;

    std::lock_guard<std::mutex> _{context->mSourceLock};
    ALsource *source{LookupSource(context.get(),src)};
    if UNLIKELY(!source)
        SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", src);

    if UNLIKELY(source->SourceType != AL_STREAMING)
        SETERR_RETURN(context, AL_INVALID_VALUE,, "Unqueueing from a non-streaming source %u",
            src);
    if UNLIKELY(source->Looping)
        SETERR_RETURN(context, AL_INVALID_VALUE,, "Unqueueing from looping source %u", src);

    /* Make sure enough buffers have been processed to unqueue. */
    uint processed{0u};
    if LIKELY(source->state != AL_INITIAL)
    {
        VoiceBufferItem *Current{nullptr};
        if(Voice *voice{GetSourceVoice(source, context.get())})
            Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);
        for(auto &item : source->mQueue)
        {
            if(&item == Current)
                break;
            ++processed;
        }
    }
    if UNLIKELY(processed < static_cast<ALuint>(nb))
        SETERR_RETURN(context, AL_INVALID_VALUE,, "Unqueueing %d buffer%s (only %u processed)",
            nb, (nb==1)?"":"s", processed);

    do {
        auto &head = source->mQueue.front();
        if(ALbuffer *buffer{head.mBuffer})
        {
            *(buffers++) = buffer->id;
            DecrementRef(buffer->ref);
        }
        else
            *(buffers++) = 0;
        source->mQueue.pop_front();
    } while(--nb);
}
END_API_FUNC


AL_API void AL_APIENTRY alSourceQueueBufferLayersSOFT(ALuint, ALsizei, const ALuint*)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    context->setError(AL_INVALID_OPERATION, "alSourceQueueBufferLayersSOFT not supported");
}
END_API_FUNC


ALsource::ALsource()
{
    Direct.Gain = 1.0f;
    Direct.GainHF = 1.0f;
    Direct.HFReference = LOWPASSFREQREF;
    Direct.GainLF = 1.0f;
    Direct.LFReference = HIGHPASSFREQREF;
    for(auto &send : Send)
    {
        send.Slot = nullptr;
        send.Gain = 1.0f;
        send.GainHF = 1.0f;
        send.HFReference = LOWPASSFREQREF;
        send.GainLF = 1.0f;
        send.LFReference = HIGHPASSFREQREF;
    }
}

ALsource::~ALsource()
{
    for(auto &item : mQueue)
    {
        if(ALbuffer *buffer{item.mBuffer})
            DecrementRef(buffer->ref);
    }

    auto clear_send = [](ALsource::SendData &send) -> void
    { if(send.Slot) DecrementRef(send.Slot->ref); };
    std::for_each(Send.begin(), Send.end(), clear_send);
}

void UpdateAllSourceProps(ALCcontext *context)
{
    std::lock_guard<std::mutex> _{context->mSourceLock};
#ifdef ALSOFT_EAX
    if(context->has_eax())
    {
        /* If EAX is enabled, we need to go through and commit all sources' EAX
         * changes, along with updating its voice, if any.
         */
        for(auto &sublist : context->mSourceList)
        {
            uint64_t usemask{~sublist.FreeMask};
            while(usemask)
            {
                const int idx{al::countr_zero(usemask)};
                usemask &= ~(1_u64 << idx);

                ALsource *source{sublist.Sources + idx};
                source->eax_commit_and_update();
            }
        }
    }
    else
#endif
    {
        auto voicelist = context->getVoicesSpan();
        ALuint vidx{0u};
        for(Voice *voice : voicelist)
        {
            ALuint sid{voice->mSourceID.load(std::memory_order_acquire)};
            ALsource *source = sid ? LookupSource(context, sid) : nullptr;
            if(source && source->VoiceIdx == vidx)
            {
                if(std::exchange(source->mPropsDirty, false))
                    UpdateSourceProps(source, voice, context);
            }
            ++vidx;
        }
    }
}

SourceSubList::~SourceSubList()
{
    uint64_t usemask{~FreeMask};
    while(usemask)
    {
        const int idx{al::countr_zero(usemask)};
        usemask &= ~(1_u64 << idx);
        al::destroy_at(Sources+idx);
    }
    FreeMask = ~usemask;
    al_free(Sources);
    Sources = nullptr;
}


#ifdef ALSOFT_EAX
void EaxUpdateSourceVoice(ALsource *source, ALCcontext *context)
{
    if(Voice *voice{GetSourceVoice(source, context)})
    {
        if(std::exchange(source->mPropsDirty, false))
            UpdateSourceProps(source, voice, context);
    }
}

constexpr const ALsource::EaxFxSlotIds ALsource::eax4_fx_slot_ids;
constexpr const ALsource::EaxFxSlotIds ALsource::eax5_fx_slot_ids;

void ALsource::eax_initialize(ALCcontext *context) noexcept
{
    assert(context != nullptr);
    eax_al_context_ = context;
    eax_primary_fx_slot_id_ = eax_al_context_->eax_get_primary_fx_slot_index();
    eax_version_ = eax_al_context_->eax_get_version();
    eax_set_defaults();
    eax_commit(EaxCommitType::forced);
}

void ALsource::eax_dispatch(const EaxCall& call)
{
    const auto eax_version = call.get_version();
    eax_is_version_changed_ |= (eax_version_ == eax_version);
    eax_version_ = eax_version;
    call.is_get() ? eax_get(call) : eax_set(call);
}

void ALsource::eax_commit()
{
    eax_commit(EaxCommitType::normal);
}

void ALsource::eax_commit_and_update()
{
    eax_commit();
    EaxUpdateSourceVoice(this, eax_al_context_);
}

ALsource* ALsource::eax_lookup_source(ALCcontext& al_context, ALuint source_id) noexcept
{
    return LookupSource(&al_context, source_id);
}

[[noreturn]] void ALsource::eax_fail(const char* message)
{
    throw Exception{message};
}

[[noreturn]] void ALsource::eax_fail_unknown_property_id()
{
    eax_fail("Unknown property id.");
}

[[noreturn]] void ALsource::eax_fail_unknown_version()
{
    eax_fail("Unknown version.");
}

[[noreturn]] void ALsource::eax_fail_unknown_active_fx_slot_id()
{
    eax_fail("Unknown active FX slot ID.");
}

[[noreturn]] void ALsource::eax_fail_unknown_receiving_fx_slot_id()
{
    eax_fail("Unknown receiving FX slot ID.");
}

void ALsource::eax_set_sends_defaults(EaxSends& sends, const EaxFxSlotIds& ids) noexcept
{
    for (auto i = size_t{}; i < EAX_MAX_FXSLOTS; ++i) {
        auto& send = sends[i];
        send.guidReceivingFXSlotID = *(ids[i]);
        send.lSend = EAXSOURCE_DEFAULTSEND;
        send.lSendHF = EAXSOURCE_DEFAULTSENDHF;
        send.lOcclusion = EAXSOURCE_DEFAULTOCCLUSION;
        send.flOcclusionLFRatio = EAXSOURCE_DEFAULTOCCLUSIONLFRATIO;
        send.flOcclusionRoomRatio = EAXSOURCE_DEFAULTOCCLUSIONROOMRATIO;
        send.flOcclusionDirectRatio = EAXSOURCE_DEFAULTOCCLUSIONDIRECTRATIO;
        send.lExclusion = EAXSOURCE_DEFAULTEXCLUSION;
        send.flExclusionLFRatio = EAXSOURCE_DEFAULTEXCLUSIONLFRATIO;
    }
}

void ALsource::eax1_set_defaults(Eax1Props& props) noexcept
{
    props.fMix = EAX_REVERBMIX_USEDISTANCE;
}

void ALsource::eax1_set_defaults() noexcept
{
    eax1_set_defaults(eax1_.i);
    eax1_.d = eax1_.i;
}

void ALsource::eax2_set_defaults(Eax2Props& props) noexcept
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
    eax2_set_defaults(eax2_.i);
    eax2_.d = eax2_.i;
}

void ALsource::eax3_set_defaults(Eax3Props& props) noexcept
{
    props.lDirect = EAXSOURCE_DEFAULTDIRECT;
    props.lDirectHF = EAXSOURCE_DEFAULTDIRECTHF;
    props.lRoom = EAXSOURCE_DEFAULTROOM;
    props.lRoomHF = EAXSOURCE_DEFAULTROOMHF;
    props.lObstruction = EAXSOURCE_DEFAULTOBSTRUCTION;
    props.flObstructionLFRatio = EAXSOURCE_DEFAULTOBSTRUCTIONLFRATIO;
    props.lOcclusion = EAXSOURCE_DEFAULTOCCLUSION;
    props.flOcclusionLFRatio = EAXSOURCE_DEFAULTOCCLUSIONLFRATIO;
    props.flOcclusionRoomRatio = EAXSOURCE_DEFAULTOCCLUSIONROOMRATIO;
    props.flOcclusionDirectRatio = EAXSOURCE_DEFAULTOCCLUSIONDIRECTRATIO;
    props.lExclusion = EAXSOURCE_DEFAULTEXCLUSION;
    props.flExclusionLFRatio = EAXSOURCE_DEFAULTEXCLUSIONLFRATIO;
    props.lOutsideVolumeHF = EAXSOURCE_DEFAULTOUTSIDEVOLUMEHF;
    props.flDopplerFactor = EAXSOURCE_DEFAULTDOPPLERFACTOR;
    props.flRolloffFactor = EAXSOURCE_DEFAULTROLLOFFFACTOR;
    props.flRoomRolloffFactor = EAXSOURCE_DEFAULTROOMROLLOFFFACTOR;
    props.flAirAbsorptionFactor = EAXSOURCE_DEFAULTAIRABSORPTIONFACTOR;
    props.ulFlags = EAXSOURCE_DEFAULTFLAGS;
}

void ALsource::eax3_set_defaults() noexcept
{
    eax3_set_defaults(eax3_.i);
    eax3_.d = eax3_.i;
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
    eax3_set_defaults(eax4_.i.source);
    eax4_set_sends_defaults(eax4_.i.sends);
    eax4_set_active_fx_slots_defaults(eax4_.i.active_fx_slots);
    eax4_.d = eax4_.i;
}

void ALsource::eax5_set_source_defaults(EAX50SOURCEPROPERTIES& props) noexcept
{
    eax3_set_defaults(static_cast<Eax3Props&>(props));
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
    for (auto i = size_t{}; i < eax_max_speakers; ++i) {
        auto& speaker_level = speaker_levels[i];
        speaker_level.lSpeakerID = static_cast<long>(EAXSPEAKER_FRONT_LEFT + i);
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
    eax5_set_defaults(eax5_.i);
    eax5_.d = eax5_.i;
}

void ALsource::eax_set_defaults() noexcept
{
    eax1_set_defaults();
    eax2_set_defaults();
    eax3_set_defaults();
    eax4_set_defaults();
    eax5_set_defaults();
}

void ALsource::eax1_translate(const Eax1Props& src, Eax5Props& dst) noexcept
{
    eax5_set_defaults(dst);

    if (src.fMix == EAX_REVERBMIX_USEDISTANCE)
        dst.source.ulFlags |= EAXSOURCEFLAGS_ROOMAUTO;
    else
        dst.source.ulFlags &= ~EAXSOURCEFLAGS_ROOMAUTO;

    dst.sends[0].lSendHF = clamp(
        static_cast<long>(gain_to_level_mb(src.fMix)),
        EAXSOURCE_MINSENDHF,
        EAXSOURCE_MAXSENDHF);
}

void ALsource::eax2_translate(const Eax2Props& src, Eax5Props& dst) noexcept
{
    // Source.
    //
    dst.source.lDirect = src.lDirect;
    dst.source.lDirectHF = src.lDirectHF;
    dst.source.lRoom = src.lRoom;
    dst.source.lRoomHF = src.lRoomHF;
    dst.source.lObstruction = src.lObstruction;
    dst.source.flObstructionLFRatio = src.flObstructionLFRatio;
    dst.source.lOcclusion = src.lOcclusion;
    dst.source.flOcclusionLFRatio = src.flOcclusionLFRatio;
    dst.source.flOcclusionRoomRatio = src.flOcclusionRoomRatio;
    dst.source.flOcclusionDirectRatio = EAXSOURCE_DEFAULTOCCLUSIONDIRECTRATIO;
    dst.source.lExclusion = EAXSOURCE_DEFAULTEXCLUSION;
    dst.source.flExclusionLFRatio = EAXSOURCE_DEFAULTEXCLUSIONLFRATIO;
    dst.source.lOutsideVolumeHF = src.lOutsideVolumeHF;
    dst.source.flDopplerFactor = EAXSOURCE_DEFAULTDOPPLERFACTOR;
    dst.source.flRolloffFactor = EAXSOURCE_DEFAULTROLLOFFFACTOR;
    dst.source.flRoomRolloffFactor = src.flRoomRolloffFactor;
    dst.source.flAirAbsorptionFactor = src.flAirAbsorptionFactor;
    dst.source.ulFlags = src.dwFlags;
    dst.source.flMacroFXFactor = EAXSOURCE_DEFAULTMACROFXFACTOR;

    // Set everyting else to defaults.
    //
    eax5_set_sends_defaults(dst.sends);
    eax5_set_active_fx_slots_defaults(dst.active_fx_slots);
    eax5_set_speaker_levels_defaults(dst.speaker_levels);
}

void ALsource::eax3_translate(const Eax3Props& src, Eax5Props& dst) noexcept
{
    // Source.
    //
    static_cast<Eax3Props&>(dst.source) = src;
    dst.source.flMacroFXFactor = EAXSOURCE_DEFAULTMACROFXFACTOR;

    // Set everyting else to defaults.
    //
    eax5_set_sends_defaults(dst.sends);
    eax5_set_active_fx_slots_defaults(dst.active_fx_slots);
    eax5_set_speaker_levels_defaults(dst.speaker_levels);
}

void ALsource::eax4_translate(const Eax4Props& src, Eax5Props& dst) noexcept
{
    // Source.
    //
    static_cast<Eax3Props&>(dst.source) = src.source;
    dst.source.flMacroFXFactor = EAXSOURCE_DEFAULTMACROFXFACTOR;

    // Sends.
    //
    dst.sends = src.sends;

    for (auto i = size_t{}; i < EAX_MAX_FXSLOTS; ++i)
        dst.sends[i].guidReceivingFXSlotID = *(eax5_fx_slot_ids[i]);

    // Active FX slots.
    //
    for (auto i = 0; i < EAX50_MAX_ACTIVE_FXSLOTS; ++i) {
        auto& dst_id = dst.active_fx_slots.guidActiveFXSlots[i];

        if (i < EAX40_MAX_ACTIVE_FXSLOTS) {
            const auto& src_id = src.active_fx_slots.guidActiveFXSlots[i];

            if (src_id == EAX_NULL_GUID)
                dst_id = EAX_NULL_GUID;
            else if (src_id == EAX_PrimaryFXSlotID)
                dst_id = EAX_PrimaryFXSlotID;
            else if (src_id == EAXPROPERTYID_EAX40_FXSlot0)
                dst_id = EAXPROPERTYID_EAX50_FXSlot0;
            else if (src_id == EAXPROPERTYID_EAX40_FXSlot1)
                dst_id = EAXPROPERTYID_EAX50_FXSlot1;
            else if (src_id == EAXPROPERTYID_EAX40_FXSlot2)
                dst_id = EAXPROPERTYID_EAX50_FXSlot2;
            else if (src_id == EAXPROPERTYID_EAX40_FXSlot3)
                dst_id = EAXPROPERTYID_EAX50_FXSlot3;
            else
                assert(false && "Unknown active FX slot ID.");
        } else
            dst_id = EAX_NULL_GUID;
    }

    // Speaker levels.
    //
    eax5_set_speaker_levels_defaults(dst.speaker_levels);
}

float ALsource::eax_calculate_dst_occlusion_mb(
    long src_occlusion_mb,
    float path_ratio,
    float lf_ratio) noexcept
{
    const auto ratio_1 = path_ratio + lf_ratio - 1.0F;
    const auto ratio_2 = path_ratio * lf_ratio;
    const auto ratio = (ratio_2 > ratio_1) ? ratio_2 : ratio_1;
    const auto dst_occlustion_mb = static_cast<float>(src_occlusion_mb) * ratio;
    return dst_occlustion_mb;
}

EaxAlLowPassParam ALsource::eax_create_direct_filter_param() const noexcept
{
    auto gain_mb =
        static_cast<float>(eax_.source.lDirect) +
        (static_cast<float>(eax_.source.lObstruction) * eax_.source.flObstructionLFRatio) +
        eax_calculate_dst_occlusion_mb(
            eax_.source.lOcclusion,
            eax_.source.flOcclusionDirectRatio,
            eax_.source.flOcclusionLFRatio);

    auto gain_hf_mb =
        static_cast<float>(eax_.source.lDirectHF) +
        static_cast<float>(eax_.source.lObstruction) +
        (static_cast<float>(eax_.source.lOcclusion) * eax_.source.flOcclusionDirectRatio);

    for (auto i = std::size_t{}; i < EAX_MAX_FXSLOTS; ++i)
    {
        if (!eax_active_fx_slots_[i])
        {
            continue;
        }

        const auto& send = eax_.sends[i];

        gain_mb += eax_calculate_dst_occlusion_mb(
            send.lOcclusion,
            send.flOcclusionDirectRatio,
            send.flOcclusionLFRatio);

        gain_hf_mb += static_cast<float>(send.lOcclusion) * send.flOcclusionDirectRatio;
    }

    const auto al_low_pass_param = EaxAlLowPassParam{
        level_mb_to_gain(gain_mb),
        minf(level_mb_to_gain(gain_hf_mb), 1.0f)};

    return al_low_pass_param;
}

EaxAlLowPassParam ALsource::eax_create_room_filter_param(
    const ALeffectslot& fx_slot,
    const EAXSOURCEALLSENDPROPERTIES& send) const noexcept
{
    const auto& fx_slot_eax = fx_slot.eax_get_eax_fx_slot();

    const auto gain_mb =
        static_cast<float>(eax_.source.lRoom + send.lSend) +
        eax_calculate_dst_occlusion_mb(
            eax_.source.lOcclusion,
            eax_.source.flOcclusionRoomRatio,
            eax_.source.flOcclusionLFRatio) +
        eax_calculate_dst_occlusion_mb(
            send.lOcclusion,
            send.flOcclusionRoomRatio,
            send.flOcclusionLFRatio) +
        (static_cast<float>(eax_.source.lExclusion) * eax_.source.flExclusionLFRatio) +
        (static_cast<float>(send.lExclusion) * send.flExclusionLFRatio);

    const auto gain_hf_mb =
        static_cast<float>(eax_.source.lRoomHF + send.lSendHF) +
        (static_cast<float>(fx_slot_eax.lOcclusion + eax_.source.lOcclusion) * eax_.source.flOcclusionRoomRatio) +
        (static_cast<float>(send.lOcclusion) * send.flOcclusionRoomRatio) +
        static_cast<float>(eax_.source.lExclusion + send.lExclusion);

    const auto al_low_pass_param = EaxAlLowPassParam{
        level_mb_to_gain(gain_mb),
        minf(level_mb_to_gain(gain_hf_mb), 1.0f)};

    return al_low_pass_param;
}

void ALsource::eax_update_direct_filter()
{
    const auto& direct_param = eax_create_direct_filter_param();
    Direct.Gain = direct_param.gain;
    Direct.GainHF = direct_param.gain_hf;
    Direct.HFReference = LOWPASSFREQREF;
    Direct.GainLF = 1.0f;
    Direct.LFReference = HIGHPASSFREQREF;
    mPropsDirty = true;
}

void ALsource::eax_update_room_filters()
{
    for (auto i = size_t{}; i < EAX_MAX_FXSLOTS; ++i) {
        if (!eax_active_fx_slots_[i])
            continue;

        auto& fx_slot = eax_al_context_->eax_get_fx_slot(i);
        const auto& send = eax_.sends[i];
        const auto& room_param = eax_create_room_filter_param(fx_slot, send);
        eax_set_al_source_send(&fx_slot, i, room_param);
    }
}

void ALsource::eax_set_efx_outer_gain_hf()
{
    OuterGainHF = clamp(
        level_mb_to_gain(static_cast<float>(eax_.source.lOutsideVolumeHF)),
        AL_MIN_CONE_OUTER_GAINHF,
        AL_MAX_CONE_OUTER_GAINHF);
}

void ALsource::eax_set_efx_doppler_factor()
{
    DopplerFactor = eax_.source.flDopplerFactor;
}

void ALsource::eax_set_efx_rolloff_factor()
{
    RolloffFactor2 = eax_.source.flRolloffFactor;
}

void ALsource::eax_set_efx_room_rolloff_factor()
{
    RoomRolloffFactor = eax_.source.flRoomRolloffFactor;
}

void ALsource::eax_set_efx_air_absorption_factor()
{
    AirAbsorptionFactor = eax_.source.flAirAbsorptionFactor;
}

void ALsource::eax_set_efx_dry_gain_hf_auto()
{
    DryGainHFAuto = ((eax_.source.ulFlags & EAXSOURCEFLAGS_DIRECTHFAUTO) != 0);
}

void ALsource::eax_set_efx_wet_gain_auto()
{
    WetGainAuto = ((eax_.source.ulFlags & EAXSOURCEFLAGS_ROOMAUTO) != 0);
}

void ALsource::eax_set_efx_wet_gain_hf_auto()
{
    WetGainHFAuto = ((eax_.source.ulFlags & EAXSOURCEFLAGS_ROOMHFAUTO) != 0);
}

void ALsource::eax1_set(const EaxCall& call, Eax1Props& props)
{
    switch (call.get_property_id()) {
        case DSPROPERTY_EAXBUFFER_ALL:
            eax_defer<Eax1SourceAllValidator>(call, props);
            break;

        case DSPROPERTY_EAXBUFFER_REVERBMIX:
            eax_defer<Eax1SourceReverbMixValidator>(call, props.fMix);
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax2_set(const EaxCall& call, Eax2Props& props)
{
    switch (call.get_property_id()) {
        case DSPROPERTY_EAX20BUFFER_NONE:
            break;

        case DSPROPERTY_EAX20BUFFER_ALLPARAMETERS:
            eax_defer<Eax2SourceAllValidator>(call, props);
            break;

        case DSPROPERTY_EAX20BUFFER_DIRECT:
            eax_defer<Eax2SourceDirectValidator>(call, props.lDirect);
            break;

        case DSPROPERTY_EAX20BUFFER_DIRECTHF:
            eax_defer<Eax2SourceDirectHfValidator>(call, props.lDirectHF);
            break;

        case DSPROPERTY_EAX20BUFFER_ROOM:
            eax_defer<Eax2SourceRoomValidator>(call, props.lRoom);
            break;

        case DSPROPERTY_EAX20BUFFER_ROOMHF:
            eax_defer<Eax2SourceRoomHfValidator>(call, props.lRoomHF);
            break;

        case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
            eax_defer<Eax2SourceRoomRolloffFactorValidator>(call, props.flRoomRolloffFactor);
            break;

        case DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
            eax_defer<Eax2SourceObstructionValidator>(call, props.lObstruction);
            break;

        case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
            eax_defer<Eax2SourceObstructionLfRatioValidator>(call, props.flObstructionLFRatio);
            break;

        case DSPROPERTY_EAX20BUFFER_OCCLUSION:
            eax_defer<Eax2SourceOcclusionValidator>(call, props.lOcclusion);
            break;

        case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
            eax_defer<Eax2SourceOcclusionLfRatioValidator>(call, props.flOcclusionLFRatio);
            break;

        case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
            eax_defer<Eax2SourceOcclusionRoomRatioValidator>(call, props.flOcclusionRoomRatio);
            break;

        case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
            eax_defer<Eax2SourceOutsideVolumeHfValidator>(call, props.lOutsideVolumeHF);
            break;

        case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
            eax_defer<Eax2SourceAirAbsorptionFactorValidator>(call, props.flAirAbsorptionFactor);
            break;

        case DSPROPERTY_EAX20BUFFER_FLAGS:
            eax_defer<Eax2SourceFlagsValidator>(call, props.dwFlags);
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax3_set(const EaxCall& call, Eax3Props& props)
{
    switch (call.get_property_id()) {
        case EAXSOURCE_NONE:
            break;

        case EAXSOURCE_ALLPARAMETERS:
            eax_defer<Eax3SourceAllValidator>(call, props);
            break;

        case EAXSOURCE_OBSTRUCTIONPARAMETERS:
            eax_defer_sub<Eax4ObstructionValidator, EAXOBSTRUCTIONPROPERTIES>(call, props.lObstruction);
            break;

        case EAXSOURCE_OCCLUSIONPARAMETERS:
            eax_defer_sub<Eax4OcclusionValidator, EAXOCCLUSIONPROPERTIES>(call, props.lOcclusion);
            break;

        case EAXSOURCE_EXCLUSIONPARAMETERS:
            eax_defer_sub<Eax4ExclusionValidator, EAXEXCLUSIONPROPERTIES>(call, props.lExclusion);
            break;

        case EAXSOURCE_DIRECT:
            eax_defer<Eax2SourceDirectValidator>(call, props.lDirect);
            break;

        case EAXSOURCE_DIRECTHF:
            eax_defer<Eax2SourceDirectHfValidator>(call, props.lDirectHF);
            break;

        case EAXSOURCE_ROOM:
            eax_defer<Eax2SourceRoomValidator>(call, props.lRoom);
            break;

        case EAXSOURCE_ROOMHF:
            eax_defer<Eax2SourceRoomHfValidator>(call, props.lRoomHF);
            break;

        case EAXSOURCE_OBSTRUCTION:
            eax_defer<Eax2SourceObstructionValidator>(call, props.lObstruction);
            break;

        case EAXSOURCE_OBSTRUCTIONLFRATIO:
            eax_defer<Eax2SourceObstructionLfRatioValidator>(call, props.flObstructionLFRatio);
            break;

        case EAXSOURCE_OCCLUSION:
            eax_defer<Eax2SourceOcclusionValidator>(call, props.lOcclusion);
            break;

        case EAXSOURCE_OCCLUSIONLFRATIO:
            eax_defer<Eax2SourceOcclusionLfRatioValidator>(call, props.flOcclusionLFRatio);
            break;

        case EAXSOURCE_OCCLUSIONROOMRATIO:
            eax_defer<Eax2SourceOcclusionRoomRatioValidator>(call, props.flOcclusionRoomRatio);
            break;

        case EAXSOURCE_OCCLUSIONDIRECTRATIO:
            eax_defer<Eax3SourceOcclusionDirectRatioValidator>(call, props.flOcclusionDirectRatio);
            break;

        case EAXSOURCE_EXCLUSION:
            eax_defer<Eax3SourceExclusionValidator>(call, props.lExclusion);
            break;

        case EAXSOURCE_EXCLUSIONLFRATIO:
            eax_defer<Eax3SourceExclusionLfRatioValidator>(call, props.flExclusionLFRatio);
            break;

        case EAXSOURCE_OUTSIDEVOLUMEHF:
            eax_defer<Eax2SourceOutsideVolumeHfValidator>(call, props.lOutsideVolumeHF);
            break;

        case EAXSOURCE_DOPPLERFACTOR:
            eax_defer<Eax3SourceDopplerFactorValidator>(call, props.flDopplerFactor);
            break;

        case EAXSOURCE_ROLLOFFFACTOR:
            eax_defer<Eax3SourceRolloffFactorValidator>(call, props.flRolloffFactor);
            break;

        case EAXSOURCE_ROOMROLLOFFFACTOR:
            eax_defer<Eax2SourceRoomRolloffFactorValidator>(call, props.flRoomRolloffFactor);
            break;

        case EAXSOURCE_AIRABSORPTIONFACTOR:
            eax_defer<Eax2SourceAirAbsorptionFactorValidator>(call, props.flAirAbsorptionFactor);
            break;

        case EAXSOURCE_FLAGS:
            eax_defer<Eax2SourceFlagsValidator>(call, props.ulFlags);
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax4_set(const EaxCall& call, Eax4Props& props)
{
    switch (call.get_property_id()) {
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
            eax4_defer_sends<Eax4SendValidator, EAXSOURCESENDPROPERTIES>(call, props.sends);
            break;

        case EAXSOURCE_ALLSENDPARAMETERS:
            eax4_defer_sends<Eax4AllSendValidator, EAXSOURCEALLSENDPROPERTIES>(call, props.sends);
            break;

        case EAXSOURCE_OCCLUSIONSENDPARAMETERS:
            eax4_defer_sends<Eax4OcclusionSendValidator, EAXSOURCEOCCLUSIONSENDPROPERTIES>(call, props.sends);
            break;

        case EAXSOURCE_EXCLUSIONSENDPARAMETERS:
            eax4_defer_sends<Eax4ExclusionSendValidator, EAXSOURCEEXCLUSIONSENDPROPERTIES>(call, props.sends);
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
    const auto& src_props = call.get_value<Exception, const EAXSOURCE2DPROPERTIES>();
    Eax5SourceAll2dValidator{}(src_props);
    props.lDirect = src_props.lDirect;
    props.lDirectHF = src_props.lDirectHF;
    props.lRoom = src_props.lRoom;
    props.lRoomHF = src_props.lRoomHF;
    props.ulFlags = src_props.ulFlags;
}

void ALsource::eax5_defer_speaker_levels(const EaxCall& call, EaxSpeakerLevels& props)
{
    const auto values = call.get_values<const EAXSPEAKERLEVELPROPERTIES>(eax_max_speakers);
    std::for_each(values.cbegin(), values.cend(), Eax5SpeakerAllValidator{});

    for (const auto& value : values) {
        const auto index = static_cast<size_t>(value.lSpeakerID - EAXSPEAKER_FRONT_LEFT);
        props[index].lLevel = value.lLevel;
    }
}

void ALsource::eax5_set(const EaxCall& call, Eax5Props& props)
{
    switch (call.get_property_id()) {
        case EAXSOURCE_NONE:
            break;

        case EAXSOURCE_ALLPARAMETERS:
            eax_defer<Eax5SourceAllValidator>(call, props.source);
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
        case EAXSOURCE_FLAGS:
            eax3_set(call, props.source);
            break;

        case EAXSOURCE_SENDPARAMETERS:
            eax5_defer_sends<Eax5SendValidator, EAXSOURCESENDPROPERTIES>(call, props.sends);
            break;

        case EAXSOURCE_ALLSENDPARAMETERS:
            eax5_defer_sends<Eax5AllSendValidator, EAXSOURCEALLSENDPROPERTIES>(call, props.sends);
            break;

        case EAXSOURCE_OCCLUSIONSENDPARAMETERS:
            eax5_defer_sends<Eax5OcclusionSendValidator, EAXSOURCEOCCLUSIONSENDPROPERTIES>(call, props.sends);
            break;

        case EAXSOURCE_EXCLUSIONSENDPARAMETERS:
            eax5_defer_sends<Eax5ExclusionSendValidator, EAXSOURCEEXCLUSIONSENDPROPERTIES>(call, props.sends);
            break;

        case EAXSOURCE_ACTIVEFXSLOTID:
            eax5_defer_active_fx_slot_id(call, props.active_fx_slots.guidActiveFXSlots);
            break;

        case EAXSOURCE_MACROFXFACTOR:
            eax_defer<Eax5SourceMacroFXFactorValidator>(call, props.source.flMacroFXFactor);
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
    switch (call.get_version()) {
        case 1: eax1_set(call, eax1_.d); break;
        case 2: eax2_set(call, eax2_.d); break;
        case 3: eax3_set(call, eax3_.d); break;
        case 4: eax4_set(call, eax4_.d); break;
        case 5: eax5_set(call, eax5_.d); break;
        default: eax_fail_unknown_property_id();
    }
}

void ALsource::eax1_get(const EaxCall& call, const Eax1Props& props)
{
    switch (call.get_property_id()) {
        case DSPROPERTY_EAXBUFFER_ALL:
        case DSPROPERTY_EAXBUFFER_REVERBMIX:
            call.set_value<Exception>(props.fMix);
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax2_get(const EaxCall& call, const Eax2Props& props)
{
    switch (call.get_property_id()) {
        case DSPROPERTY_EAX20BUFFER_NONE:
            break;

        case DSPROPERTY_EAX20BUFFER_ALLPARAMETERS:
            call.set_value<Exception>(props);
            break;

        case DSPROPERTY_EAX20BUFFER_DIRECT:
            call.set_value<Exception>(props.lDirect);
            break;

        case DSPROPERTY_EAX20BUFFER_DIRECTHF:
            call.set_value<Exception>(props.lDirectHF);
            break;

        case DSPROPERTY_EAX20BUFFER_ROOM:
            call.set_value<Exception>(props.lRoom);
            break;

        case DSPROPERTY_EAX20BUFFER_ROOMHF:
            call.set_value<Exception>(props.lRoomHF);
            break;

        case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
            call.set_value<Exception>(props.flRoomRolloffFactor);
            break;

        case DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
            call.set_value<Exception>(props.lObstruction);
            break;

        case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
            call.set_value<Exception>(props.flObstructionLFRatio);
            break;

        case DSPROPERTY_EAX20BUFFER_OCCLUSION:
            call.set_value<Exception>(props.lOcclusion);
            break;

        case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
            call.set_value<Exception>(props.flOcclusionLFRatio);
            break;

        case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
            call.set_value<Exception>(props.flOcclusionRoomRatio);
            break;

        case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
            call.set_value<Exception>(props.lOutsideVolumeHF);
            break;

        case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
            call.set_value<Exception>(props.flAirAbsorptionFactor);
            break;

        case DSPROPERTY_EAX20BUFFER_FLAGS:
            call.set_value<Exception>(props.dwFlags);
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax3_get_obstruction(const EaxCall& call, const Eax3Props& props)
{
    const auto& subprops = reinterpret_cast<const EAXOBSTRUCTIONPROPERTIES&>(props.lObstruction);
    call.set_value<Exception>(subprops);
}

void ALsource::eax3_get_occlusion(const EaxCall& call, const Eax3Props& props)
{
    const auto& subprops = reinterpret_cast<const EAXOCCLUSIONPROPERTIES&>(props.lOcclusion);
    call.set_value<Exception>(subprops);
}

void ALsource::eax3_get_exclusion(const EaxCall& call, const Eax3Props& props)
{
    const auto& subprops = reinterpret_cast<const EAXEXCLUSIONPROPERTIES&>(props.lExclusion);
    call.set_value<Exception>(subprops);
}

void ALsource::eax3_get(const EaxCall& call, const Eax3Props& props)
{
    switch (call.get_property_id()) {
        case EAXSOURCE_NONE:
            break;

        case EAXSOURCE_ALLPARAMETERS:
            call.set_value<Exception>(props);
            break;

        case EAXSOURCE_OBSTRUCTIONPARAMETERS:
            eax3_get_obstruction(call, props);
            break;

        case EAXSOURCE_OCCLUSIONPARAMETERS:
            eax3_get_occlusion(call, props);
            break;

        case EAXSOURCE_EXCLUSIONPARAMETERS:
            eax3_get_exclusion(call, props);
            break;

        case EAXSOURCE_DIRECT:
            call.set_value<Exception>(props.lDirect);
            break;

        case EAXSOURCE_DIRECTHF:
            call.set_value<Exception>(props.lDirectHF);
            break;

        case EAXSOURCE_ROOM:
            call.set_value<Exception>(props.lRoom);
            break;

        case EAXSOURCE_ROOMHF:
            call.set_value<Exception>(props.lRoomHF);
            break;

        case EAXSOURCE_OBSTRUCTION:
            call.set_value<Exception>(props.lObstruction);
            break;

        case EAXSOURCE_OBSTRUCTIONLFRATIO:
            call.set_value<Exception>(props.flObstructionLFRatio);
            break;

        case EAXSOURCE_OCCLUSION:
            call.set_value<Exception>(props.lOcclusion);
            break;

        case EAXSOURCE_OCCLUSIONLFRATIO:
            call.set_value<Exception>(props.flOcclusionLFRatio);
            break;

        case EAXSOURCE_OCCLUSIONROOMRATIO:
            call.set_value<Exception>(props.flOcclusionRoomRatio);
            break;

        case EAXSOURCE_OCCLUSIONDIRECTRATIO:
            call.set_value<Exception>(props.flOcclusionDirectRatio);
            break;

        case EAXSOURCE_EXCLUSION:
            call.set_value<Exception>(props.lExclusion);
            break;

        case EAXSOURCE_EXCLUSIONLFRATIO:
            call.set_value<Exception>(props.flExclusionLFRatio);
            break;

        case EAXSOURCE_OUTSIDEVOLUMEHF:
            call.set_value<Exception>(props.lOutsideVolumeHF);
            break;

        case EAXSOURCE_DOPPLERFACTOR:
            call.set_value<Exception>(props.flDopplerFactor);
            break;

        case EAXSOURCE_ROLLOFFFACTOR:
            call.set_value<Exception>(props.flRolloffFactor);
            break;

        case EAXSOURCE_ROOMROLLOFFFACTOR:
            call.set_value<Exception>(props.flRoomRolloffFactor);
            break;

        case EAXSOURCE_AIRABSORPTIONFACTOR:
            call.set_value<Exception>(props.flAirAbsorptionFactor);
            break;

        case EAXSOURCE_FLAGS:
            call.set_value<Exception>(props.ulFlags);
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax4_get(const EaxCall& call, const Eax4Props& props)
{
    switch (call.get_property_id()) {
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
            call.set_value<Exception>(props.active_fx_slots);
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax5_get_all_2d(const EaxCall& call, const EAX50SOURCEPROPERTIES& props)
{
    auto& subprops = call.get_value<Exception, EAXSOURCE2DPROPERTIES>();
    subprops.lDirect = props.lDirect;
    subprops.lDirectHF = props.lDirectHF;
    subprops.lRoom = props.lRoom;
    subprops.lRoomHF = props.lRoomHF;
    subprops.ulFlags = props.ulFlags;
}

void ALsource::eax5_get_speaker_levels(const EaxCall& call, const EaxSpeakerLevels& props)
{
    const auto subprops = call.get_values<EAXSPEAKERLEVELPROPERTIES>(eax_max_speakers);
    std::uninitialized_copy_n(props.cbegin(), subprops.size(), subprops.begin());
}

void ALsource::eax5_get(const EaxCall& call, const Eax5Props& props)
{
    switch (call.get_property_id()) {
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
            call.set_value<Exception>(props.active_fx_slots);
            break;

        case EAXSOURCE_MACROFXFACTOR:
            call.set_value<Exception>(props.source.flMacroFXFactor);
            break;

        case EAXSOURCE_SPEAKERLEVELS:
            call.set_value<Exception>(props.speaker_levels);
            break;

        case EAXSOURCE_ALL2DPARAMETERS:
            eax5_get_all_2d(call, props.source);
            break;

        default:
            eax_fail_unknown_property_id();
    }
}

void ALsource::eax_get(const EaxCall& call)
{
    switch (call.get_version()) {
        case 1: eax1_get(call, eax1_.i); break;
        case 2: eax2_get(call, eax2_.i); break;
        case 3: eax3_get(call, eax3_.i); break;
        case 4: eax4_get(call, eax4_.i); break;
        case 5: eax5_get(call, eax5_.i); break;
        default: eax_fail_unknown_version();
    }
}

void ALsource::eax_set_al_source_send(ALeffectslot *slot, size_t sendidx, const EaxAlLowPassParam &filter)
{
    if(sendidx >= EAX_MAX_FXSLOTS)
        return;

    auto &send = Send[sendidx];
    send.Gain = filter.gain;
    send.GainHF = filter.gain_hf;
    send.HFReference = LOWPASSFREQREF;
    send.GainLF = 1.0f;
    send.LFReference = HIGHPASSFREQREF;

    if(slot != nullptr)
        IncrementRef(slot->ref);
    if(auto *oldslot = send.Slot)
        DecrementRef(oldslot->ref);

    send.Slot = slot;
    mPropsDirty = true;
}

void ALsource::eax_commit_active_fx_slots()
{
    // Mark all slots as non-active.
    eax_active_fx_slots_.fill(false);

    // Mark primary FX slot as active.
    if (eax_primary_fx_slot_id_.has_value())
        eax_active_fx_slots_[*eax_primary_fx_slot_id_] = true;

    // Mark the other FX slots as active.
    for (const auto& slot_id : eax_.active_fx_slots.guidActiveFXSlots) {
        if (slot_id == EAXPROPERTYID_EAX50_FXSlot0)
            eax_active_fx_slots_[0] = true;
        else if (slot_id == EAXPROPERTYID_EAX50_FXSlot1)
            eax_active_fx_slots_[1] = true;
        else if (slot_id == EAXPROPERTYID_EAX50_FXSlot2)
            eax_active_fx_slots_[2] = true;
        else if (slot_id == EAXPROPERTYID_EAX50_FXSlot3)
            eax_active_fx_slots_[3] = true;
    }

    // Deactivate EFX auxiliary effect slots.
    for (auto i = size_t{}; i < EAX_MAX_FXSLOTS; ++i) {
        if (!eax_active_fx_slots_[i])
            eax_set_al_source_send(nullptr, i, EaxAlLowPassParam{1.0f, 1.0f});
    }
}

void ALsource::eax_commit_filters()
{
    eax_update_direct_filter();
    eax_update_room_filters();
}

void ALsource::eax_commit(EaxCommitType commit_type)
{
    const auto primary_fx_slot_id = eax_al_context_->eax_get_primary_fx_slot_index();
    const auto is_primary_fx_slot_id_changed = (eax_primary_fx_slot_id_ == primary_fx_slot_id);

    const auto is_forced = (
        eax_is_version_changed_ ||
        is_primary_fx_slot_id_changed ||
        commit_type == EaxCommitType::forced);

    eax_is_version_changed_ = false;
    eax_primary_fx_slot_id_ = primary_fx_slot_id;

    switch (eax_version_) {
        case 1:
            if (!is_forced && eax1_.i == eax1_.d)
                return;
            eax1_.i = eax1_.d;
            eax1_translate(eax1_.i, eax_);
            break;

        case 2:
            if (!is_forced && eax2_.i == eax2_.d)
                return;
            eax2_.i = eax2_.d;
            eax2_translate(eax2_.i, eax_);
            break;

        case 3:
            if (!is_forced && eax3_.i == eax3_.d)
                return;
            eax3_.i = eax3_.d;
            eax3_translate(eax3_.i, eax_);
            break;

        case 4:
            if (!is_forced && eax4_.i == eax4_.d)
                return;
            eax4_.i = eax4_.d;
            eax4_translate(eax4_.i, eax_);
            break;

        case 5:
            if (!is_forced && eax5_.i == eax5_.d)
                return;
            eax5_.i = eax5_.d;
            eax_ = eax5_.d;
            break;

        default:
            eax_fail_unknown_version();
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
