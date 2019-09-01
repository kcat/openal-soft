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
#include <thread>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx.h"

#include "alcmain.h"
#include "alcontext.h"
#include "alexcpt.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "alspan.h"
#include "alu.h"
#include "ambidefs.h"
#include "atomic.h"
#include "auxeffectslot.h"
#include "backends/base.h"
#include "bformatdec.h"
#include "buffer.h"
#include "event.h"
#include "filter.h"
#include "filters/nfc.h"
#include "filters/splitter.h"
#include "inprogext.h"
#include "logging.h"
#include "math_defs.h"
#include "opthelpers.h"
#include "ringbuffer.h"
#include "threads.h"


namespace {

using namespace std::placeholders;

ALvoice *GetSourceVoice(ALsource *source, ALCcontext *context)
{
    ALuint idx{source->VoiceIdx};
    if(idx < context->mVoiceCount.load(std::memory_order_relaxed))
    {
        ALuint sid{source->id};
        ALvoice &voice = (*context->mVoices)[idx];
        if(voice.mSourceID.load(std::memory_order_acquire) == sid)
            return &voice;
    }
    source->VoiceIdx = INVALID_VOICE_IDX;
    return nullptr;
}

void UpdateSourceProps(const ALsource *source, ALvoice *voice, ALCcontext *context)
{
    /* Get an unused property container, or allocate a new one as needed. */
    ALvoiceProps *props{context->mFreeVoiceProps.load(std::memory_order_acquire)};
    if(!props)
        props = new ALvoiceProps{};
    else
    {
        ALvoiceProps *next;
        do {
            next = props->next.load(std::memory_order_relaxed);
        } while(context->mFreeVoiceProps.compare_exchange_weak(props, next,
                std::memory_order_acq_rel, std::memory_order_acquire) == 0);
    }

    /* Copy in current property values. */
    props->Pitch = source->Pitch;
    props->Gain = source->Gain;
    props->OuterGain = source->OuterGain;
    props->MinGain = source->MinGain;
    props->MaxGain = source->MaxGain;
    props->InnerAngle = source->InnerAngle;
    props->OuterAngle = source->OuterAngle;
    props->RefDistance = source->RefDistance;
    props->MaxDistance = source->MaxDistance;
    props->RolloffFactor = source->RolloffFactor;
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

    props->Direct.Gain = source->Direct.Gain;
    props->Direct.GainHF = source->Direct.GainHF;
    props->Direct.HFReference = source->Direct.HFReference;
    props->Direct.GainLF = source->Direct.GainLF;
    props->Direct.LFReference = source->Direct.LFReference;

    auto copy_send = [](const ALsource::SendData &srcsend) noexcept -> ALvoicePropsBase::SendData
    {
        ALvoicePropsBase::SendData ret;
        ret.Slot = srcsend.Slot;
        ret.Gain = srcsend.Gain;
        ret.GainHF = srcsend.GainHF;
        ret.HFReference = srcsend.HFReference;
        ret.GainLF = srcsend.GainLF;
        ret.LFReference = srcsend.LFReference;
        return ret;
    };
    std::transform(source->Send.cbegin(), source->Send.cend(), props->Send, copy_send);

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
int64_t GetSourceSampleOffset(ALsource *Source, ALCcontext *context, std::chrono::nanoseconds *clocktime)
{
    ALCdevice *device{context->mDevice.get()};
    const ALbufferlistitem *Current;
    uint64_t readPos;
    ALuint refcount;
    ALvoice *voice;

    do {
        Current = nullptr;
        readPos = 0;
        while(((refcount=device->MixCount.load(std::memory_order_acquire))&1))
            std::this_thread::yield();
        *clocktime = GetDeviceClockTime(device);

        voice = GetSourceVoice(Source, context);
        if(voice)
        {
            Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);

            readPos  = uint64_t{voice->mPosition.load(std::memory_order_relaxed)} << 32;
            readPos |= uint64_t{voice->mPositionFrac.load(std::memory_order_relaxed)} <<
                       (32-FRACTIONBITS);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->MixCount.load(std::memory_order_relaxed));

    if(voice)
    {
        const ALbufferlistitem *BufferList{Source->queue};
        while(BufferList && BufferList != Current)
        {
            readPos += uint64_t{BufferList->mSampleLen} << 32;
            BufferList = BufferList->mNext.load(std::memory_order_relaxed);
        }
        readPos = minu64(readPos, 0x7fffffffffffffff_u64);
    }

    return static_cast<int64_t>(readPos);
}

/* GetSourceSecOffset
 *
 * Gets the current read offset for the given Source, in seconds. The offset is
 * relative to the start of the queue (not the start of the current buffer).
 */
ALdouble GetSourceSecOffset(ALsource *Source, ALCcontext *context, std::chrono::nanoseconds *clocktime)
{
    ALCdevice *device{context->mDevice.get()};
    const ALbufferlistitem *Current;
    uint64_t readPos;
    ALuint refcount;
    ALvoice *voice;

    do {
        Current = nullptr;
        readPos = 0;
        while(((refcount=device->MixCount.load(std::memory_order_acquire))&1))
            std::this_thread::yield();
        *clocktime = GetDeviceClockTime(device);

        voice = GetSourceVoice(Source, context);
        if(voice)
        {
            Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);

            readPos  = uint64_t{voice->mPosition.load(std::memory_order_relaxed)} << FRACTIONBITS;
            readPos |= voice->mPositionFrac.load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->MixCount.load(std::memory_order_relaxed));

    ALdouble offset{0.0};
    if(voice)
    {
        const ALbufferlistitem *BufferList{Source->queue};
        const ALbuffer *BufferFmt{nullptr};
        while(BufferList && BufferList != Current)
        {
            if(!BufferFmt) BufferFmt = BufferList->mBuffer;
            readPos += uint64_t{BufferList->mSampleLen} << FRACTIONBITS;
            BufferList = BufferList->mNext.load(std::memory_order_relaxed);
        }

        while(BufferList && !BufferFmt)
        {
            BufferFmt = BufferList->mBuffer;
            BufferList = BufferList->mNext.load(std::memory_order_relaxed);
        }
        assert(BufferFmt != nullptr);

        offset = static_cast<ALdouble>(readPos) / ALdouble{FRACTIONONE} /
                 static_cast<ALdouble>(BufferFmt->Frequency);
    }

    return offset;
}

/* GetSourceOffset
 *
 * Gets the current read offset for the given Source, in the appropriate format
 * (Bytes, Samples or Seconds). The offset is relative to the start of the
 * queue (not the start of the current buffer).
 */
ALdouble GetSourceOffset(ALsource *Source, ALenum name, ALCcontext *context)
{
    ALCdevice *device{context->mDevice.get()};
    const ALbufferlistitem *Current;
    ALuint readPos;
    ALuint readPosFrac;
    ALuint refcount;
    ALvoice *voice;

    do {
        Current = nullptr;
        readPos = readPosFrac = 0;
        while(((refcount=device->MixCount.load(std::memory_order_acquire))&1))
            std::this_thread::yield();
        voice = GetSourceVoice(Source, context);
        if(voice)
        {
            Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);

            readPos = voice->mPosition.load(std::memory_order_relaxed);
            readPosFrac = voice->mPositionFrac.load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->MixCount.load(std::memory_order_relaxed));

    ALdouble offset{0.0};
    if(voice)
    {
        const ALbufferlistitem *BufferList{Source->queue};
        const ALbuffer *BufferFmt{nullptr};
        ALboolean readFin{AL_FALSE};
        ALuint totalBufferLen{0u};

        while(BufferList)
        {
            if(!BufferFmt) BufferFmt = BufferList->mBuffer;

            readFin |= (BufferList == Current);
            totalBufferLen += BufferList->mSampleLen;
            if(!readFin) readPos += BufferList->mSampleLen;

            BufferList = BufferList->mNext.load(std::memory_order_relaxed);
        }
        assert(BufferFmt != nullptr);

        if(Source->Looping)
            readPos %= totalBufferLen;
        else
        {
            /* Wrap back to 0 */
            if(readPos >= totalBufferLen)
                readPos = readPosFrac = 0;
        }

        offset = 0.0;
        switch(name)
        {
            case AL_SEC_OFFSET:
                offset = (readPos + static_cast<ALdouble>(readPosFrac)/FRACTIONONE) / BufferFmt->Frequency;
                break;

            case AL_SAMPLE_OFFSET:
                offset = readPos + static_cast<ALdouble>(readPosFrac)/FRACTIONONE;
                break;

            case AL_BYTE_OFFSET:
                if(BufferFmt->OriginalType == UserFmtIMA4)
                {
                    ALsizei align = (BufferFmt->OriginalAlign-1)/2 + 4;
                    ALuint BlockSize = align * ChannelsFromFmt(BufferFmt->mFmtChannels);
                    ALuint FrameBlockSize = BufferFmt->OriginalAlign;

                    /* Round down to nearest ADPCM block */
                    offset = static_cast<ALdouble>(readPos / FrameBlockSize * BlockSize);
                }
                else if(BufferFmt->OriginalType == UserFmtMSADPCM)
                {
                    ALsizei align = (BufferFmt->OriginalAlign-2)/2 + 7;
                    ALuint BlockSize = align * ChannelsFromFmt(BufferFmt->mFmtChannels);
                    ALuint FrameBlockSize = BufferFmt->OriginalAlign;

                    /* Round down to nearest ADPCM block */
                    offset = static_cast<ALdouble>(readPos / FrameBlockSize * BlockSize);
                }
                else
                {
                    const ALsizei FrameSize{FrameSizeFromFmt(BufferFmt->mFmtChannels,
                        BufferFmt->mFmtType)};
                    offset = static_cast<ALdouble>(readPos * FrameSize);
                }
                break;
        }
    }

    return offset;
}


/* GetSampleOffset
 *
 * Retrieves the sample offset into the Source's queue (from the Sample, Byte
 * or Second offset supplied by the application). This takes into account the
 * fact that the buffer format may have been modifed since.
 */
ALboolean GetSampleOffset(ALsource *Source, ALuint *offset, ALuint *frac)
{
    const ALbuffer *BufferFmt{nullptr};
    const ALbufferlistitem *BufferList;
 
    /* Find the first valid Buffer in the Queue */
    BufferList = Source->queue;
    while(BufferList && !BufferFmt)
    {
        BufferFmt = BufferList->mBuffer;
        BufferList = BufferList->mNext.load(std::memory_order_relaxed);
    }
    if(!BufferFmt)
    {
        Source->OffsetType = AL_NONE;
        Source->Offset = 0.0;
        return AL_FALSE;
    }

    ALdouble dbloff, dblfrac;
    switch(Source->OffsetType)
    {
    case AL_BYTE_OFFSET:
        /* Determine the ByteOffset (and ensure it is block aligned) */
        *offset = static_cast<ALuint>(Source->Offset);
        if(BufferFmt->OriginalType == UserFmtIMA4)
        {
            ALsizei align = (BufferFmt->OriginalAlign-1)/2 + 4;
            *offset /= align * ChannelsFromFmt(BufferFmt->mFmtChannels);
            *offset *= BufferFmt->OriginalAlign;
        }
        else if(BufferFmt->OriginalType == UserFmtMSADPCM)
        {
            ALsizei align = (BufferFmt->OriginalAlign-2)/2 + 7;
            *offset /= align * ChannelsFromFmt(BufferFmt->mFmtChannels);
            *offset *= BufferFmt->OriginalAlign;
        }
        else
            *offset /= FrameSizeFromFmt(BufferFmt->mFmtChannels, BufferFmt->mFmtType);
        *frac = 0;
        break;

    case AL_SAMPLE_OFFSET:
        dblfrac = modf(Source->Offset, &dbloff);
        *offset = static_cast<ALuint>(mind(dbloff, std::numeric_limits<ALuint>::max()));
        *frac = static_cast<ALuint>(mind(dblfrac*FRACTIONONE, FRACTIONONE-1.0));
        break;

    case AL_SEC_OFFSET:
        dblfrac = modf(Source->Offset*BufferFmt->Frequency, &dbloff);
        *offset = static_cast<ALuint>(mind(dbloff, std::numeric_limits<ALuint>::max()));
        *frac = static_cast<ALuint>(mind(dblfrac*FRACTIONONE, FRACTIONONE-1.0));
        break;
    }
    Source->OffsetType = AL_NONE;
    Source->Offset = 0.0;

    return AL_TRUE;
}

/* ApplyOffset
 *
 * Apply the stored playback offset to the Source. This function will update
 * the number of buffers "played" given the stored offset.
 */
ALboolean ApplyOffset(ALsource *Source, ALvoice *voice)
{
    /* Get sample frame offset */
    ALuint offset{0u};
    ALuint frac{0u};
    if(!GetSampleOffset(Source, &offset, &frac))
        return AL_FALSE;

    ALuint totalBufferLen{0u};
    ALbufferlistitem *BufferList{Source->queue};
    while(BufferList && totalBufferLen <= offset)
    {
        if(BufferList->mSampleLen > offset-totalBufferLen)
        {
            /* Offset is in this buffer */
            voice->mPosition.store(offset - totalBufferLen, std::memory_order_relaxed);
            voice->mPositionFrac.store(frac, std::memory_order_relaxed);
            voice->mCurrentBuffer.store(BufferList, std::memory_order_release);
            return AL_TRUE;
        }
        totalBufferLen += BufferList->mSampleLen;

        BufferList = BufferList->mNext.load(std::memory_order_relaxed);
    }

    /* Offset is out of range of the queue */
    return AL_FALSE;
}


ALsource *AllocSource(ALCcontext *context)
{
    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{context->mSourceLock};
    if(context->mNumSources >= device->SourcesMax)
    {
        context->setError(AL_OUT_OF_MEMORY, "Exceeding %u source limit", device->SourcesMax);
        return nullptr;
    }
    auto sublist = std::find_if(context->mSourceList.begin(), context->mSourceList.end(),
        [](const SourceSubList &entry) noexcept -> bool
        { return entry.FreeMask != 0; }
    );
    auto lidx = static_cast<ALsizei>(std::distance(context->mSourceList.begin(), sublist));
    ALsource *source;
    ALsizei slidx;
    if LIKELY(sublist != context->mSourceList.end())
    {
        slidx = CTZ64(sublist->FreeMask);
        source = sublist->Sources + slidx;
    }
    else
    {
        /* Don't allocate so many list entries that the 32-bit ID could
         * overflow...
         */
        if UNLIKELY(context->mSourceList.size() >= 1<<25)
        {
            context->setError(AL_OUT_OF_MEMORY, "Too many sources allocated");
            return nullptr;
        }
        context->mSourceList.emplace_back();
        sublist = context->mSourceList.end() - 1;

        sublist->FreeMask = ~0_u64;
        sublist->Sources = static_cast<ALsource*>(al_calloc(16, sizeof(ALsource)*64));
        if UNLIKELY(!sublist->Sources)
        {
            context->mSourceList.pop_back();
            context->setError(AL_OUT_OF_MEMORY, "Failed to allocate source batch");
            return nullptr;
        }

        slidx = 0;
        source = sublist->Sources + slidx;
    }

    source = new (source) ALsource{device->NumAuxSends};

    /* Add 1 to avoid source ID 0. */
    source->id = ((lidx<<6) | slidx) + 1;

    context->mNumSources += 1;
    sublist->FreeMask &= ~(1_u64 << slidx);

    return source;
}

void FreeSource(ALCcontext *context, ALsource *source)
{
    ALuint id = source->id - 1;
    ALsizei lidx = id >> 6;
    ALsizei slidx = id & 0x3f;

    ALCdevice *device{context->mDevice.get()};
    BackendUniqueLock backlock{*device->Backend};
    if(ALvoice *voice{GetSourceVoice(source, context)})
    {
        voice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
        voice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
        voice->mSourceID.store(0u, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        /* Don't set the voice to stopping if it was already stopped or
         * stopping.
         */
        ALvoice::State oldvstate{ALvoice::Playing};
        voice->mPlayState.compare_exchange_strong(oldvstate, ALvoice::Stopping,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }
    backlock.unlock();

    al::destroy_at(source);

    context->mSourceList[lidx].FreeMask |= 1_u64 << slidx;
    context->mNumSources--;
}


inline ALsource *LookupSource(ALCcontext *context, ALuint id) noexcept
{
    ALuint lidx = (id-1) >> 6;
    ALsizei slidx = (id-1) & 0x3f;

    if UNLIKELY(lidx >= context->mSourceList.size())
        return nullptr;
    SourceSubList &sublist{context->mSourceList[lidx]};
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.Sources + slidx;
}

inline ALbuffer *LookupBuffer(ALCdevice *device, ALuint id) noexcept
{
    ALuint lidx = (id-1) >> 6;
    ALsizei slidx = (id-1) & 0x3f;

    if UNLIKELY(lidx >= device->BufferList.size())
        return nullptr;
    BufferSubList &sublist = device->BufferList[lidx];
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.Buffers + slidx;
}

inline ALfilter *LookupFilter(ALCdevice *device, ALuint id) noexcept
{
    ALuint lidx = (id-1) >> 6;
    ALsizei slidx = (id-1) & 0x3f;

    if UNLIKELY(lidx >= device->FilterList.size())
        return nullptr;
    FilterSubList &sublist = device->FilterList[lidx];
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.Filters + slidx;
}

inline ALeffectslot *LookupEffectSlot(ALCcontext *context, ALuint id) noexcept
{
    ALuint lidx = (id-1) >> 6;
    ALsizei slidx = (id-1) & 0x3f;

    if UNLIKELY(lidx >= context->mEffectSlotList.size())
        return nullptr;
    EffectSlotSubList &sublist{context->mEffectSlotList[lidx]};
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.EffectSlots + slidx;
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

    /* AL_SOFT_source_resampler */
    srcResampler = AL_SOURCE_RESAMPLER_SOFT,

    /* AL_SOFT_source_spatialize */
    srcSpatialize = AL_SOURCE_SPATIALIZE_SOFT,

    /* ALC_SOFT_device_clock */
    srcSampleOffsetClockSOFT = AL_SAMPLE_OFFSET_CLOCK_SOFT,
    srcSecOffsetClockSOFT = AL_SEC_OFFSET_CLOCK_SOFT,
};

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
inline ALenum GetSourceState(ALsource *source, ALvoice *voice)
{
    if(!voice && source->state == AL_PLAYING)
        source->state = AL_STOPPED;
    return source->state;
}

/**
 * Returns if the source should specify an update, given the context's
 * deferring state and the source's last known state.
 */
inline bool SourceShouldUpdate(ALsource *source, ALCcontext *context)
{
    return !context->mDeferUpdates.load(std::memory_order_acquire) &&
           IsPlayingOrPaused(source);
}


/** Can only be called while the mixer is locked! */
void SendStateChangeEvent(ALCcontext *context, ALuint id, ALenum state)
{
    ALbitfieldSOFT enabledevt{context->mEnabledEvts.load(std::memory_order_acquire)};
    if(!(enabledevt&EventType_SourceStateChange)) return;

    /* The mixer may have queued a state change that's not yet been processed,
     * and we don't want state change messages to occur out of order, so send
     * it through the async queue to ensure proper ordering.
     */
    RingBuffer *ring{context->mAsyncEvents.get()};
    auto evt_vec = ring->getWriteVector();
    if(evt_vec.first.len < 1) return;

    AsyncEvent *evt{new (evt_vec.first.buf) AsyncEvent{EventType_SourceStateChange}};
    evt->u.srcstate.id = id;
    evt->u.srcstate.state = state;
    ring->writeAdvance(1);
    context->mEventSem.post();
}


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


bool SetSourcefv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<const ALfloat> values);
bool SetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<const ALint> values);
bool SetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<const ALint64SOFT> values);

#define CHECKSIZE(v, s) do { \
    if LIKELY((v).size() == (s) || (v).size() == MaxValues) break;            \
    Context->setError(AL_INVALID_ENUM,                                        \
        "Property 0x%04x expects %d value(s), got %zu", prop, (s),            \
        (v).size());                                                          \
    return false;                                                             \
} while(0)
#define CHECKVAL(x) do {                                                      \
    if LIKELY(x) break;                                                       \
    Context->setError(AL_INVALID_VALUE, "Value out of range");                \
    return false;                                                             \
} while(0)

