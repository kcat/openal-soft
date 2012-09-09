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
#include "bs2b.h"

#include "mixer_defs.h"


DryMixerFunc SelectDirectMixer(void)
{
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixDirect_SSE;
#endif
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixDirect_Neon;
#endif

    return MixDirect_C;
}

DryMixerFunc SelectHrtfMixer(void)
{
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixDirect_Hrtf_SSE;
#endif
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixDirect_Hrtf_Neon;
#endif

    return MixDirect_Hrtf_C;
}

WetMixerFunc SelectSendMixer(void)
{
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixSend_SSE;
#endif
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixSend_Neon;
#endif

    return MixSend_C;
}


static __inline ALfloat Sample_ALbyte(ALbyte val)
{ return val * (1.0f/127.0f); }

static __inline ALfloat Sample_ALshort(ALshort val)
{ return val * (1.0f/32767.0f); }

static __inline ALfloat Sample_ALfloat(ALfloat val)
{ return val; }

#define DECL_TEMPLATE(T)                                                      \
static void Load_##T(ALfloat *dst, const T *src, ALuint samples)              \
{                                                                             \
    ALuint i;                                                                 \
    for(i = 0;i < samples;i++)                                                \
        dst[i] = Sample_##T(src[i]);                                          \
}

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALfloat)

#undef DECL_TEMPLATE

static void LoadStack(ALfloat *dst, const ALvoid *src, enum FmtType srctype, ALuint samples)
{
    switch(srctype)
    {
        case FmtByte:
            Load_ALbyte(dst, src, samples);
            break;
        case FmtShort:
            Load_ALshort(dst, src, samples);
            break;
        case FmtFloat:
            Load_ALfloat(dst, src, samples);
            break;
    }
}

static void SilenceStack(ALfloat *dst, ALuint samples)
{
    ALuint i;
    for(i = 0;i < samples;i++)
        dst[i] = 0.0f;
}


static __inline ALfloat point32(const ALfloat *vals, ALint step, ALint frac)
{ return vals[0]; (void)step; (void)frac; }
static __inline ALfloat lerp32(const ALfloat *vals, ALint step, ALint frac)
{ return lerp(vals[0], vals[step], frac * (1.0f/FRACTIONONE)); }
static __inline ALfloat cubic32(const ALfloat *vals, ALint step, ALint frac)
{ return cubic(vals[-step], vals[0], vals[step], vals[step+step],
               frac * (1.0f/FRACTIONONE)); }

#define DECL_TEMPLATE(Sampler)                                                \
static void Resample_##Sampler(const ALfloat *data, ALuint frac,              \
  ALuint increment, ALuint NumChannels, ALfloat *RESTRICT OutBuffer,          \
  ALuint BufferSize)                                                          \
{                                                                             \
    ALuint pos = 0;                                                           \
    ALfloat value;                                                            \
    ALuint i;                                                                 \
                                                                              \
    for(i = 0;i < BufferSize+1;i++)                                           \
    {                                                                         \
        value = Sampler(data + pos*NumChannels, NumChannels, frac);           \
        OutBuffer[i] = value;                                                 \
                                                                              \
        frac += increment;                                                    \
        pos  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
    }                                                                         \
}

DECL_TEMPLATE(point32)
DECL_TEMPLATE(lerp32)
DECL_TEMPLATE(cubic32)

#undef DECL_TEMPLATE

static void Resample(enum Resampler Resampler, const ALfloat *data, ALuint frac,
                     ALuint increment, ALuint NumChannels,
                     ALfloat *RESTRICT OutBuffer, ALuint BufferSize)
{
    if(increment == FRACTIONONE)
        goto do_point;
    switch(Resampler)
    {
        case PointResampler:
        do_point:
            Resample_point32(data, frac, increment, NumChannels,
                             OutBuffer, BufferSize);
            break;
        case LinearResampler:
            Resample_lerp32(data, frac, increment, NumChannels,
                            OutBuffer, BufferSize);
            break;
        case CubicResampler:
            Resample_cubic32(data, frac, increment, NumChannels,
                             OutBuffer, BufferSize);
            break;
        case ResamplerMax:
            /* Shouldn't happen */
            break;
    }
}


