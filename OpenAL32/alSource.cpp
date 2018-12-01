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

#include <stdlib.h>
#include <limits.h>
#include <float.h>

#include <cmath>
#include <thread>
#include <limits>
#include <algorithm>

#include "AL/al.h"
#include "AL/alc.h"

#include "alMain.h"
#include "alcontext.h"
#include "alError.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "ringbuffer.h"

#include "backends/base.h"

#include "threads.h"
#include "almalloc.h"


namespace {

inline ALvoice *GetSourceVoice(ALsource *source, ALCcontext *context)
{
    ALint idx{source->VoiceIdx};
    if(idx >= 0 && idx < context->VoiceCount.load(std::memory_order_relaxed))
    {
        ALuint sid{source->id};
        ALvoice *voice{context->Voices[idx]};
        if(voice->SourceID.load(std::memory_order_acquire) == sid)
            return voice;
    }
    source->VoiceIdx = -1;
    return nullptr;
}

void UpdateSourceProps(ALsource *source, ALvoice *voice, ALCcontext *context)
{
    /* Get an unused property container, or allocate a new one as needed. */
    ALvoiceProps *props{context->FreeVoiceProps.load(std::memory_order_acquire)};
    if(!props)
        props = new ALvoiceProps{};
    else
    {
        ALvoiceProps *next;
        do {
            next = props->next.load(std::memory_order_relaxed);
        } while(context->FreeVoiceProps.compare_exchange_weak(props, next,
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
    std::copy(std::begin(source->Position), std::end(source->Position), props->Position);
    std::copy(std::begin(source->Velocity), std::end(source->Velocity), props->Velocity);
    std::copy(std::begin(source->Direction), std::end(source->Direction), props->Direction);
    std::copy(std::begin(source->Orientation[0]), std::end(source->Orientation[0]), props->Orientation[0]);
    std::copy(std::begin(source->Orientation[1]), std::end(source->Orientation[1]), props->Orientation[1]);
    props->HeadRelative = source->HeadRelative;
    props->mDistanceModel = source->mDistanceModel;
    props->Resampler = source->Resampler;
    props->DirectChannels = source->DirectChannels;
    props->SpatializeMode = source->Spatialize;

    props->DryGainHFAuto = source->DryGainHFAuto;
    props->WetGainAuto = source->WetGainAuto;
    props->WetGainHFAuto = source->WetGainHFAuto;
    props->OuterGainHF = source->OuterGainHF;

    props->AirAbsorptionFactor = source->AirAbsorptionFactor;
    props->RoomRolloffFactor = source->RoomRolloffFactor;
    props->DopplerFactor = source->DopplerFactor;

    std::copy(std::begin(source->StereoPan), std::end(source->StereoPan), props->StereoPan);

    props->Radius = source->Radius;

    props->Direct.Gain = source->Direct.Gain;
    props->Direct.GainHF = source->Direct.GainHF;
    props->Direct.HFReference = source->Direct.HFReference;
    props->Direct.GainLF = source->Direct.GainLF;
    props->Direct.LFReference = source->Direct.LFReference;

    for(size_t i{0u};i < source->Send.size();i++)
    {
        props->Send[i].Slot = source->Send[i].Slot;
        props->Send[i].Gain = source->Send[i].Gain;
        props->Send[i].GainHF = source->Send[i].GainHF;
        props->Send[i].HFReference = source->Send[i].HFReference;
        props->Send[i].GainLF = source->Send[i].GainLF;
        props->Send[i].LFReference = source->Send[i].LFReference;
    }

    /* Set the new container for updating internal parameters. */
    props = voice->Update.exchange(props, std::memory_order_acq_rel);
    if(props)
    {
        /* If there was an unused update container, put it back in the
         * freelist.
         */
        AtomicReplaceHead(context->FreeVoiceProps, props);
    }
}


/* GetSourceSampleOffset
 *
 * Gets the current read offset for the given Source, in 32.32 fixed-point
 * samples. The offset is relative to the start of the queue (not the start of
 * the current buffer).
 */
ALint64 GetSourceSampleOffset(ALsource *Source, ALCcontext *context, std::chrono::nanoseconds *clocktime)
{
    ALCdevice *device{context->Device};
    const ALbufferlistitem *Current;
    ALuint64 readPos;
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
            Current = voice->current_buffer.load(std::memory_order_relaxed);

            readPos  = (ALuint64)voice->position.load(std::memory_order_relaxed) << 32;
            readPos |= (ALuint64)voice->position_fraction.load(std::memory_order_relaxed) <<
                       (32-FRACTIONBITS);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != device->MixCount.load(std::memory_order_relaxed));

    if(voice)
    {
        const ALbufferlistitem *BufferList{Source->queue};
        while(BufferList && BufferList != Current)
        {
            readPos += (ALuint64)BufferList->max_samples << 32;
            BufferList = BufferList->next.load(std::memory_order_relaxed);
        }
        readPos = minu64(readPos, U64(0x7fffffffffffffff));
    }

    return (ALint64)readPos;
}

/* GetSourceSecOffset
 *
 * Gets the current read offset for the given Source, in seconds. The offset is
 * relative to the start of the queue (not the start of the current buffer).
 */
ALdouble GetSourceSecOffset(ALsource *Source, ALCcontext *context, std::chrono::nanoseconds *clocktime)
{
    ALCdevice *device{context->Device};
    const ALbufferlistitem *Current;
    ALuint64 readPos;
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
            Current = voice->current_buffer.load(std::memory_order_relaxed);

            readPos  = (ALuint64)voice->position.load(std::memory_order_relaxed) << FRACTIONBITS;
            readPos |= voice->position_fraction.load(std::memory_order_relaxed);
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
            for(ALsizei i{0};!BufferFmt && i < BufferList->num_buffers;++i)
                BufferFmt = BufferList->buffers[i];
            readPos += (ALuint64)BufferList->max_samples << FRACTIONBITS;
            BufferList = BufferList->next.load(std::memory_order_relaxed);
        }

        while(BufferList && !BufferFmt)
        {
            for(ALsizei i{0};!BufferFmt && i < BufferList->num_buffers;++i)
                BufferFmt = BufferList->buffers[i];
            BufferList = BufferList->next.load(std::memory_order_relaxed);
        }
        assert(BufferFmt != nullptr);

        offset = (ALdouble)readPos / (ALdouble)FRACTIONONE /
                 (ALdouble)BufferFmt->Frequency;
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
    ALCdevice *device{context->Device};
    const ALbufferlistitem *Current;
    ALuint readPos;
    ALsizei readPosFrac;
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
            Current = voice->current_buffer.load(std::memory_order_relaxed);

            readPos = voice->position.load(std::memory_order_relaxed);
            readPosFrac = voice->position_fraction.load(std::memory_order_relaxed);
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
            for(ALsizei i{0};!BufferFmt && i < BufferList->num_buffers;++i)
                BufferFmt = BufferList->buffers[i];

            readFin |= (BufferList == Current);
            totalBufferLen += BufferList->max_samples;
            if(!readFin) readPos += BufferList->max_samples;

            BufferList = BufferList->next.load(std::memory_order_relaxed);
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
                offset = (readPos + (ALdouble)readPosFrac/FRACTIONONE) / BufferFmt->Frequency;
                break;

            case AL_SAMPLE_OFFSET:
                offset = readPos + (ALdouble)readPosFrac/FRACTIONONE;
                break;

            case AL_BYTE_OFFSET:
                if(BufferFmt->OriginalType == UserFmtIMA4)
                {
                    ALsizei align = (BufferFmt->OriginalAlign-1)/2 + 4;
                    ALuint BlockSize = align * ChannelsFromFmt(BufferFmt->FmtChannels);
                    ALuint FrameBlockSize = BufferFmt->OriginalAlign;

                    /* Round down to nearest ADPCM block */
                    offset = (ALdouble)(readPos / FrameBlockSize * BlockSize);
                }
                else if(BufferFmt->OriginalType == UserFmtMSADPCM)
                {
                    ALsizei align = (BufferFmt->OriginalAlign-2)/2 + 7;
                    ALuint BlockSize = align * ChannelsFromFmt(BufferFmt->FmtChannels);
                    ALuint FrameBlockSize = BufferFmt->OriginalAlign;

                    /* Round down to nearest ADPCM block */
                    offset = (ALdouble)(readPos / FrameBlockSize * BlockSize);
                }
                else
                {
                    ALuint FrameSize = FrameSizeFromFmt(BufferFmt->FmtChannels,
                                                        BufferFmt->FmtType);
                    offset = (ALdouble)(readPos * FrameSize);
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
ALboolean GetSampleOffset(ALsource *Source, ALuint *offset, ALsizei *frac)
{
    const ALbuffer *BufferFmt{nullptr};
    const ALbufferlistitem *BufferList;
 
    /* Find the first valid Buffer in the Queue */
    BufferList = Source->queue;
    while(BufferList)
    {
        for(ALsizei i{0};i < BufferList->num_buffers && !BufferFmt;i++)
            BufferFmt = BufferList->buffers[i];
        if(BufferFmt) break;
        BufferList = BufferList->next.load(std::memory_order_relaxed);
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
        *offset = (ALuint)Source->Offset;
        if(BufferFmt->OriginalType == UserFmtIMA4)
        {
            ALsizei align = (BufferFmt->OriginalAlign-1)/2 + 4;
            *offset /= align * ChannelsFromFmt(BufferFmt->FmtChannels);
            *offset *= BufferFmt->OriginalAlign;
        }
        else if(BufferFmt->OriginalType == UserFmtMSADPCM)
        {
            ALsizei align = (BufferFmt->OriginalAlign-2)/2 + 7;
            *offset /= align * ChannelsFromFmt(BufferFmt->FmtChannels);
            *offset *= BufferFmt->OriginalAlign;
        }
        else
            *offset /= FrameSizeFromFmt(BufferFmt->FmtChannels, BufferFmt->FmtType);
        *frac = 0;
        break;

    case AL_SAMPLE_OFFSET:
        dblfrac = modf(Source->Offset, &dbloff);
        *offset = (ALuint)mind(dbloff, std::numeric_limits<unsigned int>::max());
        *frac = (ALsizei)mind(dblfrac*FRACTIONONE, FRACTIONONE-1.0);
        break;

    case AL_SEC_OFFSET:
        dblfrac = modf(Source->Offset*BufferFmt->Frequency, &dbloff);
        *offset = (ALuint)mind(dbloff, std::numeric_limits<unsigned int>::max());
        *frac = (ALsizei)mind(dblfrac*FRACTIONONE, FRACTIONONE-1.0);
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
    ALsizei frac{0};
    if(!GetSampleOffset(Source, &offset, &frac))
        return AL_FALSE;

    ALuint totalBufferLen{0u};
    ALbufferlistitem *BufferList{Source->queue};
    while(BufferList && totalBufferLen <= offset)
    {
        if((ALuint)BufferList->max_samples > offset-totalBufferLen)
        {
            /* Offset is in this buffer */
            voice->position.store(offset - totalBufferLen, std::memory_order_relaxed);
            voice->position_fraction.store(frac, std::memory_order_relaxed);
            voice->current_buffer.store(BufferList, std::memory_order_release);
            return AL_TRUE;
        }
        totalBufferLen += BufferList->max_samples;

        BufferList = BufferList->next.load(std::memory_order_relaxed);
    }

    /* Offset is out of range of the queue */
    return AL_FALSE;
}


ALsource *AllocSource(ALCcontext *context)
{
    ALCdevice *device{context->Device};
    std::lock_guard<std::mutex> _{context->SourceLock};
    if(context->NumSources >= device->SourcesMax)
    {
        alSetError(context, AL_OUT_OF_MEMORY, "Exceeding %u source limit", device->SourcesMax);
        return nullptr;
    }
    auto sublist = std::find_if(context->SourceList.begin(), context->SourceList.end(),
        [](const SourceSubList &entry) -> bool
        { return entry.FreeMask != 0; }
    );
    ALsizei lidx = std::distance(context->SourceList.begin(), sublist);
    ALsource *source;
    ALsizei slidx;
    if(LIKELY(sublist != context->SourceList.end()))
    {
        slidx = CTZ64(sublist->FreeMask);
        source = sublist->Sources + slidx;
    }
    else
    {
        /* Don't allocate so many list entries that the 32-bit ID could
         * overflow...
         */
        if(UNLIKELY(context->SourceList.size() >= 1<<25))
        {
            alSetError(context, AL_OUT_OF_MEMORY, "Too many sources allocated");
            return nullptr;
        }
        context->SourceList.emplace_back();
        sublist = context->SourceList.end() - 1;

        sublist->FreeMask = ~U64(0);
        sublist->Sources = static_cast<ALsource*>(al_calloc(16, sizeof(ALsource)*64));
        if(UNLIKELY(!sublist->Sources))
        {
            context->SourceList.pop_back();
            alSetError(context, AL_OUT_OF_MEMORY, "Failed to allocate source batch");
            return nullptr;
        }

        slidx = 0;
        source = sublist->Sources + slidx;
    }

    source = new (source) ALsource{device->NumAuxSends};

    /* Add 1 to avoid source ID 0. */
    source->id = ((lidx<<6) | slidx) + 1;

    context->NumSources += 1;
    sublist->FreeMask &= ~(U64(1)<<slidx);

    return source;
}

void FreeSource(ALCcontext *context, ALsource *source)
{
    ALuint id = source->id - 1;
    ALsizei lidx = id >> 6;
    ALsizei slidx = id & 0x3f;

    ALCdevice *device{context->Device};
    ALCdevice_Lock(device);
    ALvoice *voice{GetSourceVoice(source, context)};
    if(voice)
    {
        voice->SourceID.store(0u, std::memory_order_relaxed);
        voice->Playing.store(false, std::memory_order_release);
    }
    ALCdevice_Unlock(device);

    source->~ALsource();

    context->SourceList[lidx].FreeMask |= U64(1) << slidx;
    context->NumSources--;
}


inline ALsource *LookupSource(ALCcontext *context, ALuint id) noexcept
{
    ALuint lidx = (id-1) >> 6;
    ALsizei slidx = (id-1) & 0x3f;

    if(UNLIKELY(lidx >= context->SourceList.size()))
        return nullptr;
    SourceSubList &sublist{context->SourceList[lidx]};
    if(UNLIKELY(sublist.FreeMask & (U64(1)<<slidx)))
        return nullptr;
    return sublist.Sources + slidx;
}

inline ALbuffer *LookupBuffer(ALCdevice *device, ALuint id) noexcept
{
    ALuint lidx = (id-1) >> 6;
    ALsizei slidx = (id-1) & 0x3f;

    if(UNLIKELY(lidx >= device->BufferList.size()))
        return nullptr;
    BufferSubList &sublist = device->BufferList[lidx];
    if(UNLIKELY(sublist.FreeMask & (U64(1)<<slidx)))
        return nullptr;
    return sublist.Buffers + slidx;
}

inline ALfilter *LookupFilter(ALCdevice *device, ALuint id) noexcept
{
    ALuint lidx = (id-1) >> 6;
    ALsizei slidx = (id-1) & 0x3f;

    if(UNLIKELY(lidx >= device->FilterList.size()))
        return nullptr;
    FilterSubList &sublist = device->FilterList[lidx];
    if(UNLIKELY(sublist.FreeMask & (U64(1)<<slidx)))
        return nullptr;
    return sublist.Filters + slidx;
}

inline ALeffectslot *LookupEffectSlot(ALCcontext *context, ALuint id) noexcept
{
    --id;
    if(UNLIKELY(id >= context->EffectSlotList.size()))
        return nullptr;
    return context->EffectSlotList[id].get();
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
    return !context->DeferUpdates.load(std::memory_order_acquire) &&
           IsPlayingOrPaused(source);
}


/** Can only be called while the mixer is locked! */
void SendStateChangeEvent(ALCcontext *context, ALuint id, ALenum state)
{
    AsyncEvent evt = ASYNC_EVENT(EventType_SourceStateChange);
    ALbitfieldSOFT enabledevt;

    enabledevt = context->EnabledEvts.load(std::memory_order_acquire);
    if(!(enabledevt&EventType_SourceStateChange)) return;

    evt.u.user.type = AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT;
    evt.u.user.id = id;
    evt.u.user.param = state;
    snprintf(evt.u.user.msg, sizeof(evt.u.user.msg), "Source ID %u state changed to %s", id,
        (state==AL_INITIAL) ? "AL_INITIAL" :
        (state==AL_PLAYING) ? "AL_PLAYING" :
        (state==AL_PAUSED) ? "AL_PAUSED" :
        (state==AL_STOPPED) ? "AL_STOPPED" : "<unknown>"
    );
    /* The mixer may have queued a state change that's not yet been processed,
     * and we don't want state change messages to occur out of order, so send
     * it through the async queue to ensure proper ordering.
     */
    if(ll_ringbuffer_write(context->AsyncEvents, &evt, 1) == 1)
        context->EventSem.post();
}


ALint FloatValsByProp(ALenum prop)
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
ALint DoubleValsByProp(ALenum prop)
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

ALint IntValsByProp(ALenum prop)
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
        case AL_BUFFER:
        case AL_SOURCE_STATE:
        case AL_BUFFERS_QUEUED:
        case AL_BUFFERS_PROCESSED:
        case AL_SOURCE_TYPE:
        case AL_DIRECT_FILTER:
        case AL_SOURCE_RADIUS:
        case AL_SOURCE_RESAMPLER_SOFT:
        case AL_SOURCE_SPATIALIZE_SOFT:
            return 1;

        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
        case AL_AUXILIARY_SEND_FILTER:
            return 3;

        case AL_ORIENTATION:
            return 6;

        case AL_SAMPLE_OFFSET_LATENCY_SOFT:
        case AL_SAMPLE_OFFSET_CLOCK_SOFT:
            break; /* i64 only */
        case AL_SEC_OFFSET_LATENCY_SOFT:
        case AL_SEC_OFFSET_CLOCK_SOFT:
            break; /* Double only */
        case AL_STEREO_ANGLES:
            break; /* Float/double only */
    }
    return 0;
}
ALint Int64ValsByProp(ALenum prop)
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
        case AL_BUFFER:
        case AL_SOURCE_STATE:
        case AL_BUFFERS_QUEUED:
        case AL_BUFFERS_PROCESSED:
        case AL_SOURCE_TYPE:
        case AL_DIRECT_FILTER:
        case AL_SOURCE_RADIUS:
        case AL_SOURCE_RESAMPLER_SOFT:
        case AL_SOURCE_SPATIALIZE_SOFT:
            return 1;

        case AL_SAMPLE_OFFSET_LATENCY_SOFT:
        case AL_SAMPLE_OFFSET_CLOCK_SOFT:
            return 2;

        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
        case AL_AUXILIARY_SEND_FILTER:
            return 3;

        case AL_ORIENTATION:
            return 6;

        case AL_SEC_OFFSET_LATENCY_SOFT:
        case AL_SEC_OFFSET_CLOCK_SOFT:
            break; /* Double only */
        case AL_STEREO_ANGLES:
            break; /* Float/double only */
    }
    return 0;
}


ALboolean SetSourcefv(ALsource *Source, ALCcontext *Context, SourceProp prop, const ALfloat *values);
ALboolean SetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, const ALint *values);
ALboolean SetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, const ALint64SOFT *values);