bool UpdateSourceProps(ALsource *source, ALCcontext *context)
{
    ALvoice *voice;
    if(SourceShouldUpdate(source, context) && (voice=GetSourceVoice(source, context)) != nullptr)
        UpdateSourceProps(source, voice, context);
    else
        source->PropsClean.clear(std::memory_order_release);
    return true;
}

bool SetSourcefv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<const ALfloat> values)
{
    ALint ival;

    switch(prop)
    {
        case AL_SEC_OFFSET_LATENCY_SOFT:
        case AL_SEC_OFFSET_CLOCK_SOFT:
            /* Query only */
            SETERR_RETURN(Context, AL_INVALID_OPERATION, false,
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
            return UpdateSourceProps(Source, Context);

        case AL_CONE_OUTER_ANGLE:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] >= 0.0f && values[0] <= 360.0f);

            Source->OuterAngle = values[0];
            return UpdateSourceProps(Source, Context);

        case AL_GAIN:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] >= 0.0f);

            Source->Gain = values[0];
            return UpdateSourceProps(Source, Context);

        case AL_MAX_DISTANCE:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] >= 0.0f);

            Source->MaxDistance = values[0];
            return UpdateSourceProps(Source, Context);

        case AL_ROLLOFF_FACTOR:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] >= 0.0f);

            Source->RolloffFactor = values[0];
            return UpdateSourceProps(Source, Context);

        case AL_REFERENCE_DISTANCE:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] >= 0.0f);

            Source->RefDistance = values[0];
            return UpdateSourceProps(Source, Context);

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

            Source->OffsetType = prop;
            Source->Offset = values[0];

            if(IsPlayingOrPaused(Source))
            {
                ALCdevice *device{Context->mDevice.get()};
                BackendLockGuard _{*device->Backend};
                /* Double-check that the source is still playing while we have
                 * the lock.
                 */
                if(ALvoice *voice{GetSourceVoice(Source, Context)})
                {
                    if(ApplyOffset(Source, voice) == AL_FALSE)
                        SETERR_RETURN(Context, AL_INVALID_VALUE, false, "Invalid offset");
                }
            }
            return true;

        case AL_SOURCE_RADIUS:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] >= 0.0f && std::isfinite(values[0]));

            Source->Radius = values[0];
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
            return UpdateSourceProps(Source, Context);

        case AL_VELOCITY:
            CHECKSIZE(values, 3);
            CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]));

            Source->Velocity[0] = values[0];
            Source->Velocity[1] = values[1];
            Source->Velocity[2] = values[2];
            return UpdateSourceProps(Source, Context);

        case AL_DIRECTION:
            CHECKSIZE(values, 3);
            CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]));

            Source->Direction[0] = values[0];
            Source->Direction[1] = values[1];
            Source->Direction[2] = values[2];
            return UpdateSourceProps(Source, Context);

        case AL_ORIENTATION:
            CHECKSIZE(values, 6);
            CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]) &&
                     std::isfinite(values[3]) && std::isfinite(values[4]) && std::isfinite(values[5]));

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
            CHECKSIZE(values, 1);
            ival = static_cast<ALint>(values[0]);
            return SetSourceiv(Source, Context, prop, {&ival, 1u});

        case AL_BUFFERS_QUEUED:
        case AL_BUFFERS_PROCESSED:
            CHECKSIZE(values, 1);
            ival = static_cast<ALint>(static_cast<ALuint>(values[0]));
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
    return false;
}