ALvoid MixSource(ALsource *Source, ALCdevice *Device, ALuint SamplesToDo)
{
    ALbufferlistitem *BufferListItem;
    ALuint DataPosInt, DataPosFrac;
    ALuint BuffersPlayed;
    ALboolean Looping;
    ALuint increment;
    enum Resampler Resampler;
    ALenum State;
    ALuint OutPos;
    ALuint NumChannels;
    ALuint FrameSize;
    ALint64 DataSize64;
    ALuint i, j;

    /* Get source info */
    State         = Source->state;
    BuffersPlayed = Source->BuffersPlayed;
    DataPosInt    = Source->position;
    DataPosFrac   = Source->position_fraction;
    Looping       = Source->Looping;
    increment     = Source->Params.Step;
    Resampler     = Source->Resampler;
    NumChannels   = Source->NumChannels;
    FrameSize     = NumChannels * Source->SampleSize;

    /* Get current buffer queue item */
    BufferListItem = Source->queue;
    for(i = 0;i < BuffersPlayed;i++)
        BufferListItem = BufferListItem->next;

    OutPos = 0;
    do {
        const ALuint BufferPrePadding = ResamplerPrePadding[Resampler];
        const ALuint BufferPadding = ResamplerPadding[Resampler];
        ALfloat StackData[BUFFERSIZE];
        ALfloat *SrcData = StackData;
        ALuint SrcDataSize = 0;
        ALuint BufferSize;

        /* Figure out how many buffer bytes will be needed */
        DataSize64  = SamplesToDo-OutPos+1;
        DataSize64 *= increment;
        DataSize64 += DataPosFrac+FRACTIONMASK;
        DataSize64 >>= FRACTIONBITS;
        DataSize64 += BufferPadding+BufferPrePadding;
        DataSize64 *= NumChannels;

        BufferSize  = (ALuint)mini64(DataSize64, BUFFERSIZE);
        BufferSize /= NumChannels;

        if(Source->SourceType == AL_STATIC)
        {
            const ALbuffer *ALBuffer = Source->queue->buffer;
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
                    DataSize = minu(BufferSize, DataSize);

                    SilenceStack(&SrcData[SrcDataSize*NumChannels],
                                 DataSize*NumChannels);
                    SrcDataSize += DataSize;
                    BufferSize -= DataSize;

                    pos = 0;
                }

                /* Copy what's left to play in the source buffer, and clear the
                 * rest of the temp buffer */
                DataSize = ALBuffer->SampleLen - pos;
                DataSize = minu(BufferSize, DataSize);

                LoadStack(&SrcData[SrcDataSize*NumChannels], &Data[pos*FrameSize],
                          ALBuffer->FmtType, DataSize*NumChannels);
                SrcDataSize += DataSize;
                BufferSize -= DataSize;

                SilenceStack(&SrcData[SrcDataSize*NumChannels],
                             BufferSize*NumChannels);
                SrcDataSize += BufferSize;
                BufferSize -= BufferSize;
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
                    DataSize = minu(BufferSize, DataSize);

                    SilenceStack(&SrcData[SrcDataSize*NumChannels], DataSize*NumChannels);
                    SrcDataSize += DataSize;
                    BufferSize -= DataSize;

                    pos = 0;
                }

                /* Copy what's left of this loop iteration, then copy repeats
                 * of the loop section */
                DataSize = LoopEnd - pos;
                DataSize = minu(BufferSize, DataSize);

                LoadStack(&SrcData[SrcDataSize*NumChannels], &Data[pos*FrameSize],
                          ALBuffer->FmtType, DataSize*NumChannels);
                SrcDataSize += DataSize;
                BufferSize -= DataSize;

                DataSize = LoopEnd-LoopStart;
                while(BufferSize > 0)
                {
                    DataSize = minu(BufferSize, DataSize);

                    LoadStack(&SrcData[SrcDataSize*NumChannels], &Data[LoopStart*FrameSize],
                              ALBuffer->FmtType, DataSize*NumChannels);
                    SrcDataSize += DataSize;
                    BufferSize -= DataSize;
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
                    if(!tmpiter->prev && !Looping)
                    {
                        ALuint DataSize = minu(BufferSize, pos);

                        SilenceStack(&SrcData[SrcDataSize*NumChannels], DataSize*NumChannels);
                        SrcDataSize += DataSize;
                        BufferSize -= DataSize;

                        pos = 0;
                        break;
                    }

                    if(tmpiter->prev)
                        tmpiter = tmpiter->prev;
                    else
                    {
                        while(tmpiter->next)
                            tmpiter = tmpiter->next;
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

            while(tmpiter && BufferSize > 0)
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
                        Data += pos*FrameSize;
                        DataSize -= pos;
                        pos -= pos;

                        DataSize = minu(BufferSize, DataSize);
                        LoadStack(&SrcData[SrcDataSize*NumChannels], Data,
                                  ALBuffer->FmtType, DataSize*NumChannels);
                        SrcDataSize += DataSize;
                        BufferSize -= DataSize;
                    }
                }
                tmpiter = tmpiter->next;
                if(!tmpiter && Looping)
                    tmpiter = Source->queue;
                else if(!tmpiter)
                {
                    SilenceStack(&SrcData[SrcDataSize*NumChannels], BufferSize*NumChannels);
                    SrcDataSize += BufferSize;
                    BufferSize -= BufferSize;
                }
            }
        }

        /* Figure out how many samples we can mix. */
        DataSize64  = SrcDataSize;
        DataSize64 -= BufferPadding+BufferPrePadding;
        DataSize64 <<= FRACTIONBITS;
        DataSize64 -= increment;
        DataSize64 -= DataPosFrac;

        BufferSize = (ALuint)((DataSize64+(increment-1)) / increment);
        BufferSize = minu(BufferSize, (SamplesToDo-OutPos));

        /* Some mixers like having a multiple of 4, so try to give that unless
         * this is the last update. */
        if(OutPos+BufferSize < SamplesToDo)
            BufferSize &= ~3;

        SrcData += BufferPrePadding*NumChannels;
        for(i = 0;i < NumChannels;i++)
        {
            ALfloat ResampledData[BUFFERSIZE];

            Resample(Resampler, SrcData+i, DataPosFrac, increment,
                     NumChannels, ResampledData, BufferSize);

            Source->Params.DryMix(Source, Device, &Source->Params.Direct,
                                  ResampledData, i, OutPos, SamplesToDo,
                                  BufferSize);
            for(j = 0;j < Device->NumAuxSends;j++)
            {
                if(!Source->Params.Send[j].Slot)
                    continue;
                Source->Params.WetMix(&Source->Params.Send[j], ResampledData, i,
                                      OutPos, SamplesToDo, BufferSize);
            }
        }
        for(i = 0;i < BufferSize;i++)
        {
            DataPosFrac += increment;
            DataPosInt  += DataPosFrac>>FRACTIONBITS;
            DataPosFrac &= FRACTIONMASK;
            OutPos++;
        }

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
                DataPosInt = ((DataPosInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                break;
            }

            if(DataSize > DataPosInt)
                break;

            if(BufferListItem->next)
            {
                BufferListItem = BufferListItem->next;
                BuffersPlayed++;
            }
            else if(Looping)
            {
                BufferListItem = Source->queue;
                BuffersPlayed = 0;
            }
            else
            {
                State = AL_STOPPED;
                BufferListItem = Source->queue;
                BuffersPlayed = Source->BuffersInQueue;
                DataPosInt = 0;
                DataPosFrac = 0;
                break;
            }

            DataPosInt -= DataSize;
        }
    } while(State == AL_PLAYING && OutPos < SamplesToDo);

    /* Update source info */
    Source->state             = State;
    Source->BuffersPlayed     = BuffersPlayed;
    Source->position          = DataPosInt;
    Source->position_fraction = DataPosFrac;
    Source->Hrtf.Offset      += OutPos;
    if(State == AL_PLAYING)
    {
        Source->Hrtf.Counter = maxu(Source->Hrtf.Counter, OutPos) - OutPos;
        Source->Hrtf.Moving  = AL_TRUE;
    }
    else
    {
        Source->Hrtf.Counter = 0;
        Source->Hrtf.Moving  = AL_FALSE;
    }
}