#define CHECKVAL(x) do {                                                      \
    if(!(x))                                                                  \
    {                                                                         \
        alSetError(Context, AL_INVALID_VALUE, "Value out of range");          \
        return AL_FALSE;                                                      \
    }                                                                         \
} while(0)

#define DO_UPDATEPROPS() do {                                                 \
    ALvoice *voice;                                                           \
    if(SourceShouldUpdate(Source, Context) &&                                 \
       (voice=GetSourceVoice(Source, Context)) != nullptr)                       \
        UpdateSourceProps(Source, voice, Context);                            \
    else                                                                      \
        Source->PropsClean.clear(std::memory_order_release);                  \
} while(0)

ALboolean SetSourcefv(ALsource *Source, ALCcontext *Context, SourceProp prop, const ALfloat *values)
{
    ALint ival;

    switch(prop)
    {
        case AL_SEC_OFFSET_LATENCY_SOFT:
        case AL_SEC_OFFSET_CLOCK_SOFT:
            /* Query only */
            SETERR_RETURN(Context, AL_INVALID_OPERATION, AL_FALSE,
                          "Setting read-only source property 0x%04x", prop);

        case AL_PITCH:
            CHECKVAL(*values >= 0.0f);

            Source->Pitch = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_CONE_INNER_ANGLE:
            CHECKVAL(*values >= 0.0f && *values <= 360.0f);

            Source->InnerAngle = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_CONE_OUTER_ANGLE:
            CHECKVAL(*values >= 0.0f && *values <= 360.0f);

            Source->OuterAngle = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_GAIN:
            CHECKVAL(*values >= 0.0f);

            Source->Gain = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_MAX_DISTANCE:
            CHECKVAL(*values >= 0.0f);

            Source->MaxDistance = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_ROLLOFF_FACTOR:
            CHECKVAL(*values >= 0.0f);

            Source->RolloffFactor = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_REFERENCE_DISTANCE:
            CHECKVAL(*values >= 0.0f);

            Source->RefDistance = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_MIN_GAIN:
            CHECKVAL(*values >= 0.0f);

            Source->MinGain = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_MAX_GAIN:
            CHECKVAL(*values >= 0.0f);

            Source->MaxGain = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_CONE_OUTER_GAIN:
            CHECKVAL(*values >= 0.0f && *values <= 1.0f);

            Source->OuterGain = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_CONE_OUTER_GAINHF:
            CHECKVAL(*values >= 0.0f && *values <= 1.0f);

            Source->OuterGainHF = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_AIR_ABSORPTION_FACTOR:
            CHECKVAL(*values >= 0.0f && *values <= 10.0f);

            Source->AirAbsorptionFactor = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_ROOM_ROLLOFF_FACTOR:
            CHECKVAL(*values >= 0.0f && *values <= 10.0f);

            Source->RoomRolloffFactor = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_DOPPLER_FACTOR:
            CHECKVAL(*values >= 0.0f && *values <= 1.0f);

            Source->DopplerFactor = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_SEC_OFFSET:
        case AL_SAMPLE_OFFSET:
        case AL_BYTE_OFFSET:
            CHECKVAL(*values >= 0.0f);

            Source->OffsetType = prop;
            Source->Offset = *values;

            if(IsPlayingOrPaused(Source))
            {
                ALvoice *voice;

                ALCdevice_Lock(Context->Device);
                /* Double-check that the source is still playing while we have
                 * the lock.
                 */
                voice = GetSourceVoice(Source, Context);
                if(voice)
                {
                    if(ApplyOffset(Source, voice) == AL_FALSE)
                    {
                        ALCdevice_Unlock(Context->Device);
                        SETERR_RETURN(Context, AL_INVALID_VALUE, AL_FALSE, "Invalid offset");
                    }
                }
                ALCdevice_Unlock(Context->Device);
            }
            return AL_TRUE;

        case AL_SOURCE_RADIUS:
            CHECKVAL(*values >= 0.0f && std::isfinite(*values));

            Source->Radius = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_STEREO_ANGLES:
            CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]));

            Source->StereoPan[0] = values[0];
            Source->StereoPan[1] = values[1];
            DO_UPDATEPROPS();
            return AL_TRUE;


        case AL_POSITION:
            CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]));

            Source->Position[0] = values[0];
            Source->Position[1] = values[1];
            Source->Position[2] = values[2];
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_VELOCITY:
            CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]));

            Source->Velocity[0] = values[0];
            Source->Velocity[1] = values[1];
            Source->Velocity[2] = values[2];
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_DIRECTION:
            CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]));

            Source->Direction[0] = values[0];
            Source->Direction[1] = values[1];
            Source->Direction[2] = values[2];
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_ORIENTATION:
            CHECKVAL(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]) &&
                     std::isfinite(values[3]) && std::isfinite(values[4]) && std::isfinite(values[5]));

            Source->Orientation[0][0] = values[0];
            Source->Orientation[0][1] = values[1];
            Source->Orientation[0][2] = values[2];
            Source->Orientation[1][0] = values[3];
            Source->Orientation[1][1] = values[4];
            Source->Orientation[1][2] = values[5];
            DO_UPDATEPROPS();
            return AL_TRUE;


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
            ival = (ALint)values[0];
            return SetSourceiv(Source, Context, prop, &ival);

        case AL_BUFFERS_QUEUED:
        case AL_BUFFERS_PROCESSED:
            ival = (ALint)((ALuint)values[0]);
            return SetSourceiv(Source, Context, prop, &ival);

        case AL_BUFFER:
        case AL_DIRECT_FILTER:
        case AL_AUXILIARY_SEND_FILTER:
        case AL_SAMPLE_OFFSET_LATENCY_SOFT:
        case AL_SAMPLE_OFFSET_CLOCK_SOFT:
            break;
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    SETERR_RETURN(Context, AL_INVALID_ENUM, AL_FALSE, "Invalid source float property 0x%04x", prop);
}