bool SetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<const ALint> values)
{
    ALCdevice *device{Context->mDevice.get()};
    ALbuffer *buffer{nullptr};
    ALfilter *filter{nullptr};
    ALeffectslot *slot{nullptr};
    ALbufferlistitem *oldlist{nullptr};
    std::unique_lock<std::mutex> slotlock;
    std::unique_lock<std::mutex> filtlock;
    std::unique_lock<std::mutex> buflock;
    ALfloat fvals[6];

    switch(prop)
    {
        case AL_SOURCE_STATE:
        case AL_SOURCE_TYPE:
        case AL_BUFFERS_QUEUED:
        case AL_BUFFERS_PROCESSED:
            /* Query only */
            SETERR_RETURN(Context, AL_INVALID_OPERATION, false,
                "Setting read-only source property 0x%04x", prop);

        case AL_SOURCE_RELATIVE:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->HeadRelative = static_cast<ALboolean>(values[0]);
            return UpdateSourceProps(Source, Context);

        case AL_LOOPING:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->Looping = static_cast<ALboolean>(values[0]);
            if(IsPlayingOrPaused(Source))
            {
                if(ALvoice *voice{GetSourceVoice(Source, Context)})
                {
                    if(Source->Looping)
                        voice->mLoopBuffer.store(Source->queue, std::memory_order_release);
                    else
                        voice->mLoopBuffer.store(nullptr, std::memory_order_release);

                    /* If the source is playing, wait for the current mix to finish
                     * to ensure it isn't currently looping back or reaching the
                     * end.
                     */
                    while((device->MixCount.load(std::memory_order_acquire)&1))
                        std::this_thread::yield();
                }
            }
            return true;

        case AL_BUFFER:
            CHECKSIZE(values, 1);
            buflock = std::unique_lock<std::mutex>{device->BufferLock};
            if(!(values[0] == 0 || (buffer=LookupBuffer(device, values[0])) != nullptr))
                SETERR_RETURN(Context, AL_INVALID_VALUE, false, "Invalid buffer ID %u", values[0]);

            if(buffer && buffer->MappedAccess != 0 &&
               !(buffer->MappedAccess&AL_MAP_PERSISTENT_BIT_SOFT))
                SETERR_RETURN(Context, AL_INVALID_OPERATION, false,
                    "Setting non-persistently mapped buffer %u", buffer->id);
            else
            {
                ALenum state = GetSourceState(Source, GetSourceVoice(Source, Context));
                if(state == AL_PLAYING || state == AL_PAUSED)
                    SETERR_RETURN(Context, AL_INVALID_OPERATION, false,
                        "Setting buffer on playing or paused source %u", Source->id);
            }

            oldlist = Source->queue;
            if(buffer != nullptr)
            {
                /* Add the selected buffer to a one-item queue */
                auto newlist = new ALbufferlistitem{};
                newlist->mSampleLen = buffer->SampleLen;
                newlist->mBuffer = buffer;
                IncrementRef(buffer->ref);

                /* Source is now Static */
                Source->SourceType = AL_STATIC;
                Source->queue = newlist;
            }
            else
            {
                /* Source is now Undetermined */
                Source->SourceType = AL_UNDETERMINED;
                Source->queue = nullptr;
            }
            buflock.unlock();

            /* Delete all elements in the previous queue */
            while(oldlist != nullptr)
            {
                std::unique_ptr<ALbufferlistitem> temp{oldlist};
                oldlist = temp->mNext.load(std::memory_order_relaxed);

                if(ALbuffer *buffer{temp->mBuffer})
                    DecrementRef(buffer->ref);
            }
            return true;

        case AL_SEC_OFFSET:
        case AL_SAMPLE_OFFSET:
        case AL_BYTE_OFFSET:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] >= 0);

            Source->OffsetType = prop;
            Source->Offset = values[0];

            if(IsPlayingOrPaused(Source))
            {
                ALCdevice *device{Context->mDevice.get()};
                BackendLockGuard _{*device->Backend};
                if(ALvoice *voice{GetSourceVoice(Source, Context)})
                {
                    if(ApplyOffset(Source, voice) == AL_FALSE)
                        SETERR_RETURN(Context, AL_INVALID_VALUE, false, "Invalid source offset");
                }
            }
            return true;

        case AL_DIRECT_FILTER:
            CHECKSIZE(values, 1);
            filtlock = std::unique_lock<std::mutex>{device->FilterLock};
            if(!(values[0] == 0 || (filter=LookupFilter(device, values[0])) != nullptr))
                SETERR_RETURN(Context, AL_INVALID_VALUE, false, "Invalid filter ID %u", values[0]);

            if(!filter)
            {
                Source->Direct.Gain = 1.0f;
                Source->Direct.GainHF = 1.0f;
                Source->Direct.HFReference = LOWPASSFREQREF;
                Source->Direct.GainLF = 1.0f;
                Source->Direct.LFReference = HIGHPASSFREQREF;
            }
            else
            {
                Source->Direct.Gain = filter->Gain;
                Source->Direct.GainHF = filter->GainHF;
                Source->Direct.HFReference = filter->HFReference;
                Source->Direct.GainLF = filter->GainLF;
                Source->Direct.LFReference = filter->LFReference;
            }
            filtlock.unlock();
            return UpdateSourceProps(Source, Context);

        case AL_DIRECT_FILTER_GAINHF_AUTO:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->DryGainHFAuto = values[0];
            return UpdateSourceProps(Source, Context);

        case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->WetGainAuto = values[0];
            return UpdateSourceProps(Source, Context);

        case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->WetGainHFAuto = values[0];
            return UpdateSourceProps(Source, Context);

        case AL_DIRECT_CHANNELS_SOFT:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] == AL_FALSE || values[0] == AL_TRUE);

            Source->DirectChannels = values[0];
            return UpdateSourceProps(Source, Context);

        case AL_DISTANCE_MODEL:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] == AL_NONE ||
                values[0] == AL_INVERSE_DISTANCE || values[0] == AL_INVERSE_DISTANCE_CLAMPED ||
                values[0] == AL_LINEAR_DISTANCE || values[0] == AL_LINEAR_DISTANCE_CLAMPED ||
                values[0] == AL_EXPONENT_DISTANCE || values[0] == AL_EXPONENT_DISTANCE_CLAMPED);

            Source->mDistanceModel = static_cast<DistanceModel>(values[0]);
            if(Context->mSourceDistanceModel)
                return UpdateSourceProps(Source, Context);
            return true;

        case AL_SOURCE_RESAMPLER_SOFT:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] >= 0 && values[0] <= ResamplerMax);

            Source->mResampler = static_cast<Resampler>(values[0]);
            return UpdateSourceProps(Source, Context);

        case AL_SOURCE_SPATIALIZE_SOFT:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] >= AL_FALSE && values[0] <= AL_AUTO_SOFT);

            Source->mSpatialize = static_cast<SpatializeMode>(values[0]);
            return UpdateSourceProps(Source, Context);


        case AL_AUXILIARY_SEND_FILTER:
            CHECKSIZE(values, 3);
            slotlock = std::unique_lock<std::mutex>{Context->mEffectSlotLock};
            if(!(values[0] == 0 || (slot=LookupEffectSlot(Context, values[0])) != nullptr))
                SETERR_RETURN(Context, AL_INVALID_VALUE, false, "Invalid effect ID %u", values[0]);
            if(static_cast<ALuint>(values[1]) >= static_cast<ALuint>(device->NumAuxSends))
                SETERR_RETURN(Context, AL_INVALID_VALUE, false, "Invalid send %u", values[1]);

            filtlock = std::unique_lock<std::mutex>{device->FilterLock};
            if(!(values[2] == 0 || (filter=LookupFilter(device, values[2])) != nullptr))
                SETERR_RETURN(Context, AL_INVALID_VALUE, false, "Invalid filter ID %u", values[2]);

            if(!filter)
            {
                /* Disable filter */
                Source->Send[values[1]].Gain = 1.0f;
                Source->Send[values[1]].GainHF = 1.0f;
                Source->Send[values[1]].HFReference = LOWPASSFREQREF;
                Source->Send[values[1]].GainLF = 1.0f;
                Source->Send[values[1]].LFReference = HIGHPASSFREQREF;
            }
            else
            {
                Source->Send[values[1]].Gain = filter->Gain;
                Source->Send[values[1]].GainHF = filter->GainHF;
                Source->Send[values[1]].HFReference = filter->HFReference;
                Source->Send[values[1]].GainLF = filter->GainLF;
                Source->Send[values[1]].LFReference = filter->LFReference;
            }
            filtlock.unlock();

            if(slot != Source->Send[values[1]].Slot && IsPlayingOrPaused(Source))
            {
                /* Add refcount on the new slot, and release the previous slot */
                if(slot) IncrementRef(slot->ref);
                if(Source->Send[values[1]].Slot)
                    DecrementRef(Source->Send[values[1]].Slot->ref);
                Source->Send[values[1]].Slot = slot;

                /* We must force an update if the auxiliary slot changed on an
                 * active source, in case the slot is about to be deleted.
                 */
                ALvoice *voice{GetSourceVoice(Source, Context)};
                if(voice) UpdateSourceProps(Source, voice, Context);
                else Source->PropsClean.clear(std::memory_order_release);
            }
            else
            {
                if(slot) IncrementRef(slot->ref);
                if(Source->Send[values[1]].Slot)
                    DecrementRef(Source->Send[values[1]].Slot->ref);
                Source->Send[values[1]].Slot = slot;
                UpdateSourceProps(Source, Context);
            }
            return true;


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
            CHECKSIZE(values, 1);
            fvals[0] = static_cast<ALfloat>(values[0]);
            return SetSourcefv(Source, Context, prop, {fvals, 1u});

        /* 3x float */
        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            CHECKSIZE(values, 3);
            fvals[0] = static_cast<ALfloat>(values[0]);
            fvals[1] = static_cast<ALfloat>(values[1]);
            fvals[2] = static_cast<ALfloat>(values[2]);
            return SetSourcefv(Source, Context, prop, {fvals, 3u});

        /* 6x float */
        case AL_ORIENTATION:
            CHECKSIZE(values, 6);
            fvals[0] = static_cast<ALfloat>(values[0]);
            fvals[1] = static_cast<ALfloat>(values[1]);
            fvals[2] = static_cast<ALfloat>(values[2]);
            fvals[3] = static_cast<ALfloat>(values[3]);
            fvals[4] = static_cast<ALfloat>(values[4]);
            fvals[5] = static_cast<ALfloat>(values[5]);
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
    return false;
}

