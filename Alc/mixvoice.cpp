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

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cassert>

#include <numeric>
#include <algorithm>

#include "AL/al.h"
#include "AL/alc.h"

#include "alMain.h"
#include "alcontext.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alListener.h"
#include "alAuxEffectSlot.h"
#include "sample_cvt.h"
#include "alu.h"
#include "alconfig.h"
#include "ringbuffer.h"

#include "cpu_caps.h"
#include "mixer/defs.h"


static_assert((INT_MAX>>FRACTIONBITS)/MAX_PITCH > BUFFERSIZE,
              "MAX_PITCH and/or BUFFERSIZE are too large for FRACTIONBITS!");

/* BSinc24 requires up to 23 extra samples before the current position, and 24 after. */
static_assert(MAX_RESAMPLE_PADDING >= 24, "MAX_RESAMPLE_PADDING must be at least 24!");


Resampler ResamplerDefault = LinearResampler;

MixerFunc MixSamples = Mix_<CTag>;
RowMixerFunc MixRowSamples = MixRow_<CTag>;
static HrtfMixerFunc MixHrtfSamples = MixHrtf_<CTag>;
static HrtfMixerBlendFunc MixHrtfBlendSamples = MixHrtfBlend_<CTag>;

static MixerFunc SelectMixer()
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return Mix_<NEONTag>;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return Mix_<SSETag>;
#endif
    return Mix_<CTag>;
}

static RowMixerFunc SelectRowMixer()
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixRow_<NEONTag>;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixRow_<SSETag>;
#endif
    return MixRow_<CTag>;
}

static inline HrtfMixerFunc SelectHrtfMixer()
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtf_<NEONTag>;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtf_<SSETag>;
#endif
    return MixHrtf_<CTag>;
}

static inline HrtfMixerBlendFunc SelectHrtfBlendMixer()
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtfBlend_<NEONTag>;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtfBlend_<SSETag>;
#endif
    return MixHrtfBlend_<CTag>;
}

ResamplerFunc SelectResampler(Resampler resampler)
{
    switch(resampler)
    {
        case PointResampler:
            return Resample_<PointTag,CTag>;
        case LinearResampler:
#ifdef HAVE_NEON
            if((CPUCapFlags&CPU_CAP_NEON))
                return Resample_<LerpTag,NEONTag>;
#endif
#ifdef HAVE_SSE4_1
            if((CPUCapFlags&CPU_CAP_SSE4_1))
                return Resample_<LerpTag,SSE4Tag>;
#endif
#ifdef HAVE_SSE2
            if((CPUCapFlags&CPU_CAP_SSE2))
                return Resample_<LerpTag,SSE2Tag>;
#endif
            return Resample_<LerpTag,CTag>;
        case FIR4Resampler:
            return Resample_<CubicTag,CTag>;
        case BSinc12Resampler:
        case BSinc24Resampler:
#ifdef HAVE_NEON
            if((CPUCapFlags&CPU_CAP_NEON))
                return Resample_<BSincTag,NEONTag>;
#endif
#ifdef HAVE_SSE
            if((CPUCapFlags&CPU_CAP_SSE))
                return Resample_<BSincTag,SSETag>;
#endif
            return Resample_<BSincTag,CTag>;
    }

    return Resample_<PointTag,CTag>;
}


void aluInitMixer()
{
    const char *str;

    if(ConfigValueStr(nullptr, nullptr, "resampler", &str))
    {
        if(strcasecmp(str, "point") == 0 || strcasecmp(str, "none") == 0)
            ResamplerDefault = PointResampler;
        else if(strcasecmp(str, "linear") == 0)
            ResamplerDefault = LinearResampler;
        else if(strcasecmp(str, "cubic") == 0)
            ResamplerDefault = FIR4Resampler;
        else if(strcasecmp(str, "bsinc12") == 0)
            ResamplerDefault = BSinc12Resampler;
        else if(strcasecmp(str, "bsinc24") == 0)
            ResamplerDefault = BSinc24Resampler;
        else if(strcasecmp(str, "bsinc") == 0)
        {
            WARN("Resampler option \"%s\" is deprecated, using bsinc12\n", str);
            ResamplerDefault = BSinc12Resampler;
        }
        else if(strcasecmp(str, "sinc4") == 0 || strcasecmp(str, "sinc8") == 0)
        {
            WARN("Resampler option \"%s\" is deprecated, using cubic\n", str);
            ResamplerDefault = FIR4Resampler;
        }
        else
        {
            char *end;
            long n = strtol(str, &end, 0);
            if(*end == '\0' && (n == PointResampler || n == LinearResampler || n == FIR4Resampler))
                ResamplerDefault = static_cast<Resampler>(n);
            else
                WARN("Invalid resampler: %s\n", str);
        }
    }

    MixHrtfBlendSamples = SelectHrtfBlendMixer();
    MixHrtfSamples = SelectHrtfMixer();
    MixSamples = SelectMixer();
    MixRowSamples = SelectRowMixer();
}