ALboolean SetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, const ALint *values)
{
    ALCdevice *device{Context->Device};
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
            SETERR_RETURN(Context, AL_INVALID_OPERATION, AL_FALSE,
                          "Setting read-only source property 0x%04x", prop);

        case AL_SOURCE_RELATIVE:
            CHECKVAL(*values == AL_FALSE || *values == AL_TRUE);

            Source->HeadRelative = (ALboolean)*values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_LOOPING:
            CHECKVAL(*values == AL_FALSE || *values == AL_TRUE);

            Source->Looping = (ALboolean)*values;
            if(IsPlayingOrPaused(Source))
            {
                ALvoice *voice{GetSourceVoice(Source, Context)};
                if(voice)
                {
                    if(Source->Looping)
                        voice->loop_buffer.store(Source->queue, std::memory_order_release);
                    else
                        voice->loop_buffer.store(nullptr, std::memory_order_release);

                    /* If the source is playing, wait for the current mix to finish
                     * to ensure it isn't currently looping back or reaching the
                     * end.
                     */
                    while((device->MixCount.load(std::memory_order_acquire)&1))
                        std::this_thread::yield();
                }
            }
            return AL_TRUE;

        case AL_BUFFER:
            buflock = std::unique_lock<std::mutex>{device->BufferLock};
            if(!(*values == 0 || (buffer=LookupBuffer(device, *values)) != nullptr))
                SETERR_RETURN(Context, AL_INVALID_VALUE, AL_FALSE, "Invalid buffer ID %u",
                              *values);

            if(buffer && buffer->MappedAccess != 0 &&
               !(buffer->MappedAccess&AL_MAP_PERSISTENT_BIT_SOFT))
                SETERR_RETURN(Context, AL_INVALID_OPERATION, AL_FALSE,
                              "Setting non-persistently mapped buffer %u", buffer->id);
            else
            {
                ALenum state = GetSourceState(Source, GetSourceVoice(Source, Context));
                if(state == AL_PLAYING || state == AL_PAUSED)
                    SETERR_RETURN(Context, AL_INVALID_OPERATION, AL_FALSE,
                                  "Setting buffer on playing or paused source %u", Source->id);
            }

            oldlist = Source->queue;
            if(buffer != nullptr)
            {
                /* Add the selected buffer to a one-item queue */
                auto newlist = static_cast<ALbufferlistitem*>(al_calloc(DEF_ALIGN,
                    FAM_SIZE(ALbufferlistitem, buffers, 1)));
                newlist->next.store(nullptr, std::memory_order_relaxed);
                newlist->max_samples = buffer->SampleLen;
                newlist->num_buffers = 1;
                newlist->buffers[0] = buffer;
                IncrementRef(&buffer->ref);

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
                ALbufferlistitem *temp{oldlist};
                oldlist = temp->next.load(std::memory_order_relaxed);

                for(ALsizei i{0};i < temp->num_buffers;i++)
                {
                    if(temp->buffers[i])
                        DecrementRef(&temp->buffers[i]->ref);
                }
                al_free(temp);
            }
            return AL_TRUE;

        case AL_SEC_OFFSET:
        case AL_SAMPLE_OFFSET:
        case AL_BYTE_OFFSET:
            CHECKVAL(*values >= 0);

            Source->OffsetType = prop;
            Source->Offset = *values;

            if(IsPlayingOrPaused(Source))
            {
                ALCdevice_Lock(Context->Device);
                ALvoice *voice{GetSourceVoice(Source, Context)};
                if(voice)
                {
                    if(ApplyOffset(Source, voice) == AL_FALSE)
                    {
                        ALCdevice_Unlock(Context->Device);
                        SETERR_RETURN(Context, AL_INVALID_VALUE, AL_FALSE,
                                      "Invalid source offset");
                    }
                }
                ALCdevice_Unlock(Context->Device);
            }
            return AL_TRUE;

        case AL_DIRECT_FILTER:
            filtlock = std::unique_lock<std::mutex>{device->FilterLock};
            if(!(*values == 0 || (filter=LookupFilter(device, *values)) != nullptr))
                SETERR_RETURN(Context, AL_INVALID_VALUE, AL_FALSE, "Invalid filter ID %u",
                              *values);

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
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_DIRECT_FILTER_GAINHF_AUTO:
            CHECKVAL(*values == AL_FALSE || *values == AL_TRUE);

            Source->DryGainHFAuto = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
            CHECKVAL(*values == AL_FALSE || *values == AL_TRUE);

            Source->WetGainAuto = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
            CHECKVAL(*values == AL_FALSE || *values == AL_TRUE);

            Source->WetGainHFAuto = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_DIRECT_CHANNELS_SOFT:
            CHECKVAL(*values == AL_FALSE || *values == AL_TRUE);

            Source->DirectChannels = *values;
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_DISTANCE_MODEL:
            CHECKVAL(*values == AL_NONE ||
                     *values == AL_INVERSE_DISTANCE ||
                     *values == AL_INVERSE_DISTANCE_CLAMPED ||
                     *values == AL_LINEAR_DISTANCE ||
                     *values == AL_LINEAR_DISTANCE_CLAMPED ||
                     *values == AL_EXPONENT_DISTANCE ||
                     *values == AL_EXPONENT_DISTANCE_CLAMPED);

            Source->mDistanceModel = static_cast<DistanceModel>(*values);
            if(Context->SourceDistanceModel)
                DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_SOURCE_RESAMPLER_SOFT:
            CHECKVAL(*values >= 0 && *values <= ResamplerMax);

            Source->Resampler = static_cast<enum Resampler>(*values);
            DO_UPDATEPROPS();
            return AL_TRUE;

        case AL_SOURCE_SPATIALIZE_SOFT:
            CHECKVAL(*values >= AL_FALSE && *values <= AL_AUTO_SOFT);

            Source->Spatialize = static_cast<enum SpatializeMode>(*values);
            DO_UPDATEPROPS();
            return AL_TRUE;


        case AL_AUXILIARY_SEND_FILTER:
            slotlock = std::unique_lock<std::mutex>{Context->EffectSlotLock};
            if(!(values[0] == 0 || (slot=LookupEffectSlot(Context, values[0])) != nullptr))
                SETERR_RETURN(Context, AL_INVALID_VALUE, AL_FALSE, "Invalid effect ID %u",
                              values[0]);
            if((ALuint)values[1] >= (ALuint)device->NumAuxSends)
                SETERR_RETURN(Context, AL_INVALID_VALUE, AL_FALSE, "Invalid send %u", values[1]);

            filtlock = std::unique_lock<std::mutex>{device->FilterLock};
            if(!(values[2] == 0 || (filter=LookupFilter(device, values[2])) != nullptr))
                SETERR_RETURN(Context, AL_INVALID_VALUE, AL_FALSE, "Invalid filter ID %u",
                              values[2]);

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
                if(slot) IncrementRef(&slot->ref);
                if(Source->Send[values[1]].Slot)
                    DecrementRef(&Source->Send[values[1]].Slot->ref);
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
                if(slot) IncrementRef(&slot->ref);
                if(Source->Send[values[1]].Slot)
                    DecrementRef(&Source->Send[values[1]].Slot->ref);
                Source->Send[values[1]].Slot = slot;
                DO_UPDATEPROPS();
            }

            return AL_TRUE;


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
            fvals[0] = (ALfloat)*values;
            return SetSourcefv(Source, Context, prop, fvals);

        /* 3x float */
        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            fvals[0] = (ALfloat)values[0];
            fvals[1] = (ALfloat)values[1];
            fvals[2] = (ALfloat)values[2];
            return SetSourcefv(Source, Context, prop, fvals);

        /* 6x float */
        case AL_ORIENTATION:
            fvals[0] = (ALfloat)values[0];
            fvals[1] = (ALfloat)values[1];
            fvals[2] = (ALfloat)values[2];
            fvals[3] = (ALfloat)values[3];
            fvals[4] = (ALfloat)values[4];
            fvals[5] = (ALfloat)values[5];
            return SetSourcefv(Source, Context, prop, fvals);

        case AL_SAMPLE_OFFSET_LATENCY_SOFT:
        case AL_SEC_OFFSET_LATENCY_SOFT:
        case AL_SEC_OFFSET_CLOCK_SOFT:
        case AL_SAMPLE_OFFSET_CLOCK_SOFT:
        case AL_STEREO_ANGLES:
            break;
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    SETERR_RETURN(Context, AL_INVALID_ENUM, AL_FALSE, "Invalid source integer property 0x%04x",
                  prop);
}

