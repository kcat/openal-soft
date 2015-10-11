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


static_assert((INT_MAX>>FRACTIONBITS)/MAX_PITCH > BUFFERSIZE,
              "MAX_PITCH and/or BUFFERSIZE are too large for FRACTIONBITS!");

extern inline void InitiatePositionArrays(ALuint frac, ALuint increment, ALuint *frac_arr, ALuint *pos_arr, ALuint size);

alignas(16) union ResamplerCoeffs ResampleCoeffs;


enum Resampler {
    PointResampler,
    LinearResampler,
    FIR4Resampler,
    FIR8Resampler,

    ResamplerMax,
};

static enum Resampler DefaultResampler = LinearResampler;

/* Each entry is a pair, where the first is the number of samples needed before
 * the current position, and the second is the number of samples needed after
 * (not including) the current position, for the given resampler.
 */
static const ALsizei ResamplerPadding[ResamplerMax][2] = {
    {0, 0}, /* Point */
    {0, 1}, /* Linear */
    {1, 2}, /* FIR4 */
    {3, 4}, /* FIR8 */
};


static HrtfMixerFunc MixHrtfSamples = MixHrtf_C;
static MixerFunc MixSamples = Mix_C;
static ResamplerFunc ResampleSamples = Resample_point32_C;

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

static inline ResamplerFunc SelectResampler(enum Resampler resampler)
{
    switch(resampler)
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
        case FIR4Resampler:
#ifdef HAVE_SSE4_1
            if((CPUCapFlags&CPU_CAP_SSE4_1))
                return Resample_fir4_32_SSE41;
#endif
#ifdef HAVE_SSE3
            if((CPUCapFlags&CPU_CAP_SSE3))
                return Resample_fir4_32_SSE3;
#endif
            return Resample_fir4_32_C;
        case FIR8Resampler:
#ifdef HAVE_SSE4_1
            if((CPUCapFlags&CPU_CAP_SSE4_1))
                return Resample_fir8_32_SSE41;
#endif
#ifdef HAVE_SSE3
            if((CPUCapFlags&CPU_CAP_SSE3))
                return Resample_fir8_32_SSE3;
#endif
            return Resample_fir8_32_C;
        case ResamplerMax:
            /* Shouldn't happen */
            break;
    }

    return Resample_point32_C;
}

#ifndef M_PI
#define M_PI                         (3.14159265358979323846)
#endif
static float lanc(double r, double x)
{
    if(x == 0.0) return 1.0f;
    if(fabs(x) >= r) return 0.0f;
    return (float)(r*sin(x*M_PI)*sin(x*M_PI/r) /
                   (M_PI*M_PI * x*x));
}

void aluInitMixer(void)
{
    const char *str;
    ALuint i;

    if(ConfigValueStr(NULL, NULL, "resampler", &str))
    {
        if(strcasecmp(str, "point") == 0 || strcasecmp(str, "none") == 0)
            DefaultResampler = PointResampler;
        else if(strcasecmp(str, "linear") == 0)
            DefaultResampler = LinearResampler;
        else if(strcasecmp(str, "sinc4") == 0)
            DefaultResampler = FIR4Resampler;
        else if(strcasecmp(str, "sinc8") == 0)
            DefaultResampler = FIR8Resampler;
        else if(strcasecmp(str, "cubic") == 0)
        {
            WARN("Resampler option \"cubic\" is deprecated, using sinc4\n");
            DefaultResampler = FIR4Resampler;
        }
        else
        {
            char *end;
            long n = strtol(str, &end, 0);
            if(*end == '\0' && (n == PointResampler || n == LinearResampler || n == FIR4Resampler))
                DefaultResampler = n;
            else
                WARN("Invalid resampler: %s\n", str);
        }
    }

    if(DefaultResampler == FIR8Resampler)
        for(i = 0;i < FRACTIONONE;i++)
        {
            ALdouble mu = (ALdouble)i / FRACTIONONE;
            ResampleCoeffs.FIR8[i][0] = lanc(4.0, mu - -3.0);
            ResampleCoeffs.FIR8[i][1] = lanc(4.0, mu - -2.0);
            ResampleCoeffs.FIR8[i][2] = lanc(4.0, mu - -1.0);
            ResampleCoeffs.FIR8[i][3] = lanc(4.0, mu -  0.0);
            ResampleCoeffs.FIR8[i][4] = lanc(4.0, mu -  1.0);
            ResampleCoeffs.FIR8[i][5] = lanc(4.0, mu -  2.0);
            ResampleCoeffs.FIR8[i][6] = lanc(4.0, mu -  3.0);
            ResampleCoeffs.FIR8[i][7] = lanc(4.0, mu -  4.0);
        }
    else if(DefaultResampler == FIR4Resampler)
        for(i = 0;i < FRACTIONONE;i++)
        {
            ALdouble mu = (ALdouble)i / FRACTIONONE;
            ResampleCoeffs.FIR4[i][0] = lanc(2.0, mu - -1.0);
            ResampleCoeffs.FIR4[i][1] = lanc(2.0, mu -  0.0);
            ResampleCoeffs.FIR4[i][2] = lanc(2.0, mu -  1.0);
            ResampleCoeffs.FIR4[i][3] = lanc(2.0, mu -  2.0);
        }

    MixHrtfSamples = SelectHrtfMixer();
    MixSamples = SelectMixer();
    ResampleSamples = SelectResampler(DefaultResampler);
}