bool SetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<const ALint64SOFT> values)
{
    ALfloat fvals[MaxValues];
    ALint   ivals[MaxValues];

    switch(prop)
    {
        case AL_SOURCE_TYPE:
        case AL_BUFFERS_QUEUED:
        case AL_BUFFERS_PROCESSED:
        case AL_SOURCE_STATE:
        case AL_SAMPLE_OFFSET_LATENCY_SOFT:
        case AL_SAMPLE_OFFSET_CLOCK_SOFT:
            /* Query only */
            SETERR_RETURN(Context, AL_INVALID_OPERATION, false,
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
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] <= INT_MAX && values[0] >= INT_MIN);

            ivals[0] = static_cast<ALint>(values[0]);
            return SetSourceiv(Source, Context, prop, {ivals, 1u});

        /* 1x uint */
        case AL_BUFFER:
        case AL_DIRECT_FILTER:
            CHECKSIZE(values, 1);
            CHECKVAL(values[0] <= UINT_MAX && values[0] >= 0);

            ivals[0] = static_cast<ALuint>(values[0]);
            return SetSourceiv(Source, Context, prop, {ivals, 1u});

        /* 3x uint */
        case AL_AUXILIARY_SEND_FILTER:
            CHECKSIZE(values, 3);
            CHECKVAL(values[0] <= UINT_MAX && values[0] >= 0 &&
                     values[1] <= UINT_MAX && values[1] >= 0 &&
                     values[2] <= UINT_MAX && values[2] >= 0);

            ivals[0] = static_cast<ALuint>(values[0]);
            ivals[1] = static_cast<ALuint>(values[1]);
            ivals[2] = static_cast<ALuint>(values[2]);
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
            CHECKSIZE(values, 1);
            fvals[0] = static_cast<ALfloat>(values[0]);
            return SetSourcefv(Source, Context, prop, {fvals, 1u});

        /* 3x float */
        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            CHECKSIZE(values, 3);
            fvals[0] = static_cast<ALfloat>(values[0]);
            fvals[1] = static_cast<ALfloat>(values[1]);
            fvals[2] = static_cast<ALfloat>(values[2]);
            return SetSourcefv(Source, Context, prop, {fvals, 3u});

        /* 6x float */
        case AL_ORIENTATION:
            CHECKSIZE(values, 6);
            fvals[0] = static_cast<ALfloat>(values[0]);
            fvals[1] = static_cast<ALfloat>(values[1]);
            fvals[2] = static_cast<ALfloat>(values[2]);
            fvals[3] = static_cast<ALfloat>(values[3]);
            fvals[4] = static_cast<ALfloat>(values[4]);
            fvals[5] = static_cast<ALfloat>(values[5]);
            return SetSourcefv(Source, Context, prop, {fvals, 6u});

        case AL_SEC_OFFSET_LATENCY_SOFT:
        case AL_SEC_OFFSET_CLOCK_SOFT:
        case AL_STEREO_ANGLES:
            break;
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    Context->setError(AL_INVALID_ENUM, "Invalid source integer64 property 0x%04x", prop);
    return false;
}