ALboolean SetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, const ALint64SOFT *values)
{
    ALfloat fvals[6];
    ALint   ivals[3];

    switch(prop)
    {
        case AL_SOURCE_TYPE:
        case AL_BUFFERS_QUEUED:
        case AL_BUFFERS_PROCESSED:
        case AL_SOURCE_STATE:
        case AL_SAMPLE_OFFSET_LATENCY_SOFT:
        case AL_SAMPLE_OFFSET_CLOCK_SOFT:
            /* Query only */
            SETERR_RETURN(Context, AL_INVALID_OPERATION, AL_FALSE,
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
            CHECKVAL(*values <= INT_MAX && *values >= INT_MIN);

            ivals[0] = (ALint)*values;
            return SetSourceiv(Source, Context, prop, ivals);

        /* 1x uint */
        case AL_BUFFER:
        case AL_DIRECT_FILTER:
            CHECKVAL(*values <= UINT_MAX && *values >= 0);

            ivals[0] = (ALuint)*values;
            return SetSourceiv(Source, Context, prop, ivals);

        /* 3x uint */
        case AL_AUXILIARY_SEND_FILTER:
            CHECKVAL(values[0] <= UINT_MAX && values[0] >= 0 &&
                     values[1] <= UINT_MAX && values[1] >= 0 &&
                     values[2] <= UINT_MAX && values[2] >= 0);

            ivals[0] = (ALuint)values[0];
            ivals[1] = (ALuint)values[1];
            ivals[2] = (ALuint)values[2];
            return SetSourceiv(Source, Context, prop, ivals);

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
            fvals[0] = (ALfloat)*values;
            return SetSourcefv(Source, Context, prop, fvals);

        /* 3x float */
        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            fvals[0] = (ALfloat)values[0];
            fvals[1] = (ALfloat)values[1];
            fvals[2] = (ALfloat)values[2];
            return SetSourcefv(Source, Context, prop, fvals);

        /* 6x float */
        case AL_ORIENTATION:
            fvals[0] = (ALfloat)values[0];
            fvals[1] = (ALfloat)values[1];
            fvals[2] = (ALfloat)values[2];
            fvals[3] = (ALfloat)values[3];
            fvals[4] = (ALfloat)values[4];
            fvals[5] = (ALfloat)values[5];
            return SetSourcefv(Source, Context, prop, fvals);

        case AL_SEC_OFFSET_LATENCY_SOFT:
        case AL_SEC_OFFSET_CLOCK_SOFT:
        case AL_STEREO_ANGLES:
            break;
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    SETERR_RETURN(Context, AL_INVALID_ENUM, AL_FALSE, "Invalid source integer64 property 0x%04x",
                  prop);
}

#undef CHECKVAL


ALboolean GetSourcedv(ALsource *Source, ALCcontext *Context, SourceProp prop, ALdouble *values);
ALboolean GetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, ALint *values);
ALboolean GetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, ALint64 *values);