namespace {

void SendSourceStoppedEvent(ALCcontext *context, ALuint id)
{
    ALbitfieldSOFT enabledevt{context->EnabledEvts.load(std::memory_order_acquire)};
    if(!(enabledevt&EventType_SourceStateChange)) return;

    RingBuffer *ring{context->AsyncEvents.get()};
    auto evt_vec = ring->getWriteVector();
    if(evt_vec.first.len < 1) return;

    AsyncEvent *evt{new (evt_vec.first.buf) AsyncEvent{EventType_SourceStateChange}};
    evt->u.srcstate.id = id;
    evt->u.srcstate.state = AL_STOPPED;

    ring->writeAdvance(1);
    context->EventSem.post();
}


const ALfloat *DoFilters(BiquadFilter *lpfilter, BiquadFilter *hpfilter,
    ALfloat *RESTRICT dst, const ALfloat *RESTRICT src, ALsizei numsamples, int type)
{
    switch(type)
    {
        case AF_None:
            lpfilter->passthru(numsamples);
            hpfilter->passthru(numsamples);
            break;

        case AF_LowPass:
            lpfilter->process(dst, src, numsamples);
            hpfilter->passthru(numsamples);
            return dst;
        case AF_HighPass:
            lpfilter->passthru(numsamples);
            hpfilter->process(dst, src, numsamples);
            return dst;

        case AF_BandPass:
            for(ALsizei i{0};i < numsamples;)
            {
                ALfloat temp[256];
                ALsizei todo = mini(256, numsamples-i);

                lpfilter->process(temp, src+i, todo);
                hpfilter->process(dst+i, temp, todo);
                i += todo;
            }
            return dst;
    }
    return src;
}


/* Base template left undefined. Should be marked =delete, but Clang 3.8.1
 * chokes on that given the inline specializations.
 */
template<FmtType T>
inline ALfloat LoadSample(typename FmtTypeTraits<T>::Type val);

template<> inline ALfloat LoadSample<FmtUByte>(FmtTypeTraits<FmtUByte>::Type val)
{ return (val-128) * (1.0f/128.0f); }
template<> inline ALfloat LoadSample<FmtShort>(FmtTypeTraits<FmtShort>::Type val)
{ return val * (1.0f/32768.0f); }
template<> inline ALfloat LoadSample<FmtFloat>(FmtTypeTraits<FmtFloat>::Type val)
{ return val; }
template<> inline ALfloat LoadSample<FmtDouble>(FmtTypeTraits<FmtDouble>::Type val)
{ return static_cast<ALfloat>(val); }
template<> inline ALfloat LoadSample<FmtMulaw>(FmtTypeTraits<FmtMulaw>::Type val)
{ return muLawDecompressionTable[val] * (1.0f/32768.0f); }
template<> inline ALfloat LoadSample<FmtAlaw>(FmtTypeTraits<FmtAlaw>::Type val)
{ return aLawDecompressionTable[val] * (1.0f/32768.0f); }

template<FmtType T>
inline void LoadSampleArray(ALfloat *RESTRICT dst, const void *src, ALint srcstep,
    const ptrdiff_t samples)
{
    using SampleType = typename FmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(ALsizei i{0};i < samples;i++)
        dst[i] += LoadSample<T>(ssrc[i*srcstep]);
}

void LoadSamples(ALfloat *RESTRICT dst, const ALvoid *RESTRICT src, ALint srcstep, FmtType srctype,
    const ptrdiff_t samples)
{
#define HANDLE_FMT(T)  case T: LoadSampleArray<T>(dst, src, srcstep, samples); break
    switch(srctype)
    {
        HANDLE_FMT(FmtUByte);
        HANDLE_FMT(FmtShort);
        HANDLE_FMT(FmtFloat);
        HANDLE_FMT(FmtDouble);
        HANDLE_FMT(FmtMulaw);
        HANDLE_FMT(FmtAlaw);
    }
#undef HANDLE_FMT
}

ALfloat *LoadBufferStatic(ALbufferlistitem *BufferListItem, ALbufferlistitem *&BufferLoopItem,
    const ALsizei NumChannels, const ALsizei SampleSize, const ALsizei chan, ALsizei DataPosInt,
    ALfloat *SrcData, const ALfloat *const SrcDataEnd)
{
    /* TODO: For static sources, loop points are taken from the first buffer
     * (should be adjusted by any buffer offset, to possibly be added later).
     */
    const ALbuffer *Buffer0{BufferListItem->buffers[0]};
    const ALsizei LoopStart{Buffer0->LoopStart};
    const ALsizei LoopEnd{Buffer0->LoopEnd};
    ASSUME(LoopStart >= 0);
    ASSUME(LoopEnd > LoopStart);

    /* If current pos is beyond the loop range, do not loop */
    if(!BufferLoopItem || DataPosInt >= LoopEnd)
    {
        const ptrdiff_t SizeToDo{SrcDataEnd - SrcData};
        ASSUME(SizeToDo > 0);

        BufferLoopItem = nullptr;

        auto load_buffer = [DataPosInt,SrcData,NumChannels,SampleSize,chan,SizeToDo](ptrdiff_t CompLen, const ALbuffer *buffer) -> ptrdiff_t
        {
            if(DataPosInt >= buffer->SampleLen)
                return CompLen;

            /* Load what's left to play from the buffer */
            const ptrdiff_t DataSize{std::min<ptrdiff_t>(SizeToDo, buffer->SampleLen-DataPosInt)};
            CompLen = std::max<ptrdiff_t>(CompLen, DataSize);

            const ALbyte *Data{buffer->mData.data()};
            Data += (DataPosInt*NumChannels + chan)*SampleSize;

            LoadSamples(SrcData, Data, NumChannels, buffer->mFmtType, DataSize);
            return CompLen;
        };
        /* It's impossible to have a buffer list item with no entries. */
        ASSUME(BufferListItem->num_buffers > 0);
        auto buffers_end = BufferListItem->buffers + BufferListItem->num_buffers;
        SrcData += std::accumulate(BufferListItem->buffers, buffers_end, ptrdiff_t{0},
            load_buffer);
    }
    else
    {
        const ptrdiff_t SizeToDo{std::min<ptrdiff_t>(SrcDataEnd-SrcData, LoopEnd-DataPosInt)};
        ASSUME(SizeToDo > 0);

        auto load_buffer = [DataPosInt,SrcData,NumChannels,SampleSize,chan,SizeToDo](ptrdiff_t CompLen, const ALbuffer *buffer) -> ptrdiff_t
        {
            if(DataPosInt >= buffer->SampleLen)
                return CompLen;

            /* Load what's left of this loop iteration */
            const ptrdiff_t DataSize{std::min<ptrdiff_t>(SizeToDo, buffer->SampleLen-DataPosInt)};
            CompLen = std::max<ptrdiff_t>(CompLen, DataSize);

            const ALbyte *Data{buffer->mData.data()};
            Data += (DataPosInt*NumChannels + chan)*SampleSize;

            LoadSamples(SrcData, Data, NumChannels, buffer->mFmtType, DataSize);
            return CompLen;
        };
        ASSUME(BufferListItem->num_buffers > 0);
        auto buffers_end = BufferListItem->buffers + BufferListItem->num_buffers;
        SrcData += std::accumulate(BufferListItem->buffers, buffers_end, ptrdiff_t{0},
            load_buffer);

        const auto LoopSize = static_cast<ptrdiff_t>(LoopEnd - LoopStart);
        while(SrcData != SrcDataEnd)
        {
            const ptrdiff_t SizeToDo{std::min<ptrdiff_t>(SrcDataEnd-SrcData, LoopSize)};
            ASSUME(SizeToDo > 0);

            auto load_buffer_loop = [LoopStart,SrcData,NumChannels,SampleSize,chan,SizeToDo](ptrdiff_t CompLen, const ALbuffer *buffer) -> ptrdiff_t
            {
                if(LoopStart >= buffer->SampleLen)
                    return CompLen;

                const ptrdiff_t DataSize{std::min<ptrdiff_t>(SizeToDo,
                    buffer->SampleLen-LoopStart)};
                CompLen = std::max<ptrdiff_t>(CompLen, DataSize);

                const ALbyte *Data{buffer->mData.data()};
                Data += (LoopStart*NumChannels + chan)*SampleSize;

                LoadSamples(SrcData, Data, NumChannels, buffer->mFmtType, DataSize);
                return CompLen;
            };
            SrcData += std::accumulate(BufferListItem->buffers, buffers_end, ptrdiff_t{0},
                load_buffer_loop);
        }
    }
    return SrcData;
}

ALfloat *LoadBufferQueue(ALbufferlistitem *BufferListItem, ALbufferlistitem *BufferLoopItem,
    const ALsizei NumChannels, const ALsizei SampleSize, const ALsizei chan, ALsizei DataPosInt,
    ALfloat *SrcData, const ALfloat *const SrcDataEnd)
{
    /* Crawl the buffer queue to fill in the temp buffer */
    while(BufferListItem && SrcData != SrcDataEnd)
    {
        if(DataPosInt >= BufferListItem->max_samples)
        {
            DataPosInt -= BufferListItem->max_samples;
            BufferListItem = BufferListItem->next.load(std::memory_order_acquire);
            if(!BufferListItem) BufferListItem = BufferLoopItem;
            continue;
        }

        const ptrdiff_t SizeToDo{SrcDataEnd - SrcData};
        ASSUME(SizeToDo > 0);
        auto load_buffer = [DataPosInt,SrcData,NumChannels,SampleSize,chan,SizeToDo](ptrdiff_t CompLen, const ALbuffer *buffer) -> ptrdiff_t
        {
            if(!buffer) return CompLen;
            if(DataPosInt >= buffer->SampleLen)
                return CompLen;

            const ptrdiff_t DataSize{std::min<ptrdiff_t>(SizeToDo, buffer->SampleLen-DataPosInt)};
            CompLen = std::max<ptrdiff_t>(CompLen, DataSize);

            const ALbyte *Data{buffer->mData.data()};
            Data += (DataPosInt*NumChannels + chan)*SampleSize;

            LoadSamples(SrcData, Data, NumChannels, buffer->mFmtType, DataSize);
            return CompLen;
        };
        ASSUME(BufferListItem->num_buffers > 0);
        auto buffers_end = BufferListItem->buffers + BufferListItem->num_buffers;
        SrcData += std::accumulate(BufferListItem->buffers, buffers_end, ptrdiff_t{0u},
            load_buffer);

        if(SrcData == SrcDataEnd)
            break;
        DataPosInt = 0;
        BufferListItem = BufferListItem->next.load(std::memory_order_acquire);
        if(!BufferListItem) BufferListItem = BufferLoopItem;
    }

    return SrcData;
}

} // namespace