#undef CHECKVAL


bool GetSourcedv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<ALdouble> values);
bool GetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<ALint> values);
bool GetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<ALint64SOFT> values);

bool GetSourcedv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<ALdouble> values)
{
    ALCdevice *device{Context->mDevice.get()};
    ClockLatency clocktime;
    std::chrono::nanoseconds srcclock;
    ALint ivals[MaxValues];
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

        case AL_STEREO_ANGLES:
            CHECKSIZE(values, 2);
            values[0] = Source->StereoPan[0];
            values[1] = Source->StereoPan[1];
            return true;

        case AL_SEC_OFFSET_LATENCY_SOFT:
            CHECKSIZE(values, 2);
            /* Get the source offset with the clock time first. Then get the
             * clock time with the device latency. Order is important.
             */
            values[0] = GetSourceSecOffset(Source, Context, &srcclock);
            { std::lock_guard<std::mutex> _{device->StateLock};
                clocktime = GetClockLatency(device);
            }
            if(srcclock == clocktime.ClockTime)
                values[1] = static_cast<ALdouble>(clocktime.Latency.count()) / 1000000000.0;
            else
            {
                /* If the clock time incremented, reduce the latency by that
                 * much since it's that much closer to the source offset it got
                 * earlier.
                 */
                std::chrono::nanoseconds diff = clocktime.ClockTime - srcclock;
                values[1] = static_cast<ALdouble>((clocktime.Latency - std::min(clocktime.Latency, diff)).count()) /
                            1000000000.0;
            }
            return true;

        case AL_SEC_OFFSET_CLOCK_SOFT:
            CHECKSIZE(values, 2);
            values[0] = GetSourceSecOffset(Source, Context, &srcclock);
            values[1] = srcclock.count() / 1000000000.0;
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
            CHECKSIZE(values, 1);
            if((err=GetSourceiv(Source, Context, prop, {ivals, 1u})) != false)
                values[0] = static_cast<ALdouble>(ivals[0]);
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

bool GetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<ALint> values)
{
    ALbufferlistitem *BufferList;
    ALdouble dvals[MaxValues];
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
            BufferList = (Source->SourceType == AL_STATIC) ? Source->queue : nullptr;
            values[0] = (BufferList && BufferList->mBuffer) ? BufferList->mBuffer->id : 0;
            return true;

        case AL_SOURCE_STATE:
            CHECKSIZE(values, 1);
            values[0] = GetSourceState(Source, GetSourceVoice(Source, Context));
            return true;

        case AL_BUFFERS_QUEUED:
            CHECKSIZE(values, 1);
            if(!(BufferList=Source->queue))
                values[0] = 0;
            else
            {
                ALsizei count{0};
                do {
                    ++count;
                    BufferList = BufferList->mNext.load(std::memory_order_relaxed);
                } while(BufferList != nullptr);
                values[0] = count;
            }
            return true;

        case AL_BUFFERS_PROCESSED:
            CHECKSIZE(values, 1);
            if(Source->Looping || Source->SourceType != AL_STREAMING)
            {
                /* Buffers on a looping source are in a perpetual state of
                 * PENDING, so don't report any as PROCESSED */
                values[0] = 0;
            }
            else
            {
                const ALbufferlistitem *BufferList{Source->queue};
                const ALbufferlistitem *Current{nullptr};
                ALsizei played{0};

                ALvoice *voice{GetSourceVoice(Source, Context)};
                if(voice != nullptr)
                    Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);
                else if(Source->state == AL_INITIAL)
                    Current = BufferList;

                while(BufferList && BufferList != Current)
                {
                    ++played;
                    BufferList = BufferList->mNext.load(std::memory_order_relaxed);
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
            values[0] = Source->DirectChannels;
            return true;

        case AL_DISTANCE_MODEL:
            CHECKSIZE(values, 1);
            values[0] = static_cast<int>(Source->mDistanceModel);
            return true;

        case AL_SOURCE_RESAMPLER_SOFT:
            CHECKSIZE(values, 1);
            values[0] = Source->mResampler;
            return true;

        case AL_SOURCE_SPATIALIZE_SOFT:
            CHECKSIZE(values, 1);
            values[0] = Source->mSpatialize;
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
            CHECKSIZE(values, 1);
            if((err=GetSourcedv(Source, Context, prop, {dvals, 1u})) != false)
                values[0] = static_cast<ALint>(dvals[0]);
            return err;

        /* 3x float/double */
        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            CHECKSIZE(values, 3);
            if((err=GetSourcedv(Source, Context, prop, {dvals, 3u})) != false)
            {
                values[0] = static_cast<ALint>(dvals[0]);
                values[1] = static_cast<ALint>(dvals[1]);
                values[2] = static_cast<ALint>(dvals[2]);
            }
            return err;

        /* 6x float/double */
        case AL_ORIENTATION:
            CHECKSIZE(values, 6);
            if((err=GetSourcedv(Source, Context, prop, {dvals, 6u})) != false)
            {
                values[0] = static_cast<ALint>(dvals[0]);
                values[1] = static_cast<ALint>(dvals[1]);
                values[2] = static_cast<ALint>(dvals[2]);
                values[3] = static_cast<ALint>(dvals[3]);
                values[4] = static_cast<ALint>(dvals[4]);
                values[5] = static_cast<ALint>(dvals[5]);
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

bool GetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, const al::span<ALint64SOFT> values)
{
    ALCdevice *device = Context->mDevice.get();
    ClockLatency clocktime;
    std::chrono::nanoseconds srcclock;
    ALdouble dvals[MaxValues];
    ALint ivals[MaxValues];
    bool err;

    switch(prop)
    {
        case AL_SAMPLE_OFFSET_LATENCY_SOFT:
            CHECKSIZE(values, 2);
            /* Get the source offset with the clock time first. Then get the
             * clock time with the device latency. Order is important.
             */
            values[0] = GetSourceSampleOffset(Source, Context, &srcclock);
            { std::lock_guard<std::mutex> _{device->StateLock};
                clocktime = GetClockLatency(device);
            }
            if(srcclock == clocktime.ClockTime)
                values[1] = clocktime.Latency.count();
            else
            {
                /* If the clock time incremented, reduce the latency by that
                 * much since it's that much closer to the source offset it got
                 * earlier.
                 */
                auto diff = clocktime.ClockTime - srcclock;
                values[1] = (clocktime.Latency - std::min(clocktime.Latency, diff)).count();
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

AL_API ALvoid AL_APIENTRY alGenSources(ALsizei n, ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Generating %d sources", n);
    else if(n == 1)
    {
        ALsource *source = AllocSource(context.get());
        if(source) sources[0] = source->id;
    }
    else
    {
        al::vector<ALuint> tempids(n);
        auto alloc_end = std::find_if_not(tempids.begin(), tempids.end(),
            [&context](ALuint &id) -> bool
            {
                ALsource *source{AllocSource(context.get())};
                if(!source) return false;
                id = source->id;
                return true;
            }
        );
        if(alloc_end != tempids.end())
            alDeleteSources(static_cast<ALsizei>(std::distance(tempids.begin(), alloc_end)),
                tempids.data());
        else
            std::copy(tempids.cbegin(), tempids.cend(), sources);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alDeleteSources(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        SETERR_RETURN(context, AL_INVALID_VALUE,, "Deleting %d sources", n);

    std::lock_guard<std::mutex> _{context->mSourceLock};

    /* Check that all Sources are valid */
    const ALuint *sources_end = sources + n;
    auto invsrc = std::find_if_not(sources, sources_end,
        [&context](ALuint sid) -> bool
        {
            if(!LookupSource(context.get(), sid))
            {
                context->setError(AL_INVALID_NAME, "Invalid source ID %u", sid);
                return false;
            }
            return true;
        }
    );
    if LIKELY(invsrc == sources_end)
    {
        /* All good. Delete source IDs. */
        std::for_each(sources, sources_end,
            [&context](ALuint sid) -> void
            {
                ALsource *src{LookupSource(context.get(), sid)};
                if(src) FreeSource(context.get(), src);
            }
        );
    }
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


AL_API ALvoid AL_APIENTRY alSourcef(ALuint source, ALenum param, ALfloat value)
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

AL_API ALvoid AL_APIENTRY alSource3f(ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
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
        const ALfloat fvals[3]{ value1, value2, value3 };
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), fvals);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alSourcefv(ALuint source, ALenum param, const ALfloat *values)
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


AL_API ALvoid AL_APIENTRY alSourcedSOFT(ALuint source, ALenum param, ALdouble value)
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
        const ALfloat fval[1]{static_cast<ALfloat>(value)};
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), fval);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alSource3dSOFT(ALuint source, ALenum param, ALdouble value1, ALdouble value2, ALdouble value3)
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
        const ALfloat fvals[3]{static_cast<ALfloat>(value1), static_cast<ALfloat>(value2),
            static_cast<ALfloat>(value3)};
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), fvals);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alSourcedvSOFT(ALuint source, ALenum param, const ALdouble *values)
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
        ALfloat fvals[MaxValues];
        for(ALuint i{0};i < count;i++)
            fvals[i] = static_cast<ALfloat>(values[i]);
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), {fvals, count});
    }
}
END_API_FUNC


