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
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alListener.h"
#include "alAuxEffectSlot.h"
#include "alu.h"

#include "mixer_defs.h"


extern inline void InitiatePositionArrays(ALuint frac, ALuint increment, ALuint *frac_arr, ALuint *pos_arr, ALuint size);


static inline HrtfMixerFunc SelectHrtfMixer(void)
{
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtf_SSE;
#endif
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtf_Neon;
#endif

    return MixHrtf_C;
}

static inline MixerFunc SelectMixer(void)
{
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return Mix_SSE;
#endif
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return Mix_Neon;
#endif

    return Mix_C;
}

static inline ResamplerFunc SelectResampler(enum Resampler Resampler, ALuint increment)
{
    if(increment == FRACTIONONE)
        return Resample_copy32_C;
    switch(Resampler)
    {
        case PointResampler:
            return Resample_point32_C;
        case LinearResampler:
#ifdef HAVE_SSE4_1
            if((CPUCapFlags&CPU_CAP_SSE4_1))
                return Resample_lerp32_SSE41;
#endif
#ifdef HAVE_SSE2
            if((CPUCapFlags&CPU_CAP_SSE2))
                return Resample_lerp32_SSE2;
#endif
            return Resample_lerp32_C;
        case CubicResampler:
            return Resample_cubic32_C;
        case ResamplerMax:
            /* Shouldn't happen */
            break;
    }

    return Resample_point32_C;
}


static inline ALfloat Sample_ALbyte(ALbyte val)
{ return val * (1.0f/127.0f); }

static inline ALfloat Sample_ALshort(ALshort val)
{ return val * (1.0f/32767.0f); }

static inline ALfloat Sample_ALfloat(ALfloat val)
{ return val; }

#define DECL_TEMPLATE(T)                                                      \
static void Load_##T(ALfloat *dst, const T *src, ALuint srcstep, ALuint samples)\
{                                                                             \
    ALuint i;                                                                 \
    for(i = 0;i < samples;i++)                                                \
        dst[i] = Sample_##T(src[i*srcstep]);                                  \
}

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALfloat)

#undef DECL_TEMPLATE

static void LoadSamples(ALfloat *dst, const ALvoid *src, ALuint srcstep, enum FmtType srctype, ALuint samples)
{
    switch(srctype)
    {
        case FmtByte:
            Load_ALbyte(dst, src, srcstep, samples);
            break;
        case FmtShort:
            Load_ALshort(dst, src, srcstep, samples);
            break;
        case FmtFloat:
            Load_ALfloat(dst, src, srcstep, samples);
            break;
    }
}

static void SilenceSamples(ALfloat *dst, ALuint samples)
{
    ALuint i;
    for(i = 0;i < samples;i++)
        dst[i] = 0.0f;
}


static const ALfloat *DoFilters(ALfilterState *lpfilter, ALfilterState *hpfilter,
                                ALfloat *restrict dst, const ALfloat *restrict src,
                                ALuint numsamples, enum ActiveFilters type)
{
    ALuint i;
    switch(type)
    {
        case AF_None:
            break;

        case AF_LowPass:
            ALfilterState_process(lpfilter, dst, src, numsamples);
            return dst;
        case AF_HighPass:
            ALfilterState_process(hpfilter, dst, src, numsamples);
            return dst;

        case AF_BandPass:
            for(i = 0;i < numsamples;)
            {
                ALfloat temp[64];
                ALuint todo = minu(64, numsamples-i);

                ALfilterState_process(lpfilter, temp, src+i, todo);
                ALfilterState_process(hpfilter, dst+i, temp, todo);
                i += todo;
            }
            return dst;
    }
    return src;
}