void MixVoice(ALvoice *voice, ALvoice::State vstate, const ALuint SourceID, ALCcontext *Context, const ALsizei SamplesToDo)
{
    static constexpr ALfloat SilentTarget[MAX_OUTPUT_CHANNELS]{};

    ASSUME(SamplesToDo > 0);

    /* Get voice info */
    const bool isstatic{(voice->mFlags&VOICE_IS_STATIC) != 0};
    ALsizei DataPosInt{static_cast<ALsizei>(voice->mPosition.load(std::memory_order_relaxed))};
    ALsizei DataPosFrac{voice->mPositionFrac.load(std::memory_order_relaxed)};
    ALbufferlistitem *BufferListItem{voice->mCurrentBuffer.load(std::memory_order_relaxed)};
    ALbufferlistitem *BufferLoopItem{voice->mLoopBuffer.load(std::memory_order_relaxed)};
    const ALsizei NumChannels{voice->mNumChannels};
    const ALsizei SampleSize{voice->mSampleSize};
    const ALint increment{voice->mStep};

    ASSUME(DataPosInt >= 0);
    ASSUME(DataPosFrac >= 0);
    ASSUME(NumChannels > 0);
    ASSUME(SampleSize > 0);
    ASSUME(increment > 0);

    ALCdevice *Device{Context->Device};
    const ALsizei IrSize{Device->mHrtf ? Device->mHrtf->irSize : 0};

    ASSUME(IrSize >= 0);

    ResamplerFunc Resample{(increment == FRACTIONONE && DataPosFrac == 0) ?
                           Resample_<CopyTag,CTag> : voice->mResampler};

    ALsizei Counter{(voice->mFlags&VOICE_IS_FADING) ? SamplesToDo : 0};
    if(!Counter)
    {
        /* No fading, just overwrite the old/current params. */
        for(ALsizei chan{0};chan < NumChannels;chan++)
        {
            DirectParams &parms = voice->mDirect.Params[chan];
            if(!(voice->mFlags&VOICE_HAS_HRTF))
                std::copy(std::begin(parms.Gains.Target), std::end(parms.Gains.Target),
                    std::begin(parms.Gains.Current));
            else
                parms.Hrtf.Old = parms.Hrtf.Target;
            auto set_current = [chan](ALvoice::SendData &send) -> void
            {
                if(!send.Buffer)
                    return;

                SendParams &parms = send.Params[chan];
                std::copy(std::begin(parms.Gains.Target), std::end(parms.Gains.Target),
                    std::begin(parms.Gains.Current));
            };
            std::for_each(voice->mSend.begin(), voice->mSend.end(), set_current);
        }
    }
    else if((voice->mFlags&VOICE_HAS_HRTF))
    {
        for(ALsizei chan{0};chan < NumChannels;chan++)
        {
            DirectParams &parms = voice->mDirect.Params[chan];
            if(!(parms.Hrtf.Old.Gain > GAIN_SILENCE_THRESHOLD))
            {
                /* The old HRTF params are silent, so overwrite the old
                 * coefficients with the new, and reset the old gain to 0. The
                 * future mix will then fade from silence.
                 */
                parms.Hrtf.Old = parms.Hrtf.Target;
                parms.Hrtf.Old.Gain = 0.0f;
            }
        }
    }

    ALsizei buffers_done{0};
    ALsizei OutPos{0};
    do {
        /* Figure out how many buffer samples will be needed */
        ALsizei DstBufferSize{SamplesToDo - OutPos};

        /* Calculate the last written dst sample pos. */
        int64_t DataSize64{DstBufferSize - 1};
        /* Calculate the last read src sample pos. */
        DataSize64 = (DataSize64*increment + DataPosFrac) >> FRACTIONBITS;
        /* +1 to get the src sample count, include padding. */
        DataSize64 += 1 + MAX_RESAMPLE_PADDING*2;

        auto SrcBufferSize = static_cast<ALsizei>(
            mini64(DataSize64, BUFFERSIZE + MAX_RESAMPLE_PADDING*2 + 1));
        if(SrcBufferSize > BUFFERSIZE + MAX_RESAMPLE_PADDING*2)
        {
            SrcBufferSize = BUFFERSIZE + MAX_RESAMPLE_PADDING*2;
            /* If the source buffer got saturated, we can't fill the desired
             * dst size. Figure out how many samples we can actually mix from
             * this.
             */
            DataSize64 = SrcBufferSize - MAX_RESAMPLE_PADDING*2;
            DataSize64 = ((DataSize64<<FRACTIONBITS) - DataPosFrac + increment-1) / increment;
            DstBufferSize = static_cast<ALsizei>(mini64(DataSize64, DstBufferSize));

            /* Some mixers like having a multiple of 4, so try to give that
             * unless this is the last update.
             */
            if(DstBufferSize < SamplesToDo-OutPos)
                DstBufferSize &= ~3;
        }

        for(ALsizei chan{0};chan < NumChannels;chan++)
        {
            auto &SrcData = Device->SourceData;

            /* Load the previous samples into the source data first, and clear the rest. */
            auto srciter = std::copy_n(voice->mPrevSamples[chan].begin(), MAX_RESAMPLE_PADDING,
                std::begin(SrcData));
            std::fill(srciter, std::end(SrcData), 0.0f);

            auto srcdata_end = std::begin(SrcData) + SrcBufferSize;
            if(!BufferListItem)
                srciter = std::copy(voice->mPrevSamples[chan].begin()+MAX_RESAMPLE_PADDING,
                    voice->mPrevSamples[chan].end(), srciter);
            else if(isstatic)
                srciter = LoadBufferStatic(BufferListItem, BufferLoopItem, NumChannels,
                    SampleSize, chan, DataPosInt, srciter, srcdata_end);
            else
                srciter = LoadBufferQueue(BufferListItem, BufferLoopItem, NumChannels,
                    SampleSize, chan, DataPosInt, srciter, srcdata_end);

            if(UNLIKELY(srciter != srcdata_end))
            {
                /* If the source buffer wasn't filled, copy the last sample and
                 * fade it to 0 amplitude. Ideally it should have ended with
                 * silence, but if not this should help avoid clicks from
                 * sudden amplitude changes.
                 */
                const ALfloat sample{*(srciter-1)};
                const ALfloat gainstep{1.0f / (BUFFERSIZE*2)};
                ALfloat step{BUFFERSIZE*2};

                while(srciter != srcdata_end)
                {
                    step -= 1.0f;
                    *(srciter++) = sample * gainstep*step;
                }
            }

            /* Store the last source samples used for next time. */
            std::copy_n(&SrcData[(increment*DstBufferSize + DataPosFrac)>>FRACTIONBITS],
                voice->mPrevSamples[chan].size(), std::begin(voice->mPrevSamples[chan]));

            /* Resample, then apply ambisonic upsampling as needed. */
            const ALfloat *ResampledData{Resample(&voice->mResampleState,
                &SrcData[MAX_RESAMPLE_PADDING], DataPosFrac, increment,
                Device->ResampledData, DstBufferSize)};
            if((voice->mFlags&VOICE_IS_AMBISONIC))
            {
                const ALfloat hfscale{voice->mAmbiScales[chan]};
                /* Beware the evil const_cast. It's safe since it's pointing to
                 * either SrcData or Device->ResampledData (both non-const),
                 * but the resample method takes its input as const float* and
                 * may return it without copying to output, making it currently
                 * unavoidable.
                 */
                voice->mAmbiSplitter[chan].applyHfScale(const_cast<ALfloat*>(ResampledData),
                    hfscale, DstBufferSize);
            }

            /* Now filter and mix to the appropriate outputs. */
            {
                DirectParams &parms = voice->mDirect.Params[chan];
                const ALfloat *samples{DoFilters(&parms.LowPass, &parms.HighPass,
                    Device->FilteredData, ResampledData, DstBufferSize,
                    voice->mDirect.FilterType)};

                if((voice->mFlags&VOICE_HAS_HRTF))
                {
                    const int OutLIdx{GetChannelIdxByName(Device->RealOut, FrontLeft)};
                    const int OutRIdx{GetChannelIdxByName(Device->RealOut, FrontRight)};
                    ASSUME(OutLIdx >= 0 && OutRIdx >= 0);

                    auto &HrtfSamples = Device->HrtfSourceData;
                    auto &AccumSamples = Device->HrtfAccumData;
                    const ALfloat TargetGain{UNLIKELY(vstate == ALvoice::Stopping) ? 0.0f :
                        parms.Hrtf.Target.Gain};
                    ALsizei fademix{0};

                    /* Copy the HRTF history and new input samples into a temp
                     * buffer.
                     */
                    auto src_iter = std::copy(parms.Hrtf.State.History.begin(),
                        parms.Hrtf.State.History.end(), std::begin(HrtfSamples));
                    std::copy_n(samples, DstBufferSize, src_iter);
                    /* Copy the last used samples back into the history buffer
                     * for later.
                     */
                    std::copy_n(std::begin(HrtfSamples) + DstBufferSize,
                        parms.Hrtf.State.History.size(), parms.Hrtf.State.History.begin());

                    /* Copy the current filtered values being accumulated into
                     * the temp buffer.
                     */
                    auto accum_iter = std::copy_n(parms.Hrtf.State.Values.begin(),
                        parms.Hrtf.State.Values.size(), std::begin(AccumSamples));

                    /* Clear the accumulation buffer that will start getting
                     * filled in.
                     */
                    std::fill_n(accum_iter, DstBufferSize, float2{});

                    /* If fading, the old gain is not silence, and this is the
                     * first mixing pass, fade between the IRs.
                     */
                    if(Counter && (parms.Hrtf.Old.Gain > GAIN_SILENCE_THRESHOLD) && OutPos == 0)
                    {
                        fademix = mini(DstBufferSize, 128);

                        ALfloat gain{TargetGain};

                        /* The new coefficients need to fade in completely
                         * since they're replacing the old ones. To keep the
                         * gain fading consistent, interpolate between the old
                         * and new target gains given how much of the fade time
                         * this mix handles.
                         */
                        if(LIKELY(Counter > fademix))
                        {
                            const ALfloat a{static_cast<ALfloat>(fademix) /
                                static_cast<ALfloat>(Counter)};
                            gain = lerp(parms.Hrtf.Old.Gain, TargetGain, a);
                        }
                        MixHrtfParams hrtfparams;
                        hrtfparams.Coeffs = &parms.Hrtf.Target.Coeffs;
                        hrtfparams.Delay[0] = parms.Hrtf.Target.Delay[0];
                        hrtfparams.Delay[1] = parms.Hrtf.Target.Delay[1];
                        hrtfparams.Gain = 0.0f;
                        hrtfparams.GainStep = gain / static_cast<ALfloat>(fademix);

                        MixHrtfBlendSamples(
                            voice->mDirect.Buffer[OutLIdx], voice->mDirect.Buffer[OutRIdx],
                            HrtfSamples, AccumSamples, OutPos, IrSize, &parms.Hrtf.Old,
                            &hrtfparams, fademix);
                        /* Update the old parameters with the result. */
                        parms.Hrtf.Old = parms.Hrtf.Target;
                        if(fademix < Counter)
                            parms.Hrtf.Old.Gain = hrtfparams.Gain;
                        else
                            parms.Hrtf.Old.Gain = TargetGain;
                    }

                    if(LIKELY(fademix < DstBufferSize))
                    {
                        const ALsizei todo{DstBufferSize - fademix};
                        ALfloat gain{TargetGain};

                        /* Interpolate the target gain if the gain fading lasts
                         * longer than this mix.
                         */
                        if(Counter > DstBufferSize)
                        {
                            const ALfloat a{static_cast<ALfloat>(todo) /
                                static_cast<ALfloat>(Counter-fademix)};
                            gain = lerp(parms.Hrtf.Old.Gain, TargetGain, a);
                        }

                        MixHrtfParams hrtfparams;
                        hrtfparams.Coeffs = &parms.Hrtf.Target.Coeffs;
                        hrtfparams.Delay[0] = parms.Hrtf.Target.Delay[0];
                        hrtfparams.Delay[1] = parms.Hrtf.Target.Delay[1];
                        hrtfparams.Gain = parms.Hrtf.Old.Gain;
                        hrtfparams.GainStep = (gain - parms.Hrtf.Old.Gain) /
                            static_cast<ALfloat>(todo);
                        MixHrtfSamples(
                            voice->mDirect.Buffer[OutLIdx], voice->mDirect.Buffer[OutRIdx],
                            HrtfSamples+fademix, AccumSamples+fademix, OutPos+fademix, IrSize,
                            &hrtfparams, todo);
                        /* Store the interpolated gain or the final target gain
                         * depending if the fade is done.
                         */
                        if(DstBufferSize < Counter)
                            parms.Hrtf.Old.Gain = gain;
                        else
                            parms.Hrtf.Old.Gain = TargetGain;
                    }

                    /* Copy the new in-progress accumulation values back for
                     * the next mix.
                     */
                    std::copy_n(std::begin(AccumSamples) + DstBufferSize,
                        parms.Hrtf.State.Values.size(), parms.Hrtf.State.Values.begin());
                }
                else if((voice->mFlags&VOICE_HAS_NFC))
                {
                    const ALfloat *TargetGains{UNLIKELY(vstate == ALvoice::Stopping) ?
                        SilentTarget : parms.Gains.Target};

                    MixSamples(samples, voice->mDirect.ChannelsPerOrder[0],
                        voice->mDirect.Buffer, parms.Gains.Current, TargetGains, Counter,
                        OutPos, DstBufferSize);

                    ALfloat (&nfcsamples)[BUFFERSIZE] = Device->NfcSampleData;
                    ALsizei chanoffset{voice->mDirect.ChannelsPerOrder[0]};
                    using FilterProc = void (NfcFilter::*)(float*,const float*,int);
                    auto apply_nfc = [voice,&parms,samples,TargetGains,DstBufferSize,Counter,OutPos,&chanoffset,&nfcsamples](FilterProc process, ALsizei order) -> void
                    {
                        if(voice->mDirect.ChannelsPerOrder[order] < 1)
                            return;
                        (parms.NFCtrlFilter.*process)(nfcsamples, samples, DstBufferSize);
                        MixSamples(nfcsamples, voice->mDirect.ChannelsPerOrder[order],
                            voice->mDirect.Buffer+chanoffset, parms.Gains.Current+chanoffset,
                            TargetGains+chanoffset, Counter, OutPos, DstBufferSize);
                        chanoffset += voice->mDirect.ChannelsPerOrder[order];
                    };
                    apply_nfc(&NfcFilter::process1, 1);
                    apply_nfc(&NfcFilter::process2, 2);
                    apply_nfc(&NfcFilter::process3, 3);
                }
                else
                {
                    const ALfloat *TargetGains{UNLIKELY(vstate == ALvoice::Stopping) ?
                        SilentTarget : parms.Gains.Target};
                    MixSamples(samples, voice->mDirect.Channels, voice->mDirect.Buffer,
                        parms.Gains.Current, TargetGains, Counter, OutPos, DstBufferSize);
                }
            }

            ALfloat (&FilterBuf)[BUFFERSIZE] = Device->FilteredData;
            auto mix_send = [vstate,Counter,OutPos,DstBufferSize,chan,ResampledData,&FilterBuf](ALvoice::SendData &send) -> void
            {
                if(!send.Buffer)
                    return;

                SendParams &parms = send.Params[chan];
                const ALfloat *samples{DoFilters(&parms.LowPass, &parms.HighPass,
                    FilterBuf, ResampledData, DstBufferSize, send.FilterType)};

                const ALfloat *TargetGains{UNLIKELY(vstate==ALvoice::Stopping) ? SilentTarget :
                    parms.Gains.Target};
                MixSamples(samples, send.Channels, send.Buffer, parms.Gains.Current,
                    TargetGains, Counter, OutPos, DstBufferSize);
            };
            std::for_each(voice->mSend.begin(), voice->mSend.end(), mix_send);
        }
        /* Update positions */
        DataPosFrac += increment*DstBufferSize;
        DataPosInt  += DataPosFrac>>FRACTIONBITS;
        DataPosFrac &= FRACTIONMASK;

        OutPos += DstBufferSize;
        Counter = maxi(DstBufferSize, Counter) - DstBufferSize;

        if(UNLIKELY(!BufferListItem))
        {
            /* Do nothing extra when there's no buffers. */
        }
        else if(isstatic)
        {
            if(BufferLoopItem)
            {
                /* Handle looping static source */
                const ALbuffer *Buffer{BufferListItem->buffers[0]};
                const ALsizei LoopStart{Buffer->LoopStart};
                const ALsizei LoopEnd{Buffer->LoopEnd};
                if(DataPosInt >= LoopEnd)
                {
                    assert(LoopEnd > LoopStart);
                    DataPosInt = ((DataPosInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                }
            }
            else
            {
                /* Handle non-looping static source */
                if(DataPosInt >= BufferListItem->max_samples)
                {
                    if(LIKELY(vstate == ALvoice::Playing))
                        vstate = ALvoice::Stopped;
                    BufferListItem = nullptr;
                    break;
                }
            }
        }
        else while(1)
        {
            /* Handle streaming source */
            if(BufferListItem->max_samples > DataPosInt)
                break;

            DataPosInt -= BufferListItem->max_samples;

            buffers_done += BufferListItem->num_buffers;
            BufferListItem = BufferListItem->next.load(std::memory_order_relaxed);
            if(!BufferListItem && !(BufferListItem=BufferLoopItem))
            {
                if(LIKELY(vstate == ALvoice::Playing))
                    vstate = ALvoice::Stopped;
                break;
            }
        }
    } while(OutPos < SamplesToDo);

    voice->mFlags |= VOICE_IS_FADING;

    /* Don't update positions and buffers if we were stopping. */
    if(UNLIKELY(vstate == ALvoice::Stopping))
    {
        voice->mPlayState.store(ALvoice::Stopped, std::memory_order_release);
        return;
    }

    /* Update voice info */
    voice->mPosition.store(DataPosInt, std::memory_order_relaxed);
    voice->mPositionFrac.store(DataPosFrac, std::memory_order_relaxed);
    voice->mCurrentBuffer.store(BufferListItem, std::memory_order_relaxed);
    if(vstate == ALvoice::Stopped)
    {
        voice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
        voice->mSourceID.store(0u, std::memory_order_relaxed);
    }
    std::atomic_thread_fence(std::memory_order_release);

    /* Send any events now, after the position/buffer info was updated. */
    ALbitfieldSOFT enabledevt{Context->EnabledEvts.load(std::memory_order_acquire)};
    if(buffers_done > 0 && (enabledevt&EventType_BufferCompleted))
    {
        RingBuffer *ring{Context->AsyncEvents.get()};
        auto evt_vec = ring->getWriteVector();
        if(evt_vec.first.len > 0)
        {
            AsyncEvent *evt{new (evt_vec.first.buf) AsyncEvent{EventType_BufferCompleted}};
            evt->u.bufcomp.id = SourceID;
            evt->u.bufcomp.count = buffers_done;
            ring->writeAdvance(1);
            Context->EventSem.post();
        }
    }

    if(vstate == ALvoice::Stopped)
    {
        /* If the voice just ended, set it to Stopping so the next render
         * ensures any residual noise fades to 0 amplitude.
         */
        voice->mPlayState.store(ALvoice::Stopping, std::memory_order_release);
        SendSourceStoppedEvent(Context, SourceID);
    }
}