AL_API ALvoid AL_APIENTRY alSourcei(ALuint source, ALenum param, ALint value)
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
        const ALint ivals[3]{ value1, value2, value3 };
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


AL_API ALvoid AL_APIENTRY alSourcei64SOFT(ALuint source, ALenum param, ALint64SOFT value)
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
        const ALint64SOFT i64vals[3]{ value1, value2, value3 };
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


AL_API ALvoid AL_APIENTRY alGetSourcef(ALuint source, ALenum param, ALfloat *value)
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
        ALdouble dval[1];
        if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), dval))
            *value = static_cast<ALfloat>(dval[0]);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alGetSource3f(ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
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
        ALdouble dvals[3];
        if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), dvals))
        {
            *value1 = static_cast<ALfloat>(dvals[0]);
            *value2 = static_cast<ALfloat>(dvals[1]);
            *value3 = static_cast<ALfloat>(dvals[2]);
        }
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alGetSourcefv(ALuint source, ALenum param, ALfloat *values)
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
        ALdouble dvals[MaxValues];
        if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), {dvals, count}))
        {
            for(ALuint i{0};i < count;i++)
                values[i] = static_cast<ALfloat>(dvals[i]);
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
        ALdouble dvals[3];
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


AL_API ALvoid AL_APIENTRY alGetSourcei(ALuint source, ALenum param, ALint *value)
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
        ALint ivals[3];
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
        ALint64SOFT i64vals[3];
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


AL_API ALvoid AL_APIENTRY alSourcePlay(ALuint source)
START_API_FUNC
{ alSourcePlayv(1, &source); }
END_API_FUNC

AL_API ALvoid AL_APIENTRY alSourcePlayv(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Playing %d sources", n);
    if UNLIKELY(n <= 0) return;

    al::vector<ALsource*> extra_sources;
    std::array<ALsource*,16> source_storage;
    ALsource **srchandles{source_storage.data()};
    if UNLIKELY(static_cast<ALuint>(n) > source_storage.size())
    {
        extra_sources.resize(n);
        srchandles = extra_sources.data();
    }

    std::lock_guard<std::mutex> _{context->mSourceLock};
    for(ALsizei i{0};i < n;i++)
    {
        srchandles[i] = LookupSource(context.get(), sources[i]);
        if(!srchandles[i])
            SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", sources[i]);
    }

    ALCdevice *device{context->mDevice.get()};
    BackendLockGuard __{*device->Backend};
    /* If the device is disconnected, go right to stopped. */
    if UNLIKELY(!device->Connected.load(std::memory_order_acquire))
    {
        /* TODO: Send state change event? */
        std::for_each(srchandles, srchandles+n,
            [](ALsource *source) -> void
            {
                source->OffsetType = AL_NONE;
                source->Offset = 0.0;
                source->state = AL_STOPPED;
            }
        );
        return;
    }

    /* Count the number of reusable voices. */
    auto voices_end = context->mVoices->begin() +
        context->mVoiceCount.load(std::memory_order_relaxed);
    auto free_voices = std::accumulate(context->mVoices->begin(), voices_end, ALsizei{0},
        [](const ALsizei count, const ALvoice &voice) noexcept -> ALsizei
        {
            if(voice.mPlayState.load(std::memory_order_acquire) == ALvoice::Stopped &&
                voice.mSourceID.load(std::memory_order_relaxed) == 0u)
                return count + 1;
            return count;
        }
    );
    if UNLIKELY(n > free_voices)
    {
        /* Increment the number of voices to handle the request. */
        const ALuint need_voices{static_cast<ALuint>(n) - free_voices};
        const size_t rem_voices{context->mVoices->size() -
            context->mVoiceCount.load(std::memory_order_relaxed)};

        if UNLIKELY(need_voices > rem_voices)
        {
            /* Allocate more voices to get enough. */
            const size_t alloc_count{need_voices - rem_voices};
            if UNLIKELY(context->mVoices->size() > std::numeric_limits<ALsizei>::max()-alloc_count)
                SETERR_RETURN(context, AL_OUT_OF_MEMORY,,
                    "Overflow increasing voice count to %zu + %zu", context->mVoices->size(),
                    alloc_count);

            const size_t newcount{context->mVoices->size() + alloc_count};
            context->allocVoices(newcount);
        }

        context->mVoiceCount.fetch_add(need_voices, std::memory_order_relaxed);
    }

    auto start_source = [&context,device](ALsource *source) -> void
    {
        /* Check that there is a queue containing at least one valid, non zero
         * length buffer.
         */
        ALbufferlistitem *BufferList{source->queue};
        while(BufferList && BufferList->mSampleLen == 0)
            BufferList = BufferList->mNext.load(std::memory_order_relaxed);

        /* If there's nothing to play, go right to stopped. */
        if UNLIKELY(!BufferList)
        {
            /* NOTE: A source without any playable buffers should not have an
             * ALvoice since it shouldn't be in a playing or paused state. So
             * there's no need to look up its voice and clear the source.
             */
            ALenum oldstate{GetSourceState(source, nullptr)};
            source->OffsetType = AL_NONE;
            source->Offset = 0.0;
            if(oldstate != AL_STOPPED)
            {
                source->state = AL_STOPPED;
                SendStateChangeEvent(context.get(), source->id, AL_STOPPED);
            }
            return;
        }

        ALvoice *voice{GetSourceVoice(source, context.get())};
        switch(GetSourceState(source, voice))
        {
        case AL_PLAYING:
            assert(voice != nullptr);
            /* A source that's already playing is restarted from the beginning. */
            voice->mCurrentBuffer.store(BufferList, std::memory_order_relaxed);
            voice->mPosition.store(0u, std::memory_order_relaxed);
            voice->mPositionFrac.store(0, std::memory_order_release);
            return;

        case AL_PAUSED:
            assert(voice != nullptr);
            /* A source that's paused simply resumes. */
            voice->mPlayState.store(ALvoice::Playing, std::memory_order_release);
            source->state = AL_PLAYING;
            SendStateChangeEvent(context.get(), source->id, AL_PLAYING);
            return;

        default:
            assert(voice == nullptr);
            break;
        }

        /* Look for an unused voice to play this source with. */
        auto voices_end = context->mVoices->begin() +
            context->mVoiceCount.load(std::memory_order_relaxed);
        voice = std::find_if(context->mVoices->begin(), voices_end,
            [](const ALvoice &voice) noexcept -> bool
            {
                return voice.mPlayState.load(std::memory_order_acquire) == ALvoice::Stopped &&
                    voice.mSourceID.load(std::memory_order_relaxed) == 0u;
            }
        );
        assert(voice != voices_end);
        auto vidx = static_cast<ALuint>(std::distance(context->mVoices->begin(), voice));
        voice->mPlayState.store(ALvoice::Stopped, std::memory_order_release);

        source->PropsClean.test_and_set(std::memory_order_acquire);
        UpdateSourceProps(source, voice, context.get());

        /* A source that's not playing or paused has any offset applied when it
         * starts playing.
         */
        if(source->Looping)
            voice->mLoopBuffer.store(source->queue, std::memory_order_relaxed);
        else
            voice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
        voice->mCurrentBuffer.store(BufferList, std::memory_order_relaxed);
        voice->mPosition.store(0u, std::memory_order_relaxed);
        voice->mPositionFrac.store(0, std::memory_order_relaxed);
        bool start_fading{false};
        if(ApplyOffset(source, voice) != AL_FALSE)
            start_fading = voice->mPosition.load(std::memory_order_relaxed) != 0 ||
                voice->mPositionFrac.load(std::memory_order_relaxed) != 0 ||
                voice->mCurrentBuffer.load(std::memory_order_relaxed) != BufferList;

        ALbuffer *buffer{BufferList->mBuffer};
        voice->mFrequency = buffer->Frequency;
        voice->mFmtChannels = buffer->mFmtChannels;
        voice->mNumChannels = ChannelsFromFmt(buffer->mFmtChannels);
        voice->mSampleSize  = BytesFromFmt(buffer->mFmtType);

        /* Clear the stepping value so the mixer knows not to mix this until
         * the update gets applied.
         */
        voice->mStep = 0;

        voice->mFlags = start_fading ? VOICE_IS_FADING : 0;
        if(source->SourceType == AL_STATIC) voice->mFlags |= VOICE_IS_STATIC;

        /* Don't need to set the VOICE_IS_AMBISONIC flag if the device is
         * mixing in first order. No HF scaling is necessary to mix it.
         */
        if((voice->mFmtChannels == FmtBFormat2D || voice->mFmtChannels == FmtBFormat3D) &&
           device->mAmbiOrder > 1)
        {
            const int *OrderFromChan;
            if(voice->mFmtChannels == FmtBFormat2D)
            {
                static constexpr int Order2DFromChan[MAX_AMBI2D_CHANNELS]{
                    0, 1,1, 2,2, 3,3
                };
                OrderFromChan = Order2DFromChan;
            }
            else
            {
                static constexpr int Order3DFromChan[MAX_AMBI_CHANNELS]{
                    0, 1,1,1, 2,2,2,2,2, 3,3,3,3,3,3,3,
                };
                OrderFromChan = Order3DFromChan;
            }

            BandSplitter splitter{400.0f / static_cast<ALfloat>(device->Frequency)};

            const auto scales = BFormatDec::GetHFOrderScales(1, device->mAmbiOrder);
            auto init_ambi = [scales,&OrderFromChan,&splitter](ALvoice::ChannelData &chandata) -> void
            {
                chandata.mPrevSamples.fill(0.0f);
                chandata.mAmbiScale = scales[*(OrderFromChan++)];
                chandata.mAmbiSplitter = splitter;
            };
            std::for_each(voice->mChans.begin(), voice->mChans.begin()+voice->mNumChannels,
                init_ambi);

            voice->mFlags |= VOICE_IS_AMBISONIC;
        }
        else
        {
            /* Clear previous samples. */
            auto clear_prevs = [](ALvoice::ChannelData &chandata) -> void
            { chandata.mPrevSamples.fill(0.0f); };
            std::for_each(voice->mChans.begin(), voice->mChans.begin()+voice->mNumChannels,
                clear_prevs);
        }

        auto clear_params = [device](ALvoice::ChannelData &chandata) -> void
        {
            chandata.mDryParams = DirectParams{};
            std::fill_n(chandata.mWetParams.begin(), device->NumAuxSends, SendParams{});
        };
        std::for_each(voice->mChans.begin(), voice->mChans.begin()+voice->mNumChannels,
            clear_params);

        if(device->AvgSpeakerDist > 0.0f)
        {
            const ALfloat w1{SPEEDOFSOUNDMETRESPERSEC /
                (device->AvgSpeakerDist * device->Frequency)};
            auto init_nfc = [w1](ALvoice::ChannelData &chandata) -> void
            { chandata.mDryParams.NFCtrlFilter.init(w1); };
            std::for_each(voice->mChans.begin(), voice->mChans.begin()+voice->mNumChannels,
                init_nfc);
        }

        voice->mSourceID.store(source->id, std::memory_order_relaxed);
        voice->mPlayState.store(ALvoice::Playing, std::memory_order_release);
        source->state = AL_PLAYING;
        source->VoiceIdx = vidx;

        SendStateChangeEvent(context.get(), source->id, AL_PLAYING);
    };
    std::for_each(srchandles, srchandles+n, start_source);
}
END_API_FUNC


AL_API ALvoid AL_APIENTRY alSourcePause(ALuint source)
START_API_FUNC
{ alSourcePausev(1, &source); }
END_API_FUNC

AL_API ALvoid AL_APIENTRY alSourcePausev(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Pausing %d sources", n);
    if UNLIKELY(n <= 0) return;

    al::vector<ALsource*> extra_sources;
    std::array<ALsource*,16> source_storage;
    ALsource **srchandles{source_storage.data()};
    if UNLIKELY(static_cast<ALuint>(n) > source_storage.size())
    {
        extra_sources.resize(n);
        srchandles = extra_sources.data();
    }

    std::lock_guard<std::mutex> _{context->mSourceLock};
    for(ALsizei i{0};i < n;i++)
    {
        srchandles[i] = LookupSource(context.get(), sources[i]);
        if(!srchandles[i])
            SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", sources[i]);
    }

    ALCdevice *device{context->mDevice.get()};
    BackendLockGuard __{*device->Backend};
    auto pause_source = [&context](ALsource *source) -> void
    {
        ALvoice *voice{GetSourceVoice(source, context.get())};
        if(voice)
        {
            std::atomic_thread_fence(std::memory_order_release);
            ALvoice::State oldvstate{ALvoice::Playing};
            voice->mPlayState.compare_exchange_strong(oldvstate, ALvoice::Stopping,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }
        if(GetSourceState(source, voice) == AL_PLAYING)
        {
            source->state = AL_PAUSED;
            SendStateChangeEvent(context.get(), source->id, AL_PAUSED);
        }
    };
    std::for_each(srchandles, srchandles+n, pause_source);
}
END_API_FUNC


AL_API ALvoid AL_APIENTRY alSourceStop(ALuint source)
START_API_FUNC
{ alSourceStopv(1, &source); }
END_API_FUNC

AL_API ALvoid AL_APIENTRY alSourceStopv(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Stopping %d sources", n);
    if UNLIKELY(n <= 0) return;

    al::vector<ALsource*> extra_sources;
    std::array<ALsource*,16> source_storage;
    ALsource **srchandles{source_storage.data()};
    if UNLIKELY(static_cast<ALuint>(n) > source_storage.size())
    {
        extra_sources.resize(n);
        srchandles = extra_sources.data();
    }

    std::lock_guard<std::mutex> _{context->mSourceLock};
    for(ALsizei i{0};i < n;i++)
    {
        srchandles[i] = LookupSource(context.get(), sources[i]);
        if(!srchandles[i])
            SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", sources[i]);
    }

    ALCdevice *device{context->mDevice.get()};
    BackendLockGuard __{*device->Backend};
    auto stop_source = [&context](ALsource *source) -> void
    {
        ALvoice *voice{GetSourceVoice(source, context.get())};
        if(voice != nullptr)
        {
            voice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
            voice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
            voice->mSourceID.store(0u, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            ALvoice::State oldvstate{ALvoice::Playing};
            voice->mPlayState.compare_exchange_strong(oldvstate, ALvoice::Stopping,
                std::memory_order_acq_rel, std::memory_order_acquire);
            voice = nullptr;
        }
        ALenum oldstate{GetSourceState(source, voice)};
        if(oldstate != AL_INITIAL && oldstate != AL_STOPPED)
        {
            source->state = AL_STOPPED;
            SendStateChangeEvent(context.get(), source->id, AL_STOPPED);
        }
        source->OffsetType = AL_NONE;
        source->Offset = 0.0;
    };
    std::for_each(srchandles, srchandles+n, stop_source);
}
END_API_FUNC


AL_API ALvoid AL_APIENTRY alSourceRewind(ALuint source)
START_API_FUNC
{ alSourceRewindv(1, &source); }
END_API_FUNC

AL_API ALvoid AL_APIENTRY alSourceRewindv(ALsizei n, const ALuint *sources)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Rewinding %d sources", n);
    if UNLIKELY(n <= 0) return;

    al::vector<ALsource*> extra_sources;
    std::array<ALsource*,16> source_storage;
    ALsource **srchandles{source_storage.data()};
    if UNLIKELY(static_cast<ALuint>(n) > source_storage.size())
    {
        extra_sources.resize(n);
        srchandles = extra_sources.data();
    }

    std::lock_guard<std::mutex> _{context->mSourceLock};
    for(ALsizei i{0};i < n;i++)
    {
        srchandles[i] = LookupSource(context.get(), sources[i]);
        if(!srchandles[i])
            SETERR_RETURN(context, AL_INVALID_NAME,, "Invalid source ID %u", sources[i]);
    }

    ALCdevice *device{context->mDevice.get()};
    BackendLockGuard __{*device->Backend};
    auto rewind_source = [&context](ALsource *source) -> void
    {
        ALvoice *voice{GetSourceVoice(source, context.get())};
        if(voice != nullptr)
        {
            voice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
            voice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
            voice->mSourceID.store(0u, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            ALvoice::State oldvstate{ALvoice::Playing};
            voice->mPlayState.compare_exchange_strong(oldvstate, ALvoice::Stopping,
                std::memory_order_acq_rel, std::memory_order_acquire);
            voice = nullptr;
        }
        if(GetSourceState(source, voice) != AL_INITIAL)
        {
            source->state = AL_INITIAL;
            SendStateChangeEvent(context.get(), source->id, AL_INITIAL);
        }
        source->OffsetType = AL_NONE;
        source->Offset = 0.0;
    };
    std::for_each(srchandles, srchandles+n, rewind_source);
}
END_API_FUNC


AL_API ALvoid AL_APIENTRY alSourceQueueBuffers(ALuint src, ALsizei nb, const ALuint *buffers)
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
    ALCdevice *device{context->mDevice.get()};
    ALbuffer *BufferFmt{nullptr};
    ALbufferlistitem *BufferList{source->queue};
    while(BufferList && !BufferFmt)
    {
        BufferFmt = BufferList->mBuffer;
        BufferList = BufferList->mNext.load(std::memory_order_relaxed);
    }

    std::unique_lock<std::mutex> buflock{device->BufferLock};
    ALbufferlistitem *BufferListStart{nullptr};
    BufferList = nullptr;
    for(ALsizei i{0};i < nb;i++)
    {
        ALbuffer *buffer{nullptr};
        if(buffers[i] && (buffer=LookupBuffer(device, buffers[i])) == nullptr)
        {
            context->setError(AL_INVALID_NAME, "Queueing invalid buffer ID %u", buffers[i]);
            goto buffer_error;
        }

        if(!BufferListStart)
        {
            BufferListStart = new ALbufferlistitem{};
            BufferList = BufferListStart;
        }
        else
        {
            auto item = new ALbufferlistitem{};
            BufferList->mNext.store(item, std::memory_order_relaxed);
            BufferList = item;
        }
        BufferList->mNext.store(nullptr, std::memory_order_relaxed);
        BufferList->mSampleLen = buffer ? buffer->SampleLen : 0;
        BufferList->mBuffer = buffer;
        if(!buffer) continue;

        IncrementRef(buffer->ref);

        if(buffer->MappedAccess != 0 && !(buffer->MappedAccess&AL_MAP_PERSISTENT_BIT_SOFT))
        {
            context->setError(AL_INVALID_OPERATION, "Queueing non-persistently mapped buffer %u",
                buffer->id);
            goto buffer_error;
        }

        if(BufferFmt == nullptr)
            BufferFmt = buffer;
        else if(BufferFmt->Frequency != buffer->Frequency ||
                BufferFmt->mFmtChannels != buffer->mFmtChannels ||
                BufferFmt->OriginalType != buffer->OriginalType)
        {
            context->setError(AL_INVALID_OPERATION, "Queueing buffer with mismatched format");

        buffer_error:
            /* A buffer failed (invalid ID or format), so unlock and release
             * each buffer we had. */
            while(BufferListStart)
            {
                std::unique_ptr<ALbufferlistitem> head{BufferListStart};
                BufferListStart = head->mNext.load(std::memory_order_relaxed);
                if(ALbuffer *buffer{head->mBuffer}) DecrementRef(buffer->ref);
            }
            return;
        }
    }
    /* All buffers good. */
    buflock.unlock();

    /* Source is now streaming */
    source->SourceType = AL_STREAMING;

    if(!(BufferList=source->queue))
        source->queue = BufferListStart;
    else
    {
        ALbufferlistitem *next;
        while((next=BufferList->mNext.load(std::memory_order_relaxed)) != nullptr)
            BufferList = next;
        BufferList->mNext.store(BufferListStart, std::memory_order_release);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alSourceUnqueueBuffers(ALuint src, ALsizei nb, ALuint *buffers)
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

    if UNLIKELY(source->Looping)
        SETERR_RETURN(context, AL_INVALID_VALUE,, "Unqueueing from looping source %u", src);
    if UNLIKELY(source->SourceType != AL_STREAMING)
        SETERR_RETURN(context, AL_INVALID_VALUE,, "Unqueueing from a non-streaming source %u",
            src);

    /* Make sure enough buffers have been processed to unqueue. */
    ALbufferlistitem *BufferList{source->queue};
    ALvoice *voice{GetSourceVoice(source, context.get())};
    ALbufferlistitem *Current{nullptr};
    if(voice)
        Current = voice->mCurrentBuffer.load(std::memory_order_relaxed);
    else if(source->state == AL_INITIAL)
        Current = BufferList;
    if UNLIKELY(BufferList == Current)
        SETERR_RETURN(context, AL_INVALID_VALUE,, "Unqueueing pending buffers");

    ALuint i{1u};
    while(i < static_cast<ALuint>(nb))
    {
        /* If the next bufferlist to check is NULL or is the current one, it's
         * trying to unqueue pending buffers.
         */
        ALbufferlistitem *next{BufferList->mNext.load(std::memory_order_relaxed)};
        if UNLIKELY(!next || next == Current)
            SETERR_RETURN(context, AL_INVALID_VALUE,, "Unqueueing pending buffers");
        BufferList = next;

        ++i;
    }

    while(nb > 0)
    {
        std::unique_ptr<ALbufferlistitem> head{source->queue};
        source->queue = head->mNext.load(std::memory_order_relaxed);

        if(ALbuffer *buffer{head->mBuffer})
        {
            *(buffers++) = buffer->id;
            DecrementRef(buffer->ref);
        }
        else
            *(buffers++) = 0;
        --nb;
    }
}
END_API_FUNC


ALsource::ALsource(ALsizei num_sends)
{
    InnerAngle = 360.0f;
    OuterAngle = 360.0f;
    Pitch = 1.0f;
    Position[0] = 0.0f;
    Position[1] = 0.0f;
    Position[2] = 0.0f;
    Velocity[0] = 0.0f;
    Velocity[1] = 0.0f;
    Velocity[2] = 0.0f;
    Direction[0] = 0.0f;
    Direction[1] = 0.0f;
    Direction[2] = 0.0f;
    OrientAt[0] =  0.0f;
    OrientAt[1] =  0.0f;
    OrientAt[2] = -1.0f;
    OrientUp[0] =  0.0f;
    OrientUp[1] =  1.0f;
    OrientUp[2] =  0.0f;
    RefDistance = 1.0f;
    MaxDistance = std::numeric_limits<float>::max();
    RolloffFactor = 1.0f;
    Gain = 1.0f;
    MinGain = 0.0f;
    MaxGain = 1.0f;
    OuterGain = 0.0f;
    OuterGainHF = 1.0f;

    DryGainHFAuto = AL_TRUE;
    WetGainAuto = AL_TRUE;
    WetGainHFAuto = AL_TRUE;
    AirAbsorptionFactor = 0.0f;
    RoomRolloffFactor = 0.0f;
    DopplerFactor = 1.0f;
    HeadRelative = AL_FALSE;
    Looping = AL_FALSE;
    mDistanceModel = DistanceModel::Default;
    mResampler = ResamplerDefault;
    DirectChannels = AL_FALSE;
    mSpatialize = SpatializeAuto;

    StereoPan[0] = Deg2Rad( 30.0f);
    StereoPan[1] = Deg2Rad(-30.0f);

    Radius = 0.0f;

    Direct.Gain = 1.0f;
    Direct.GainHF = 1.0f;
    Direct.HFReference = LOWPASSFREQREF;
    Direct.GainLF = 1.0f;
    Direct.LFReference = HIGHPASSFREQREF;
    Send.resize(num_sends);
    for(auto &send : Send)
    {
        send.Slot = nullptr;
        send.Gain = 1.0f;
        send.GainHF = 1.0f;
        send.HFReference = LOWPASSFREQREF;
        send.GainLF = 1.0f;
        send.LFReference = HIGHPASSFREQREF;
    }

    PropsClean.test_and_set(std::memory_order_relaxed);
}

ALsource::~ALsource()
{
    ALbufferlistitem *BufferList{queue};
    while(BufferList != nullptr)
    {
        std::unique_ptr<ALbufferlistitem> head{BufferList};
        BufferList = head->mNext.load(std::memory_order_relaxed);
        if(ALbuffer *buffer{head->mBuffer}) DecrementRef(buffer->ref);
    }
    queue = nullptr;

    std::for_each(Send.begin(), Send.end(),
        [](ALsource::SendData &send) -> void
        {
            if(send.Slot)
                DecrementRef(send.Slot->ref);
            send.Slot = nullptr;
        }
    );
}

void UpdateAllSourceProps(ALCcontext *context)
{
    auto voices_end = context->mVoices->begin() +
        context->mVoiceCount.load(std::memory_order_relaxed);
    std::for_each(context->mVoices->begin(), voices_end,
        [context](ALvoice &voice) -> void
        {
            ALuint sid{voice.mSourceID.load(std::memory_order_acquire)};
            ALsource *source = sid ? LookupSource(context, sid) : nullptr;
            if(source && !source->PropsClean.test_and_set(std::memory_order_acq_rel))
                UpdateSourceProps(source, &voice, context);
        }
    );
}

SourceSubList::~SourceSubList()
{
    uint64_t usemask{~FreeMask};
    while(usemask)
    {
        ALsizei idx{CTZ64(usemask)};
        al::destroy_at(Sources+idx);
        usemask &= ~(1_u64 << idx);
    }
    FreeMask = ~usemask;
    al_free(Sources);
    Sources = nullptr;
}