static inline ALfloat Sample_ALbyte(ALbyte val)
{ return val * (1.0f/127.0f); }

static inline ALfloat Sample_ALshort(ALshort val)
{ return val * (1.0f/32767.0f); }

static inline ALfloat Sample_ALfloat(ALfloat val)
{ return val; }

#define DECL_TEMPLATE(T)                                                      \
static inline void Load_##T(ALfloat *dst, const T *src, ALuint srcstep, ALuint samples)\
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

static inline void SilenceSamples(ALfloat *dst, ALuint samples)
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
                ALfloat temp[256];
                ALuint todo = minu(256, numsamples-i);

                ALfilterState_process(lpfilter, temp, src+i, todo);
                ALfilterState_process(hpfilter, dst+i, temp, todo);
                i += todo;
            }
            return dst;
    }
    return src;
}


ALvoid MixSource(ALvoice *voice, ALsource *Source, ALCdevice *Device, ALuint SamplesToDo)
{
    ResamplerFunc Resample;
    ALbufferlistitem *BufferListItem;
    ALuint DataPosInt, DataPosFrac;
    ALboolean isbformat = AL_FALSE;
    ALboolean Looping;
    ALuint increment;
    ALenum State;
    ALuint OutPos;
    ALuint NumChannels;
    ALuint SampleSize;
    ALint64 DataSize64;
    ALuint IrSize;
    ALuint chan, j;

    /* Get source info */
    State          = Source->state;
    BufferListItem = ATOMIC_LOAD(&Source->current_buffer);
    DataPosInt     = Source->position;
    DataPosFrac    = Source->position_fraction;
    Looping        = Source->Looping;
    NumChannels    = Source->NumChannels;
    SampleSize     = Source->SampleSize;
    increment      = voice->Step;

    while(BufferListItem)
    {
        ALbuffer *buffer;
        if((buffer=BufferListItem->buffer) != NULL)
        {
            isbformat = (buffer->FmtChannels == FmtBFormat2D ||
                         buffer->FmtChannels == FmtBFormat3D);
            break;
        }
        BufferListItem = BufferListItem->next;
    }
    assert(BufferListItem != NULL);

    IrSize = (Device->Hrtf ? GetHrtfIrSize(Device->Hrtf) : 0);

    Resample = ((increment == FRACTIONONE && DataPosFrac == 0) ?
                Resample_copy32_C : ResampleSamples);

    OutPos = 0;
    do {
        const ALuint BufferPrePadding = ResamplerPadding[DefaultResampler][0];
        const ALuint BufferPadding = ResamplerPadding[DefaultResampler][1];
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

                /* Offset to current channel */
                Data += chan*SampleSize;

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

                    LoadSamples(&SrcData[SrcDataSize], &Data[pos * NumChannels*SampleSize],
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

                    LoadSamples(&SrcData[SrcDataSize], &Data[pos * NumChannels*SampleSize],
                                NumChannels, ALBuffer->FmtType, DataSize);
                    SrcDataSize += DataSize;

                    DataSize = LoopEnd-LoopStart;
                    while(SrcBufferSize > SrcDataSize)
                    {
                        DataSize = minu(SrcBufferSize - SrcDataSize, DataSize);

                        LoadSamples(&SrcData[SrcDataSize], &Data[LoopStart * NumChannels*SampleSize],
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
                DirectParams *parms = &voice->Direct;
                const ALfloat *samples;

                samples = DoFilters(
                    &parms->Filters[chan].LowPass, &parms->Filters[chan].HighPass,
                    Device->FilteredData, ResampledData, DstBufferSize,
                    parms->Filters[chan].ActiveType
                );
                if(!voice->IsHrtf)
                    MixSamples(samples, parms->OutChannels, parms->OutBuffer, parms->Gains[chan],
                               parms->Counter, OutPos, DstBufferSize);
                else
                    MixHrtfSamples(parms->OutBuffer, samples, parms->Counter, voice->Offset,
                                   OutPos, IrSize, &parms->Hrtf[chan].Params,
                                   &parms->Hrtf[chan].State, DstBufferSize);
            }

            /* Only the first channel for B-Format buffers (W channel) goes to
             * the send paths. */
            if(chan > 0 && isbformat)
                continue;
            for(j = 0;j < Device->NumAuxSends;j++)
            {
                SendParams *parms = &voice->Send[j];
                const ALfloat *samples;

                if(!parms->OutBuffer)
                    continue;

                samples = DoFilters(
                    &parms->Filters[chan].LowPass, &parms->Filters[chan].HighPass,
                    Device->FilteredData, ResampledData, DstBufferSize,
                    parms->Filters[chan].ActiveType
                );
                MixSamples(samples, 1, parms->OutBuffer, &parms->Gain,
                           parms->Counter, OutPos, DstBufferSize);
            }
        }
        /* Update positions */
        DataPosFrac += increment*DstBufferSize;
        DataPosInt  += DataPosFrac>>FRACTIONBITS;
        DataPosFrac &= FRACTIONMASK;

        OutPos += DstBufferSize;
        voice->Offset += DstBufferSize;
        voice->Direct.Counter = maxu(voice->Direct.Counter, DstBufferSize) - DstBufferSize;
        for(j = 0;j < Device->NumAuxSends;j++)
            voice->Send[j].Counter = maxu(voice->Send[j].Counter, DstBufferSize) - DstBufferSize;

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