ALboolean GetSourcedv(ALsource *Source, ALCcontext *Context, SourceProp prop, ALdouble *values)
{
    ALCdevice *device{Context->Device};
    ClockLatency clocktime;
    std::chrono::nanoseconds srcclock;
    ALint ivals[3];
    ALboolean err;

    switch(prop)
    {
        case AL_GAIN:
            *values = Source->Gain;
            return AL_TRUE;

        case AL_PITCH:
            *values = Source->Pitch;
            return AL_TRUE;

        case AL_MAX_DISTANCE:
            *values = Source->MaxDistance;
            return AL_TRUE;

        case AL_ROLLOFF_FACTOR:
            *values = Source->RolloffFactor;
            return AL_TRUE;

        case AL_REFERENCE_DISTANCE:
            *values = Source->RefDistance;
            return AL_TRUE;

        case AL_CONE_INNER_ANGLE:
            *values = Source->InnerAngle;
            return AL_TRUE;

        case AL_CONE_OUTER_ANGLE:
            *values = Source->OuterAngle;
            return AL_TRUE;

        case AL_MIN_GAIN:
            *values = Source->MinGain;
            return AL_TRUE;

        case AL_MAX_GAIN:
            *values = Source->MaxGain;
            return AL_TRUE;

        case AL_CONE_OUTER_GAIN:
            *values = Source->OuterGain;
            return AL_TRUE;

        case AL_SEC_OFFSET:
        case AL_SAMPLE_OFFSET:
        case AL_BYTE_OFFSET:
            *values = GetSourceOffset(Source, prop, Context);
            return AL_TRUE;

        case AL_CONE_OUTER_GAINHF:
            *values = Source->OuterGainHF;
            return AL_TRUE;

        case AL_AIR_ABSORPTION_FACTOR:
            *values = Source->AirAbsorptionFactor;
            return AL_TRUE;

        case AL_ROOM_ROLLOFF_FACTOR:
            *values = Source->RoomRolloffFactor;
            return AL_TRUE;

        case AL_DOPPLER_FACTOR:
            *values = Source->DopplerFactor;
            return AL_TRUE;

        case AL_SOURCE_RADIUS:
            *values = Source->Radius;
            return AL_TRUE;

        case AL_STEREO_ANGLES:
            values[0] = Source->StereoPan[0];
            values[1] = Source->StereoPan[1];
            return AL_TRUE;

        case AL_SEC_OFFSET_LATENCY_SOFT:
            /* Get the source offset with the clock time first. Then get the
             * clock time with the device latency. Order is important.
             */
            values[0] = GetSourceSecOffset(Source, Context, &srcclock);
            { std::lock_guard<std::mutex> _{device->BackendLock};
                clocktime = GetClockLatency(device);
            }
            if(srcclock == clocktime.ClockTime)
                values[1] = (ALdouble)clocktime.Latency.count() / 1000000000.0;
            else
            {
                /* If the clock time incremented, reduce the latency by that
                 * much since it's that much closer to the source offset it got
                 * earlier.
                 */
                std::chrono::nanoseconds diff = clocktime.ClockTime - srcclock;
                values[1] = (ALdouble)(clocktime.Latency - std::min(clocktime.Latency, diff)).count() /
                            1000000000.0;
            }
            return AL_TRUE;

        case AL_SEC_OFFSET_CLOCK_SOFT:
            values[0] = GetSourceSecOffset(Source, Context, &srcclock);
            values[1] = srcclock.count() / 1000000000.0;
            return AL_TRUE;

        case AL_POSITION:
            values[0] = Source->Position[0];
            values[1] = Source->Position[1];
            values[2] = Source->Position[2];
            return AL_TRUE;

        case AL_VELOCITY:
            values[0] = Source->Velocity[0];
            values[1] = Source->Velocity[1];
            values[2] = Source->Velocity[2];
            return AL_TRUE;

        case AL_DIRECTION:
            values[0] = Source->Direction[0];
            values[1] = Source->Direction[1];
            values[2] = Source->Direction[2];
            return AL_TRUE;

        case AL_ORIENTATION:
            values[0] = Source->Orientation[0][0];
            values[1] = Source->Orientation[0][1];
            values[2] = Source->Orientation[0][2];
            values[3] = Source->Orientation[1][0];
            values[4] = Source->Orientation[1][1];
            values[5] = Source->Orientation[1][2];
            return AL_TRUE;

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
            if((err=GetSourceiv(Source, Context, prop, ivals)) != AL_FALSE)
                *values = (ALdouble)ivals[0];
            return err;

        case AL_BUFFER:
        case AL_DIRECT_FILTER:
        case AL_AUXILIARY_SEND_FILTER:
        case AL_SAMPLE_OFFSET_LATENCY_SOFT:
        case AL_SAMPLE_OFFSET_CLOCK_SOFT:
            break;
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    SETERR_RETURN(Context, AL_INVALID_ENUM, AL_FALSE, "Invalid source double property 0x%04x",
                  prop);
}

ALboolean GetSourceiv(ALsource *Source, ALCcontext *Context, SourceProp prop, ALint *values)
{
    ALbufferlistitem *BufferList;
    ALdouble dvals[6];
    ALboolean err;

    switch(prop)
    {
        case AL_SOURCE_RELATIVE:
            *values = Source->HeadRelative;
            return AL_TRUE;

        case AL_LOOPING:
            *values = Source->Looping;
            return AL_TRUE;

        case AL_BUFFER:
            BufferList = (Source->SourceType == AL_STATIC) ? Source->queue : nullptr;
            *values = (BufferList && BufferList->num_buffers >= 1 && BufferList->buffers[0]) ?
                      BufferList->buffers[0]->id : 0;
            return AL_TRUE;

        case AL_SOURCE_STATE:
            *values = GetSourceState(Source, GetSourceVoice(Source, Context));
            return AL_TRUE;

        case AL_BUFFERS_QUEUED:
            if(!(BufferList=Source->queue))
                *values = 0;
            else
            {
                ALsizei count = 0;
                do {
                    count += BufferList->num_buffers;
                    BufferList = BufferList->next.load(std::memory_order_relaxed);
                } while(BufferList != nullptr);
                *values = count;
            }
            return AL_TRUE;

        case AL_BUFFERS_PROCESSED:
            if(Source->Looping || Source->SourceType != AL_STREAMING)
            {
                /* Buffers on a looping source are in a perpetual state of
                 * PENDING, so don't report any as PROCESSED */
                *values = 0;
            }
            else
            {
                const ALbufferlistitem *BufferList{Source->queue};
                const ALbufferlistitem *Current{nullptr};
                ALsizei played{0};

                ALvoice *voice{GetSourceVoice(Source, Context)};
                if(voice != nullptr)
                    Current = voice->current_buffer.load(std::memory_order_relaxed);
                else if(Source->state == AL_INITIAL)
                    Current = BufferList;

                while(BufferList && BufferList != Current)
                {
                    played += BufferList->num_buffers;
                    BufferList = BufferList->next.load(std::memory_order_relaxed);
                }
                *values = played;
            }
            return AL_TRUE;

        case AL_SOURCE_TYPE:
            *values = Source->SourceType;
            return AL_TRUE;

        case AL_DIRECT_FILTER_GAINHF_AUTO:
            *values = Source->DryGainHFAuto;
            return AL_TRUE;

        case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
            *values = Source->WetGainAuto;
            return AL_TRUE;

        case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
            *values = Source->WetGainHFAuto;
            return AL_TRUE;

        case AL_DIRECT_CHANNELS_SOFT:
            *values = Source->DirectChannels;
            return AL_TRUE;

        case AL_DISTANCE_MODEL:
            *values = static_cast<int>(Source->mDistanceModel);
            return AL_TRUE;

        case AL_SOURCE_RESAMPLER_SOFT:
            *values = Source->Resampler;
            return AL_TRUE;

        case AL_SOURCE_SPATIALIZE_SOFT:
            *values = Source->Spatialize;
            return AL_TRUE;

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
            if((err=GetSourcedv(Source, Context, prop, dvals)) != AL_FALSE)
                *values = (ALint)dvals[0];
            return err;

        /* 3x float/double */
        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            if((err=GetSourcedv(Source, Context, prop, dvals)) != AL_FALSE)
            {
                values[0] = (ALint)dvals[0];
                values[1] = (ALint)dvals[1];
                values[2] = (ALint)dvals[2];
            }
            return err;

        /* 6x float/double */
        case AL_ORIENTATION:
            if((err=GetSourcedv(Source, Context, prop, dvals)) != AL_FALSE)
            {
                values[0] = (ALint)dvals[0];
                values[1] = (ALint)dvals[1];
                values[2] = (ALint)dvals[2];
                values[3] = (ALint)dvals[3];
                values[4] = (ALint)dvals[4];
                values[5] = (ALint)dvals[5];
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
    SETERR_RETURN(Context, AL_INVALID_ENUM, AL_FALSE, "Invalid source integer property 0x%04x",
                  prop);
}

ALboolean GetSourcei64v(ALsource *Source, ALCcontext *Context, SourceProp prop, ALint64 *values)
{
    ALCdevice *device = Context->Device;
    ClockLatency clocktime;
    std::chrono::nanoseconds srcclock;
    ALdouble dvals[6];
    ALint ivals[3];
    ALboolean err;

    switch(prop)
    {
        case AL_SAMPLE_OFFSET_LATENCY_SOFT:
            /* Get the source offset with the clock time first. Then get the
             * clock time with the device latency. Order is important.
             */
            values[0] = GetSourceSampleOffset(Source, Context, &srcclock);
            { std::lock_guard<std::mutex> _{device->BackendLock};
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
            return AL_TRUE;

        case AL_SAMPLE_OFFSET_CLOCK_SOFT:
            values[0] = GetSourceSampleOffset(Source, Context, &srcclock);
            values[1] = srcclock.count();
            return AL_TRUE;

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
            if((err=GetSourcedv(Source, Context, prop, dvals)) != AL_FALSE)
                *values = (ALint64)dvals[0];
            return err;

        /* 3x float/double */
        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            if((err=GetSourcedv(Source, Context, prop, dvals)) != AL_FALSE)
            {
                values[0] = (ALint64)dvals[0];
                values[1] = (ALint64)dvals[1];
                values[2] = (ALint64)dvals[2];
            }
            return err;

        /* 6x float/double */
        case AL_ORIENTATION:
            if((err=GetSourcedv(Source, Context, prop, dvals)) != AL_FALSE)
            {
                values[0] = (ALint64)dvals[0];
                values[1] = (ALint64)dvals[1];
                values[2] = (ALint64)dvals[2];
                values[3] = (ALint64)dvals[3];
                values[4] = (ALint64)dvals[4];
                values[5] = (ALint64)dvals[5];
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
            if((err=GetSourceiv(Source, Context, prop, ivals)) != AL_FALSE)
                *values = ivals[0];
            return err;

        /* 1x uint */
        case AL_BUFFER:
        case AL_DIRECT_FILTER:
            if((err=GetSourceiv(Source, Context, prop, ivals)) != AL_FALSE)
                *values = (ALuint)ivals[0];
            return err;

        /* 3x uint */
        case AL_AUXILIARY_SEND_FILTER:
            if((err=GetSourceiv(Source, Context, prop, ivals)) != AL_FALSE)
            {
                values[0] = (ALuint)ivals[0];
                values[1] = (ALuint)ivals[1];
                values[2] = (ALuint)ivals[2];
            }
            return err;

        case AL_SEC_OFFSET_LATENCY_SOFT:
        case AL_SEC_OFFSET_CLOCK_SOFT:
            break; /* Double only */
        case AL_STEREO_ANGLES:
            break; /* Float/double only */
    }

    ERR("Unexpected property: 0x%04x\n", prop);
    SETERR_RETURN(Context, AL_INVALID_ENUM, AL_FALSE, "Invalid source integer64 property 0x%04x",
                  prop);
}

} // namespace

AL_API ALvoid AL_APIENTRY alGenSources(ALsizei n, ALuint *sources)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(n < 0)
        alSetError(context.get(), AL_INVALID_VALUE, "Generating %d sources", n);
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
            alDeleteSources(std::distance(tempids.begin(), alloc_end), tempids.data());
        else
            std::copy(tempids.cbegin(), tempids.cend(), sources);
    }
}


AL_API ALvoid AL_APIENTRY alDeleteSources(ALsizei n, const ALuint *sources)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(n < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Deleting %d sources", n);

    std::lock_guard<std::mutex> _{context->SourceLock};

    /* Check that all Sources are valid */
    const ALuint *sources_end = sources + n;
    auto invsrc = std::find_if_not(sources, sources_end,
        [&context](ALuint sid) -> bool
        {
            if(!LookupSource(context.get(), sid))
            {
                alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", sid);
                return false;
            }
            return true;
        }
    );
    if(LIKELY(invsrc == sources_end))
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


AL_API ALboolean AL_APIENTRY alIsSource(ALuint source)
{
    ContextRef context{GetContextRef()};
    if(LIKELY(context))
    {
        std::lock_guard<std::mutex> _{context->SourceLock};
        if(LookupSource(context.get(), source) != nullptr)
            return AL_TRUE;
    }
    return AL_FALSE;
}


AL_API ALvoid AL_APIENTRY alSourcef(ALuint source, ALenum param, ALfloat value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(FloatValsByProp(param) != 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid float property 0x%04x", param);
    else
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), &value);
}

AL_API ALvoid AL_APIENTRY alSource3f(ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(FloatValsByProp(param) != 3)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid 3-float property 0x%04x", param);
    else
    {
        ALfloat fvals[3] = { value1, value2, value3 };
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), fvals);
    }
}