ALvoid MixSource(ALactivesource *src, ALCdevice *Device, ALuint SamplesToDo)
{
    MixerFunc Mix;
    HrtfMixerFunc HrtfMix;
    ResamplerFunc Resample;
    ALsource *Source = src->Source;
    ALbufferlistitem *BufferListItem;
    ALuint DataPosInt, DataPosFrac;
    ALboolean Looping;
    ALuint increment;
    enum Resampler Resampler;
    ALenum State;
    ALuint OutPos;
    ALuint NumChannels;
    ALuint SampleSize;
    ALint64 DataSize64;
    ALuint chan, j;

    /* Get source info */
    State          = Source->state;
    BufferListItem = ATOMIC_LOAD(&Source->current_buffer);
    DataPosInt     = Source->position;
    DataPosFrac    = Source->position_fraction;
    Looping        = Source->Looping;
    increment      = src->Step;
    Resampler      = (increment==FRACTIONONE) ? PointResampler : Source->Resampler;
    NumChannels    = Source->NumChannels;
    SampleSize     = Source->SampleSize;

    Mix = SelectMixer();
    HrtfMix = SelectHrtfMixer();
    Resample = SelectResampler(Resampler, increment);

    OutPos = 0;
    do {
        const ALuint BufferPrePadding = ResamplerPrePadding[Resampler];
        const ALuint BufferPadding = ResamplerPadding[Resampler];
        ALuint SrcBufferSize, DstBufferSize;

        /* Figure out how many buffer samples will be needed */
        DataSize64  = SamplesToDo-OutPos;
        DataSize64 *= increment;
        DataSize64 += DataPosFrac+FRACTIONMASK;
        DataSize64 >>= FRACTIONBITS;
        DataSize64 += BufferPadding+BufferPrePadding;

        SrcBufferSize = (ALuint)mini64(DataSize64, BUFFERSIZE);

        /* Figure out how many samples we can actually mix from this. */
        DataSize64  = SrcBufferSize;
        DataSize64 -= BufferPadding+BufferPrePadding;
        DataSize64 <<= FRACTIONBITS;
        DataSize64 -= DataPosFrac;

        DstBufferSize = (ALuint)((DataSize64+(increment-1)) / increment);
        DstBufferSize = minu(DstBufferSize, (SamplesToDo-OutPos));

        /* Some mixers like having a multiple of 4, so try to give that unless
         * this is the last update. */
        if(OutPos+DstBufferSize < SamplesToDo)
            DstBufferSize &= ~3;

        for(chan = 0;chan < NumChannels;chan++)
        {
            const ALfloat *ResampledData;
            ALfloat *SrcData = Device->SourceData;
            ALuint SrcDataSize = 0;

            if(Source->SourceType == AL_STATIC)
            {
                const ALbuffer *ALBuffer = BufferListItem->buffer;
                const ALubyte *Data = ALBuffer->data;
                ALuint DataSize;
                ALuint pos;

                /* If current pos is beyond the loop range, do not loop */
                if(Looping == AL_FALSE || DataPosInt >= (ALuint)ALBuffer->LoopEnd)
                {
                    Looping = AL_FALSE;

                    if(DataPosInt >= BufferPrePadding)
                        pos = DataPosInt - BufferPrePadding;
                    else
                    {
                        DataSize = BufferPrePadding - DataPosInt;
                        DataSize = minu(SrcBufferSize - SrcDataSize, DataSize);

                        SilenceSamples(&SrcData[SrcDataSize], DataSize);
                        SrcDataSize += DataSize;

                        pos = 0;
                    }

                    /* Copy what's left to play in the source buffer, and clear the
                     * rest of the temp buffer */
                    DataSize = minu(SrcBufferSize - SrcDataSize, ALBuffer->SampleLen - pos);

                    LoadSamples(&SrcData[SrcDataSize], &Data[(pos*NumChannels + chan)*SampleSize],
                                NumChannels, ALBuffer->FmtType, DataSize);
                    SrcDataSize += DataSize;

                    SilenceSamples(&SrcData[SrcDataSize], SrcBufferSize - SrcDataSize);
                    SrcDataSize += SrcBufferSize - SrcDataSize;
                }
                else
                {
                    ALuint LoopStart = ALBuffer->LoopStart;
                    ALuint LoopEnd   = ALBuffer->LoopEnd;

                    if(DataPosInt >= LoopStart)
                    {
                        pos = DataPosInt-LoopStart;
                        while(pos < BufferPrePadding)
                            pos += LoopEnd-LoopStart;
                        pos -= BufferPrePadding;
                        pos += LoopStart;
                    }
                    else if(DataPosInt >= BufferPrePadding)
                        pos = DataPosInt - BufferPrePadding;
                    else
                    {
                        DataSize = BufferPrePadding - DataPosInt;
                        DataSize = minu(SrcBufferSize - SrcDataSize, DataSize);

                        SilenceSamples(&SrcData[SrcDataSize], DataSize);
                        SrcDataSize += DataSize;

                        pos = 0;
                    }

                    /* Copy what's left of this loop iteration, then copy repeats
                     * of the loop section */
                    DataSize = LoopEnd - pos;
                    DataSize = minu(SrcBufferSize - SrcDataSize, DataSize);

                    LoadSamples(&SrcData[SrcDataSize], &Data[(pos*NumChannels + chan)*SampleSize],
                                NumChannels, ALBuffer->FmtType, DataSize);
                    SrcDataSize += DataSize;

                    DataSize = LoopEnd-LoopStart;
                    while(SrcBufferSize > SrcDataSize)
                    {
                        DataSize = minu(SrcBufferSize - SrcDataSize, DataSize);

                        LoadSamples(&SrcData[SrcDataSize], &Data[(LoopStart*NumChannels + chan)*SampleSize],
                                    NumChannels, ALBuffer->FmtType, DataSize);
                        SrcDataSize += DataSize;
                    }
                }
            }
            else
            {
                /* Crawl the buffer queue to fill in the temp buffer */
                ALbufferlistitem *tmpiter = BufferListItem;
                ALuint pos;

                if(DataPosInt >= BufferPrePadding)
                    pos = DataPosInt - BufferPrePadding;
                else
                {
                    pos = BufferPrePadding - DataPosInt;
                    while(pos > 0)
                    {
                        ALbufferlistitem *prev;
                        if((prev=tmpiter->prev) != NULL)
                            tmpiter = prev;
                        else if(Looping)
                        {
                            while(tmpiter->next)
                                tmpiter = tmpiter->next;
                        }
                        else
                        {
                            ALuint DataSize = minu(SrcBufferSize - SrcDataSize, pos);

                            SilenceSamples(&SrcData[SrcDataSize], DataSize);
                            SrcDataSize += DataSize;

                            pos = 0;
                            break;
                        }

                        if(tmpiter->buffer)
                        {
                            if((ALuint)tmpiter->buffer->SampleLen > pos)
                            {
                                pos = tmpiter->buffer->SampleLen - pos;
                                break;
                            }
                            pos -= tmpiter->buffer->SampleLen;
                        }
                    }
                }

                while(tmpiter && SrcBufferSize > SrcDataSize)
                {
                    const ALbuffer *ALBuffer;
                    if((ALBuffer=tmpiter->buffer) != NULL)
                    {
                        const ALubyte *Data = ALBuffer->data;
                        ALuint DataSize = ALBuffer->SampleLen;

                        /* Skip the data already played */
                        if(DataSize <= pos)
                            pos -= DataSize;
                        else
                        {
                            Data += (pos*NumChannels + chan)*SampleSize;
                            DataSize -= pos;
                            pos -= pos;

                            DataSize = minu(SrcBufferSize - SrcDataSize, DataSize);
                            LoadSamples(&SrcData[SrcDataSize], Data, NumChannels,
                                        ALBuffer->FmtType, DataSize);
                            SrcDataSize += DataSize;
                        }
                    }
                    tmpiter = tmpiter->next;
                    if(!tmpiter && Looping)
                        tmpiter = ATOMIC_LOAD(&Source->queue);
                    else if(!tmpiter)
                    {
                        SilenceSamples(&SrcData[SrcDataSize], SrcBufferSize - SrcDataSize);
                        SrcDataSize += SrcBufferSize - SrcDataSize;
                    }
                }
            }

            /* Now resample, then filter and mix to the appropriate outputs. */
            ResampledData = Resample(
                &SrcData[BufferPrePadding], DataPosFrac, increment,
                Device->ResampledData, DstBufferSize
            );
            {
                DirectParams *parms = &src->Direct;
                const ALfloat *samples;

                samples = DoFilters(
                    &parms->Filters[chan].LowPass, &parms->Filters[chan].HighPass,
                    Device->FilteredData, ResampledData, DstBufferSize,
                    parms->Filters[chan].ActiveType
                );
                if(!src->IsHrtf)
                    Mix(samples, MaxChannels, parms->OutBuffer, parms->Mix.Gains[chan],
                        parms->Counter, OutPos, DstBufferSize);
                else
                    HrtfMix(parms->OutBuffer, samples, parms->Counter, src->Offset,
                            OutPos, parms->Mix.Hrtf.IrSize, &parms->Mix.Hrtf.Params[chan],
                            &parms->Mix.Hrtf.State[chan], DstBufferSize);
            }

            for(j = 0;j < Device->NumAuxSends;j++)
            {
                SendParams *parms = &src->Send[j];
                const ALfloat *samples;

                if(!parms->OutBuffer)
                    continue;

                samples = DoFilters(
                    &parms->Filters[chan].LowPass, &parms->Filters[chan].HighPass,
                    Device->FilteredData, ResampledData, DstBufferSize,
                    parms->Filters[chan].ActiveType
                );
                Mix(samples, 1, parms->OutBuffer, &parms->Gain,
                    parms->Counter, OutPos, DstBufferSize);
            }
        }
        /* Update positions */
        DataPosFrac += increment*DstBufferSize;
        DataPosInt  += DataPosFrac>>FRACTIONBITS;
        DataPosFrac &= FRACTIONMASK;

        OutPos += DstBufferSize;
        src->Offset += DstBufferSize;
        src->Direct.Counter = maxu(src->Direct.Counter, DstBufferSize) - DstBufferSize;
        for(j = 0;j < Device->NumAuxSends;j++)
            src->Send[j].Counter = maxu(src->Send[j].Counter, DstBufferSize) - DstBufferSize;

        /* Handle looping sources */
        while(1)
        {
            const ALbuffer *ALBuffer;
            ALuint DataSize = 0;
            ALuint LoopStart = 0;
            ALuint LoopEnd = 0;

            if((ALBuffer=BufferListItem->buffer) != NULL)
            {
                DataSize = ALBuffer->SampleLen;
                LoopStart = ALBuffer->LoopStart;
                LoopEnd = ALBuffer->LoopEnd;
                if(LoopEnd > DataPosInt)
                    break;
            }

            if(Looping && Source->SourceType == AL_STATIC)
            {
                assert(LoopEnd > LoopStart);
                DataPosInt = ((DataPosInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                break;
            }

            if(DataSize > DataPosInt)
                break;

            if(!(BufferListItem=BufferListItem->next))
            {
                if(Looping)
                    BufferListItem = ATOMIC_LOAD(&Source->queue);
                else
                {
                    State = AL_STOPPED;
                    BufferListItem = NULL;
                    DataPosInt = 0;
                    DataPosFrac = 0;
                    break;
                }
            }

            DataPosInt -= DataSize;
        }
    } while(State == AL_PLAYING && OutPos < SamplesToDo);

    /* Update source info */
    Source->state             = State;
    ATOMIC_STORE(&Source->current_buffer, BufferListItem);
    Source->position          = DataPosInt;
    Source->position_fraction = DataPosFrac;
}