AL_API ALvoid AL_APIENTRY alSourcefv(ALuint source, ALenum param, const ALfloat *values)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(FloatValsByProp(param) < 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid float-vector property 0x%04x", param);
    else
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), values);
}


AL_API ALvoid AL_APIENTRY alSourcedSOFT(ALuint source, ALenum param, ALdouble value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(DoubleValsByProp(param) != 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid double property 0x%04x", param);
    else
    {
        ALfloat fval = (ALfloat)value;
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), &fval);
    }
}

AL_API ALvoid AL_APIENTRY alSource3dSOFT(ALuint source, ALenum param, ALdouble value1, ALdouble value2, ALdouble value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(DoubleValsByProp(param) != 3)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid 3-double property 0x%04x", param);
    else
    {
        ALfloat fvals[3] = { (ALfloat)value1, (ALfloat)value2, (ALfloat)value3 };
        SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), fvals);
    }
}

AL_API ALvoid AL_APIENTRY alSourcedvSOFT(ALuint source, ALenum param, const ALdouble *values)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else
    {
        ALint count{DoubleValsByProp(param)};
        if(count < 1 || count > 6)
            alSetError(context.get(), AL_INVALID_ENUM, "Invalid double-vector property 0x%04x", param);
        else
        {
            ALfloat fvals[6];
            ALint i;

            for(i = 0;i < count;i++)
                fvals[i] = (ALfloat)values[i];
            SetSourcefv(Source, context.get(), static_cast<SourceProp>(param), fvals);
        }
    }
}


AL_API ALvoid AL_APIENTRY alSourcei(ALuint source, ALenum param, ALint value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(IntValsByProp(param) != 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid integer property 0x%04x", param);
    else
        SetSourceiv(Source, context.get(), static_cast<SourceProp>(param), &value);
}

AL_API void AL_APIENTRY alSource3i(ALuint source, ALenum param, ALint value1, ALint value2, ALint value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(IntValsByProp(param) != 3)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid 3-integer property 0x%04x", param);
    else
    {
        ALint ivals[3] = { value1, value2, value3 };
        SetSourceiv(Source, context.get(), static_cast<SourceProp>(param), ivals);
    }
}

AL_API void AL_APIENTRY alSourceiv(ALuint source, ALenum param, const ALint *values)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source = LookupSource(context.get(), source);
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(IntValsByProp(param) < 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid integer-vector property 0x%04x", param);
    else
        SetSourceiv(Source, context.get(), static_cast<SourceProp>(param), values);
}


AL_API ALvoid AL_APIENTRY alSourcei64SOFT(ALuint source, ALenum param, ALint64SOFT value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(Int64ValsByProp(param) != 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid integer64 property 0x%04x", param);
    else
        SetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), &value);
}

AL_API void AL_APIENTRY alSource3i64SOFT(ALuint source, ALenum param, ALint64SOFT value1, ALint64SOFT value2, ALint64SOFT value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(Int64ValsByProp(param) != 3)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid 3-integer64 property 0x%04x", param);
    else
    {
        ALint64SOFT i64vals[3] = { value1, value2, value3 };
        SetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), i64vals);
    }
}

AL_API void AL_APIENTRY alSourcei64vSOFT(ALuint source, ALenum param, const ALint64SOFT *values)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(Int64ValsByProp(param) < 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid integer64-vector property 0x%04x", param);
    else
        SetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), values);
}


AL_API ALvoid AL_APIENTRY alGetSourcef(ALuint source, ALenum param, ALfloat *value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!value)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(FloatValsByProp(param) != 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid float property 0x%04x", param);
    else
    {
        ALdouble dval;
        if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), &dval))
            *value = (ALfloat)dval;
    }
}


AL_API ALvoid AL_APIENTRY alGetSource3f(ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!(value1 && value2 && value3))
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(FloatValsByProp(param) != 3)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid 3-float property 0x%04x", param);
    else
    {
        ALdouble dvals[3];
        if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), dvals))
        {
            *value1 = (ALfloat)dvals[0];
            *value2 = (ALfloat)dvals[1];
            *value3 = (ALfloat)dvals[2];
        }
    }
}


AL_API ALvoid AL_APIENTRY alGetSourcefv(ALuint source, ALenum param, ALfloat *values)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else
    {
        ALint count{FloatValsByProp(param)};
        if(count < 1 && count > 6)
            alSetError(context.get(), AL_INVALID_ENUM, "Invalid float-vector property 0x%04x", param);
        else
        {
            ALdouble dvals[6];
            if(GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), dvals))
            {
                for(ALint i{0};i < count;i++)
                    values[i] = (ALfloat)dvals[i];
            }
        }
    }
}


AL_API void AL_APIENTRY alGetSourcedSOFT(ALuint source, ALenum param, ALdouble *value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!value)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(DoubleValsByProp(param) != 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid double property 0x%04x", param);
    else
        GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), value);
}

AL_API void AL_APIENTRY alGetSource3dSOFT(ALuint source, ALenum param, ALdouble *value1, ALdouble *value2, ALdouble *value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!(value1 && value2 && value3))
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(DoubleValsByProp(param) != 3)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid 3-double property 0x%04x", param);
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

AL_API void AL_APIENTRY alGetSourcedvSOFT(ALuint source, ALenum param, ALdouble *values)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(DoubleValsByProp(param) < 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid double-vector property 0x%04x", param);
    else
        GetSourcedv(Source, context.get(), static_cast<SourceProp>(param), values);
}


AL_API ALvoid AL_APIENTRY alGetSourcei(ALuint source, ALenum param, ALint *value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!value)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(IntValsByProp(param) != 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid integer property 0x%04x", param);
    else
        GetSourceiv(Source, context.get(), static_cast<SourceProp>(param), value);
}


AL_API void AL_APIENTRY alGetSource3i(ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!(value1 && value2 && value3))
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(IntValsByProp(param) != 3)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid 3-integer property 0x%04x", param);
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


AL_API void AL_APIENTRY alGetSourceiv(ALuint source, ALenum param, ALint *values)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(IntValsByProp(param) < 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid integer-vector property 0x%04x", param);
    else
        GetSourceiv(Source, context.get(), static_cast<SourceProp>(param), values);
}


AL_API void AL_APIENTRY alGetSourcei64SOFT(ALuint source, ALenum param, ALint64SOFT *value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!value)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(Int64ValsByProp(param) != 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid integer64 property 0x%04x", param);
    else
        GetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), value);
}

AL_API void AL_APIENTRY alGetSource3i64SOFT(ALuint source, ALenum param, ALint64SOFT *value1, ALint64SOFT *value2, ALint64SOFT *value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!(value1 && value2 && value3))
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(Int64ValsByProp(param) != 3)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid 3-integer64 property 0x%04x", param);
    else
    {
        ALint64 i64vals[3];
        if(GetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), i64vals))
        {
            *value1 = i64vals[0];
            *value2 = i64vals[1];
            *value3 = i64vals[2];
        }
    }
}

AL_API void AL_APIENTRY alGetSourcei64vSOFT(ALuint source, ALenum param, ALint64SOFT *values)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *Source{LookupSource(context.get(), source)};
    if(UNLIKELY(!Source))
        alSetError(context.get(), AL_INVALID_NAME, "Invalid source ID %u", source);
    else if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else if(Int64ValsByProp(param) < 1)
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid integer64-vector property 0x%04x", param);
    else
        GetSourcei64v(Source, context.get(), static_cast<SourceProp>(param), values);
}


AL_API ALvoid AL_APIENTRY alSourcePlay(ALuint source)
{
    alSourcePlayv(1, &source);
}
AL_API ALvoid AL_APIENTRY alSourcePlayv(ALsizei n, const ALuint *sources)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(n < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Playing %d sources", n);
    if(n == 0) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    for(ALsizei i{0};i < n;i++)
    {
        if(!LookupSource(context.get(), sources[i]))
            SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid source ID %u", sources[i]);
    }

    ALCdevice *device{context->Device};
    ALCdevice_Lock(device);
    /* If the device is disconnected, go right to stopped. */
    if(!device->Connected.load(std::memory_order_acquire))
    {
        /* TODO: Send state change event? */
        for(ALsizei i{0};i < n;i++)
        {
            ALsource *source{LookupSource(context.get(), sources[i])};
            source->OffsetType = AL_NONE;
            source->Offset = 0.0;
            source->state = AL_STOPPED;
        }
        ALCdevice_Unlock(device);
        return;
    }

    while(n > context->MaxVoices-context->VoiceCount.load(std::memory_order_relaxed))
    {
        ALsizei newcount = context->MaxVoices << 1;
        if(context->MaxVoices >= newcount)
        {
            ALCdevice_Unlock(device);
            SETERR_RETURN(context.get(), AL_OUT_OF_MEMORY,,
                "Overflow increasing voice count %d -> %d", context->MaxVoices, newcount);
        }
        AllocateVoices(context.get(), newcount, device->NumAuxSends);
    }

    for(ALsizei i{0};i < n;i++)
    {
        ALsource *source{LookupSource(context.get(), sources[i])};
        /* Check that there is a queue containing at least one valid, non zero
         * length buffer.
         */
        ALbufferlistitem *BufferList{source->queue};
        while(BufferList && BufferList->max_samples == 0)
            BufferList = BufferList->next.load(std::memory_order_relaxed);

        /* If there's nothing to play, go right to stopped. */
        if(UNLIKELY(!BufferList))
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
            continue;
        }

        ALvoice *voice{GetSourceVoice(source, context.get())};
        switch(GetSourceState(source, voice))
        {
            case AL_PLAYING:
                assert(voice != nullptr);
                /* A source that's already playing is restarted from the beginning. */
                voice->current_buffer.store(BufferList, std::memory_order_relaxed);
                voice->position.store(0u, std::memory_order_relaxed);
                voice->position_fraction.store(0, std::memory_order_release);
                continue;

            case AL_PAUSED:
                assert(voice != nullptr);
                /* A source that's paused simply resumes. */
                voice->Playing.store(true, std::memory_order_release);
                source->state = AL_PLAYING;
                SendStateChangeEvent(context.get(), source->id, AL_PLAYING);
                continue;

            default:
                break;
        }

        /* Look for an unused voice to play this source with. */
        assert(voice == nullptr);
        auto voices_end = context->Voices + context->VoiceCount.load(std::memory_order_relaxed);
        auto voice_iter = std::find_if(context->Voices, voices_end,
            [](const ALvoice *voice) noexcept -> bool
            { return voice->SourceID.load(std::memory_order_relaxed) == 0u; }
        );
        auto vidx = static_cast<ALint>(std::distance(context->Voices, voice_iter));
        voice = *voice_iter;
        voice->Playing.store(false, std::memory_order_release);
        if(voice_iter == voices_end) context->VoiceCount.fetch_add(1, std::memory_order_acq_rel);

        source->PropsClean.test_and_set(std::memory_order_acquire);
        UpdateSourceProps(source, voice, context.get());

        /* A source that's not playing or paused has any offset applied when it
         * starts playing.
         */
        if(source->Looping)
            voice->loop_buffer.store(source->queue, std::memory_order_relaxed);
        else
            voice->loop_buffer.store(nullptr, std::memory_order_relaxed);
        voice->current_buffer.store(BufferList, std::memory_order_relaxed);
        voice->position.store(0u, std::memory_order_relaxed);
        voice->position_fraction.store(0, std::memory_order_relaxed);
        bool start_fading{false};
        if(ApplyOffset(source, voice) != AL_FALSE)
            start_fading = voice->position.load(std::memory_order_relaxed) != 0 ||
                voice->position_fraction.load(std::memory_order_relaxed) != 0 ||
                voice->current_buffer.load(std::memory_order_relaxed) != BufferList;

        auto buffers_end = BufferList->buffers + BufferList->num_buffers;
        auto buffer = std::find_if(BufferList->buffers, buffers_end,
            [](const ALbuffer *buffer) noexcept -> bool
            { return buffer != nullptr; }
        );
        if(buffer != buffers_end)
        {
            voice->NumChannels = ChannelsFromFmt((*buffer)->FmtChannels);
            voice->SampleSize  = BytesFromFmt((*buffer)->FmtType);
        }

        /* Clear previous samples. */
        for(auto &samples : voice->PrevSamples)
            std::fill(std::begin(samples), std::end(samples), 0.0f);

        /* Clear the stepping value so the mixer knows not to mix this until
         * the update gets applied.
         */
        voice->Step = 0;

        voice->Flags = start_fading ? VOICE_IS_FADING : 0;
        if(source->SourceType == AL_STATIC) voice->Flags |= VOICE_IS_STATIC;
        memset(voice->Direct.Params, 0, sizeof(voice->Direct.Params[0])*voice->NumChannels);
        for(ALsizei j{0};j < device->NumAuxSends;j++)
            memset(voice->Send[j].Params, 0, sizeof(voice->Send[j].Params[0])*voice->NumChannels);
        if(device->AvgSpeakerDist > 0.0f)
        {
            ALfloat w1 = SPEEDOFSOUNDMETRESPERSEC /
                         (device->AvgSpeakerDist * device->Frequency);
            for(ALsizei j{0};j < voice->NumChannels;j++)
                NfcFilterCreate(&voice->Direct.Params[j].NFCtrlFilter, 0.0f, w1);
        }

        voice->SourceID.store(source->id, std::memory_order_relaxed);
        voice->Playing.store(true, std::memory_order_release);
        source->state = AL_PLAYING;
        source->VoiceIdx = vidx;

        SendStateChangeEvent(context.get(), source->id, AL_PLAYING);
    }
    ALCdevice_Unlock(device);
}

AL_API ALvoid AL_APIENTRY alSourcePause(ALuint source)
{
    alSourcePausev(1, &source);
}
AL_API ALvoid AL_APIENTRY alSourcePausev(ALsizei n, const ALuint *sources)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(n < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Pausing %d sources", n);
    if(n == 0) return;

    for(ALsizei i{0};i < n;i++)
    {
        if(!LookupSource(context.get(), sources[i]))
            SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid source ID %u", sources[i]);
    }

    ALCdevice *device{context->Device};
    ALCdevice_Lock(device);
    for(ALsizei i{0};i < n;i++)
    {
        ALsource *source{LookupSource(context.get(), sources[i])};
        ALvoice *voice{GetSourceVoice(source, context.get())};
        if(voice) voice->Playing.store(false, std::memory_order_release);
        if(GetSourceState(source, voice) == AL_PLAYING)
        {
            source->state = AL_PAUSED;
            SendStateChangeEvent(context.get(), source->id, AL_PAUSED);
        }
    }
    ALCdevice_Unlock(device);
}

AL_API ALvoid AL_APIENTRY alSourceStop(ALuint source)
{
    alSourceStopv(1, &source);
}
AL_API ALvoid AL_APIENTRY alSourceStopv(ALsizei n, const ALuint *sources)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(n < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Stopping %d sources", n);
    if(n == 0) return;

    for(ALsizei i{0};i < n;i++)
    {
        if(!LookupSource(context.get(), sources[i]))
            SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid source ID %u", sources[i]);
    }

    ALCdevice *device{context->Device};
    ALCdevice_Lock(device);
    for(ALsizei i{0};i < n;i++)
    {
        ALsource *source{LookupSource(context.get(), sources[i])};
        ALvoice *voice{GetSourceVoice(source, context.get())};
        if(voice != nullptr)
        {
            voice->SourceID.store(0u, std::memory_order_relaxed);
            voice->Playing.store(false, std::memory_order_release);
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
    }
    ALCdevice_Unlock(device);
}

AL_API ALvoid AL_APIENTRY alSourceRewind(ALuint source)
{
    alSourceRewindv(1, &source);
}
AL_API ALvoid AL_APIENTRY alSourceRewindv(ALsizei n, const ALuint *sources)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(n < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Rewinding %d sources", n);
    if(n == 0) return;

    for(ALsizei i{0};i < n;i++)
    {
        if(!LookupSource(context.get(), sources[i]))
            SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid source ID %u", sources[i]);
    }

    ALCdevice *device{context->Device};
    ALCdevice_Lock(device);
    for(ALsizei i{0};i < n;i++)
    {
        ALsource *source{LookupSource(context.get(), sources[i])};
        ALvoice *voice{GetSourceVoice(source, context.get())};
        if(voice != nullptr)
        {
            voice->SourceID.store(0u, std::memory_order_relaxed);
            voice->Playing.store(false, std::memory_order_release);
            voice = nullptr;
        }
        if(GetSourceState(source, voice) != AL_INITIAL)
        {
            source->state = AL_INITIAL;
            SendStateChangeEvent(context.get(), source->id, AL_INITIAL);
        }
        source->OffsetType = AL_NONE;
        source->Offset = 0.0;
    }
    ALCdevice_Unlock(device);
}


AL_API ALvoid AL_APIENTRY alSourceQueueBuffers(ALuint src, ALsizei nb, const ALuint *buffers)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(nb < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Queueing %d buffers", nb);
    if(nb == 0) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *source{LookupSource(context.get(),src)};
    if(!source)
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid source ID %u", src);

    /* Can't queue on a Static Source */
    if(source->SourceType == AL_STATIC)
        SETERR_RETURN(context.get(), AL_INVALID_OPERATION,, "Queueing onto static source %u", src);

    /* Check for a valid Buffer, for its frequency and format */
    ALCdevice *device{context->Device};
    ALbuffer *BufferFmt{nullptr};
    ALbufferlistitem *BufferList{source->queue};
    while(BufferList)
    {
        for(ALsizei i{0};i < BufferList->num_buffers;i++)
        {
            if((BufferFmt=BufferList->buffers[i]) != nullptr)
                break;
        }
        if(BufferFmt) break;
        BufferList = BufferList->next.load(std::memory_order_relaxed);
    }

    std::unique_lock<std::mutex> buflock{device->BufferLock};
    ALbufferlistitem *BufferListStart{nullptr};
    BufferList = nullptr;
    for(ALsizei i{0};i < nb;i++)
    {
        ALbuffer *buffer{nullptr};
        if(buffers[i] && (buffer=LookupBuffer(device, buffers[i])) == nullptr)
        {
            alSetError(context.get(), AL_INVALID_NAME, "Queueing invalid buffer ID %u",
                       buffers[i]);
            goto buffer_error;
        }

        if(!BufferListStart)
        {
            BufferListStart = static_cast<ALbufferlistitem*>(al_calloc(DEF_ALIGN,
                FAM_SIZE(ALbufferlistitem, buffers, 1)));
            BufferList = BufferListStart;
        }
        else
        {
            auto item = static_cast<ALbufferlistitem*>(al_calloc(DEF_ALIGN,
                FAM_SIZE(ALbufferlistitem, buffers, 1)));
            BufferList->next.store(item, std::memory_order_relaxed);
            BufferList = item;
        }
        BufferList->next.store(nullptr, std::memory_order_relaxed);
        BufferList->max_samples = buffer ? buffer->SampleLen : 0;
        BufferList->num_buffers = 1;
        BufferList->buffers[0] = buffer;
        if(!buffer) continue;

        IncrementRef(&buffer->ref);

        if(buffer->MappedAccess != 0 && !(buffer->MappedAccess&AL_MAP_PERSISTENT_BIT_SOFT))
        {
            alSetError(context.get(), AL_INVALID_OPERATION,
                       "Queueing non-persistently mapped buffer %u", buffer->id);
            goto buffer_error;
        }

        if(BufferFmt == nullptr)
            BufferFmt = buffer;
        else if(BufferFmt->Frequency != buffer->Frequency ||
                BufferFmt->FmtChannels != buffer->FmtChannels ||
                BufferFmt->OriginalType != buffer->OriginalType)
        {
            alSetError(context.get(), AL_INVALID_OPERATION,
                       "Queueing buffer with mismatched format");

        buffer_error:
            /* A buffer failed (invalid ID or format), so unlock and release
             * each buffer we had. */
            while(BufferListStart)
            {
                ALbufferlistitem *next = BufferListStart->next.load(std::memory_order_relaxed);
                for(i = 0;i < BufferListStart->num_buffers;i++)
                {
                    if((buffer=BufferListStart->buffers[i]) != nullptr)
                        DecrementRef(&buffer->ref);
                }
                al_free(BufferListStart);
                BufferListStart = next;
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
        while((next=BufferList->next.load(std::memory_order_relaxed)) != nullptr)
            BufferList = next;
        BufferList->next.store(BufferListStart, std::memory_order_release);
    }
}

AL_API void AL_APIENTRY alSourceQueueBufferLayersSOFT(ALuint src, ALsizei nb, const ALuint *buffers)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(nb < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Queueing %d buffer layers", nb);
    if(nb == 0) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *source{LookupSource(context.get(),src)};
    if(!source)
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid source ID %u", src);

    /* Can't queue on a Static Source */
    if(source->SourceType == AL_STATIC)
        SETERR_RETURN(context.get(), AL_INVALID_OPERATION,, "Queueing onto static source %u", src);

    /* Check for a valid Buffer, for its frequency and format */
    ALCdevice *device{context->Device};
    ALbuffer *BufferFmt{nullptr};
    ALbufferlistitem *BufferList{source->queue};
    while(BufferList)
    {
        for(ALsizei i{0};i < BufferList->num_buffers;i++)
        {
            if((BufferFmt=BufferList->buffers[i]) != nullptr)
                break;
        }
        if(BufferFmt) break;
        BufferList = BufferList->next.load(std::memory_order_relaxed);
    }

    std::unique_lock<std::mutex> buflock{device->BufferLock};
    auto BufferListStart = static_cast<ALbufferlistitem*>(al_calloc(DEF_ALIGN,
        FAM_SIZE(ALbufferlistitem, buffers, nb)));
    BufferList = BufferListStart;
    BufferList->next.store(nullptr, std::memory_order_relaxed);
    BufferList->max_samples = 0;
    BufferList->num_buffers = 0;

    for(ALsizei i{0};i < nb;i++)
    {
        ALbuffer *buffer{nullptr};
        if(buffers[i] && (buffer=LookupBuffer(device, buffers[i])) == nullptr)
        {
            alSetError(context.get(), AL_INVALID_NAME, "Queueing invalid buffer ID %u",
                       buffers[i]);
            goto buffer_error;
        }

        BufferList->buffers[BufferList->num_buffers++] = buffer;
        if(!buffer) continue;

        IncrementRef(&buffer->ref);

        BufferList->max_samples = maxi(BufferList->max_samples, buffer->SampleLen);

        if(buffer->MappedAccess != 0 && !(buffer->MappedAccess&AL_MAP_PERSISTENT_BIT_SOFT))
        {
            alSetError(context.get(), AL_INVALID_OPERATION,
                       "Queueing non-persistently mapped buffer %u", buffer->id);
            goto buffer_error;
        }

        if(BufferFmt == nullptr)
            BufferFmt = buffer;
        else if(BufferFmt->Frequency != buffer->Frequency ||
                BufferFmt->FmtChannels != buffer->FmtChannels ||
                BufferFmt->OriginalType != buffer->OriginalType)
        {
            alSetError(context.get(), AL_INVALID_OPERATION,
                       "Queueing buffer with mismatched format");

        buffer_error:
            /* A buffer failed (invalid ID or format), so unlock and release
             * each buffer we had. */
            while(BufferListStart)
            {
                ALbufferlistitem *next{BufferListStart->next.load(std::memory_order_relaxed)};
                for(i = 0;i < BufferListStart->num_buffers;i++)
                {
                    if((buffer=BufferListStart->buffers[i]) != nullptr)
                        DecrementRef(&buffer->ref);
                }
                al_free(BufferListStart);
                BufferListStart = next;
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
        while((next=BufferList->next.load(std::memory_order_relaxed)) != nullptr)
            BufferList = next;
        BufferList->next.store(BufferListStart, std::memory_order_release);
    }
}

AL_API ALvoid AL_APIENTRY alSourceUnqueueBuffers(ALuint src, ALsizei nb, ALuint *buffers)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(nb < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Unqueueing %d buffers", nb);
    if(nb == 0) return;

    std::lock_guard<std::mutex> _{context->SourceLock};
    ALsource *source{LookupSource(context.get(),src)};
    if(!source)
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid source ID %u", src);

    if(source->Looping)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Unqueueing from looping source %u", src);
    if(source->SourceType != AL_STREAMING)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,,
            "Unqueueing from a non-streaming source %u", src);

    /* Make sure enough buffers have been processed to unqueue. */
    ALbufferlistitem *BufferList{source->queue};
    ALvoice *voice{GetSourceVoice(source, context.get())};
    ALbufferlistitem *Current{nullptr};
    if(voice)
        Current = voice->current_buffer.load(std::memory_order_relaxed);
    else if(source->state == AL_INITIAL)
        Current = BufferList;
    if(BufferList == Current)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Unqueueing pending buffers");

    ALsizei i{BufferList->num_buffers};
    while(i < nb)
    {
        /* If the next bufferlist to check is NULL or is the current one, it's
         * trying to unqueue pending buffers.
         */
        ALbufferlistitem *next{BufferList->next.load(std::memory_order_relaxed)};
        if(!next || next == Current)
            SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Unqueueing pending buffers");
        BufferList = next;

        i += BufferList->num_buffers;
    }

    while(nb > 0)
    {
        ALbufferlistitem *head{source->queue};
        ALbufferlistitem *next{head->next.load(std::memory_order_relaxed)};
        for(i = 0;i < head->num_buffers && nb > 0;i++,nb--)
        {
            ALbuffer *buffer{head->buffers[i]};
            if(!buffer)
                *(buffers++) = 0;
            else
            {
                *(buffers++) = buffer->id;
                DecrementRef(&buffer->ref);
            }
        }
        if(i < head->num_buffers)
        {
            /* This head has some buffers left over, so move them to the front
             * and update the sample and buffer count.
             */
            ALsizei max_length{0};
            ALsizei j{0};
            while(i < head->num_buffers)
            {
                ALbuffer *buffer{head->buffers[i++]};
                if(buffer) max_length = maxi(max_length, buffer->SampleLen);
                head->buffers[j++] = buffer;
            }
            head->max_samples = max_length;
            head->num_buffers = j;
            break;
        }

        /* Otherwise, free this item and set the source queue head to the next
         * one.
         */
        al_free(head);
        source->queue = next;
    }
}


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
    Orientation[0][0] =  0.0f;
    Orientation[0][1] =  0.0f;
    Orientation[0][2] = -1.0f;
    Orientation[1][0] =  0.0f;
    Orientation[1][1] =  1.0f;
    Orientation[1][2] =  0.0f;
    RefDistance = 1.0f;
    MaxDistance = FLT_MAX;
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
    Resampler = ResamplerDefault;
    DirectChannels = AL_FALSE;
    Spatialize = SpatializeAuto;

    StereoPan[0] = DEG2RAD( 30.0f);
    StereoPan[1] = DEG2RAD(-30.0f);

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

    Offset = 0.0;
    OffsetType = AL_NONE;
    SourceType = AL_UNDETERMINED;
    state = AL_INITIAL;

    queue = nullptr;

    VoiceIdx = -1;
}

ALsource::~ALsource()
{
    ALbufferlistitem *BufferList{queue};
    while(BufferList != nullptr)
    {
        ALbufferlistitem *next{BufferList->next.load(std::memory_order_relaxed)};
        for(ALsizei i{0};i < BufferList->num_buffers;i++)
        {
            if(BufferList->buffers[i])
                DecrementRef(&BufferList->buffers[i]->ref);
        }
        al_free(BufferList);
        BufferList = next;
    }
    queue = nullptr;

    std::for_each(Send.begin(), Send.end(),
        [](ALsource::SendData &send) -> void
        {
            if(send.Slot)
                DecrementRef(&send.Slot->ref);
            send.Slot = nullptr;
        }
    );
}

void UpdateAllSourceProps(ALCcontext *context)
{
    auto voices_end = context->Voices + context->VoiceCount.load(std::memory_order_relaxed);
    std::for_each(context->Voices, voices_end,
        [context](ALvoice *voice) -> void
        {
            ALuint sid{voice->SourceID.load(std::memory_order_acquire)};
            ALsource *source = sid ? LookupSource(context, sid) : nullptr;
            if(source && !source->PropsClean.test_and_set(std::memory_order_acq_rel))
                UpdateSourceProps(source, voice, context);
        }
    );
}

SourceSubList::~SourceSubList()
{
    ALuint64 usemask = ~FreeMask;
    while(usemask)
    {
        ALsizei idx{CTZ64(usemask)};
        Sources[idx].~ALsource();
        usemask &= ~(U64(1) << idx);
    }
    FreeMask = ~usemask;
    al_free(Sources);
    Sources = nullptr;
}
