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

extern inline void InitiatePositionArrays(ALuint frac, ALint increment, ALuint *restrict frac_arr, ALint *restrict pos_arr, ALsizei size);

alignas(16) ALfloat ResampleCoeffs_FIR4[FRACTIONONE][4];


enum Resampler {
    PointResampler,
    LinearResampler,
    FIR4Resampler,
    BSincResampler,

    ResamplerDefault = LinearResampler
};

/* BSinc requires up to 11 extra samples before the current position, and 12 after. */
static_assert(MAX_PRE_SAMPLES >= 11, "MAX_PRE_SAMPLES must be at least 11!");
static_assert(MAX_POST_SAMPLES >= 12, "MAX_POST_SAMPLES must be at least 12!");


static MixerFunc MixSamples = Mix_C;
static HrtfMixerFunc MixHrtfSamples = MixHrtf_C;
static ResamplerFunc ResampleSamples = Resample_point32_C;

MixerFunc SelectMixer(void)
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

RowMixerFunc SelectRowMixer(void)
{
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixRow_SSE;
#endif
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixRow_Neon;
#endif
    return MixRow_C;
}

static inline HrtfMixerFunc SelectHrtfMixer(void)
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtf_Neon;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtf_SSE;
#endif

    return MixHrtf_C;
}

static inline ResamplerFunc SelectResampler(enum Resampler resampler)
{
    switch(resampler)
    {
        case PointResampler:
            return Resample_point32_C;
        case LinearResampler:
#ifdef HAVE_NEON
            if((CPUCapFlags&CPU_CAP_NEON))
                return Resample_lerp32_Neon;
#endif
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
#ifdef HAVE_NEON
            if((CPUCapFlags&CPU_CAP_NEON))
                return Resample_fir4_32_Neon;
#endif
#ifdef HAVE_SSE4_1
            if((CPUCapFlags&CPU_CAP_SSE4_1))
                return Resample_fir4_32_SSE41;
#endif
#ifdef HAVE_SSE3
            if((CPUCapFlags&CPU_CAP_SSE3))
                return Resample_fir4_32_SSE3;
#endif
            return Resample_fir4_32_C;
        case BSincResampler:
#ifdef HAVE_NEON
            if((CPUCapFlags&CPU_CAP_NEON))
                return Resample_bsinc32_Neon;
#endif
#ifdef HAVE_SSE
            if((CPUCapFlags&CPU_CAP_SSE))
                return Resample_bsinc32_SSE;
#endif
            return Resample_bsinc32_C;
    }

    return Resample_point32_C;
}


/* The sinc resampler makes use of a Kaiser window to limit the needed sample
 * points to 4 and 8, respectively.
 */

#ifndef M_PI
#define M_PI                         (3.14159265358979323846)
#endif
static inline double Sinc(double x)
{
    if(x == 0.0) return 1.0;
    return sin(x*M_PI) / (x*M_PI);
}

/* The zero-order modified Bessel function of the first kind, used for the
 * Kaiser window.
 *
 *   I_0(x) = sum_{k=0}^inf (1 / k!)^2 (x / 2)^(2 k)
 *          = sum_{k=0}^inf ((x / 2)^k / k!)^2
 */
static double BesselI_0(double x)
{
    double term, sum, x2, y, last_sum;
    int k;

    /* Start at k=1 since k=0 is trivial. */
    term = 1.0;
    sum = 1.0;
    x2 = x / 2.0;
    k = 1;

    /* Let the integration converge until the term of the sum is no longer
     * significant.
     */
    do {
        y = x2 / k;
        k ++;
        last_sum = sum;
        term *= y * y;
        sum += term;
    } while(sum != last_sum);
    return sum;
}

/* Calculate a Kaiser window from the given beta value and a normalized k
 * [-1, 1].
 *
 *   w(k) = { I_0(B sqrt(1 - k^2)) / I_0(B),  -1 <= k <= 1
 *          { 0,                              elsewhere.
 *
 * Where k can be calculated as:
 *
 *   k = i / l,         where -l <= i <= l.
 *
 * or:
 *
 *   k = 2 i / M - 1,   where 0 <= i <= M.
 */
static inline double Kaiser(double b, double k)
{
    if(k <= -1.0 || k >= 1.0) return 0.0;
    return BesselI_0(b * sqrt(1.0 - (k*k))) / BesselI_0(b);
}

static inline double CalcKaiserBeta(double rejection)
{
    if(rejection > 50.0)
        return 0.1102 * (rejection - 8.7);
    if(rejection >= 21.0)
        return (0.5842 * pow(rejection - 21.0, 0.4)) +
               (0.07886 * (rejection - 21.0));
    return 0.0;
}

static float SincKaiser(double r, double x)
{
    /* Limit rippling to -60dB. */
    return (float)(Kaiser(CalcKaiserBeta(60.0), x / r) * Sinc(x));
}


void aluInitMixer(void)
{
    enum Resampler resampler = ResamplerDefault;
    const char *str;
    ALuint i;

    if(ConfigValueStr(NULL, NULL, "resampler", &str))
    {
        if(strcasecmp(str, "point") == 0 || strcasecmp(str, "none") == 0)
            resampler = PointResampler;
        else if(strcasecmp(str, "linear") == 0)
            resampler = LinearResampler;
        else if(strcasecmp(str, "sinc4") == 0)
            resampler = FIR4Resampler;
        else if(strcasecmp(str, "bsinc") == 0)
            resampler = BSincResampler;
        else if(strcasecmp(str, "cubic") == 0 || strcasecmp(str, "sinc8") == 0)
        {
            WARN("Resampler option \"%s\" is deprecated, using sinc4\n", str);
            resampler = FIR4Resampler;
        }
        else
        {
            char *end;
            long n = strtol(str, &end, 0);
            if(*end == '\0' && (n == PointResampler || n == LinearResampler || n == FIR4Resampler))
                resampler = n;
            else
                WARN("Invalid resampler: %s\n", str);
        }
    }

    for(i = 0;i < FRACTIONONE;i++)
    {
        ALdouble mu = (ALdouble)i / FRACTIONONE;
        ResampleCoeffs_FIR4[i][0] = SincKaiser(2.0, mu - -1.0);
        ResampleCoeffs_FIR4[i][1] = SincKaiser(2.0, mu -  0.0);
        ResampleCoeffs_FIR4[i][2] = SincKaiser(2.0, mu -  1.0);
        ResampleCoeffs_FIR4[i][3] = SincKaiser(2.0, mu -  2.0);
    }

    MixHrtfSamples = SelectHrtfMixer();
    MixSamples = SelectMixer();
    ResampleSamples = SelectResampler(resampler);
}


static inline ALfloat Sample_ALbyte(ALbyte val)
{ return val * (1.0f/127.0f); }

static inline ALfloat Sample_ALshort(ALshort val)
{ return val * (1.0f/32767.0f); }

static inline ALfloat Sample_ALfloat(ALfloat val)
{ return val; }

#define DECL_TEMPLATE(T)                                                      \
static inline void Load_##T(ALfloat *dst, const T *src, ALint srcstep, ALsizei samples)\
{                                                                             \
    ALsizei i;                                                                \
    for(i = 0;i < samples;i++)                                                \
        dst[i] = Sample_##T(src[i*srcstep]);                                  \
}

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALfloat)

#undef DECL_TEMPLATE

static void LoadSamples(ALfloat *dst, const ALvoid *src, ALint srcstep, enum FmtType srctype, ALsizei samples)
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

static inline void SilenceSamples(ALfloat *dst, ALsizei samples)
{
    ALsizei i;
    for(i = 0;i < samples;i++)
        dst[i] = 0.0f;
}


static const ALfloat *DoFilters(ALfilterState *lpfilter, ALfilterState *hpfilter,
                                ALfloat *restrict dst, const ALfloat *restrict src,
                                ALsizei numsamples, enum ActiveFilters type)
{
    ALsizei i;
    switch(type)
    {
        case AF_None:
            ALfilterState_processPassthru(lpfilter, src, numsamples);
            ALfilterState_processPassthru(hpfilter, src, numsamples);
            break;

        case AF_LowPass:
            ALfilterState_process(lpfilter, dst, src, numsamples);
            ALfilterState_processPassthru(hpfilter, dst, numsamples);
            return dst;
        case AF_HighPass:
            ALfilterState_processPassthru(lpfilter, src, numsamples);
            ALfilterState_process(hpfilter, dst, src, numsamples);
            return dst;

        case AF_BandPass:
            for(i = 0;i < numsamples;)
            {
                ALfloat temp[256];
                ALsizei todo = mini(256, numsamples-i);

                ALfilterState_process(lpfilter, temp, src+i, todo);
                ALfilterState_process(hpfilter, dst+i, temp, todo);
                i += todo;
            }
            return dst;
    }
    return src;
}


ALboolean MixSource(ALvoice *voice, ALsource *Source, ALCdevice *Device, ALsizei SamplesToDo)
{
    ALbufferlistitem *BufferListItem;
    ALsizei NumChannels, SampleSize;
    ResamplerFunc Resample;
    ALsizei DataPosInt;
    ALuint DataPosFrac;
    ALint64 DataSize64;
    ALint increment;
    ALsizei Counter;
    ALsizei OutPos;
    ALsizei IrSize;
    bool isplaying;
    bool islooping;
    ALsizei chan;
    ALsizei send;

    /* Get source info */
    isplaying      = true; /* Will only be called while playing. */
    islooping      = ATOMIC_LOAD(&Source->looping, almemory_order_acquire);
    DataPosInt     = ATOMIC_LOAD(&voice->position, almemory_order_relaxed);
    DataPosFrac    = ATOMIC_LOAD(&voice->position_fraction, almemory_order_relaxed);
    BufferListItem = ATOMIC_LOAD(&voice->current_buffer, almemory_order_relaxed);
    NumChannels    = voice->NumChannels;
    SampleSize     = voice->SampleSize;
    increment      = voice->Step;

    IrSize = (Device->HrtfHandle ? Device->HrtfHandle->irSize : 0);

    Resample = ((increment == FRACTIONONE && DataPosFrac == 0) ?
                Resample_copy32_C : ResampleSamples);

    Counter = (voice->Flags&VOICE_IS_MOVING) ? SamplesToDo : 0;
    OutPos = 0;
    do {
        ALsizei SrcBufferSize, DstBufferSize;

        /* Figure out how many buffer samples will be needed */
        DataSize64  = SamplesToDo-OutPos;
        DataSize64 *= increment;
        DataSize64 += DataPosFrac+FRACTIONMASK;
        DataSize64 >>= FRACTIONBITS;
        DataSize64 += MAX_POST_SAMPLES+MAX_PRE_SAMPLES;

        SrcBufferSize = (ALsizei)mini64(DataSize64, BUFFERSIZE);

        /* Figure out how many samples we can actually mix from this. */
        DataSize64  = SrcBufferSize;
        DataSize64 -= MAX_POST_SAMPLES+MAX_PRE_SAMPLES;
        DataSize64 <<= FRACTIONBITS;
        DataSize64 -= DataPosFrac;

        DstBufferSize = (ALsizei)((DataSize64+(increment-1)) / increment);
        DstBufferSize = mini(DstBufferSize, (SamplesToDo-OutPos));

        /* Some mixers like having a multiple of 4, so try to give that unless
         * this is the last update. */
        if(OutPos+DstBufferSize < SamplesToDo)
            DstBufferSize &= ~3;

        for(chan = 0;chan < NumChannels;chan++)
        {
            const ALfloat *ResampledData;
            ALfloat *SrcData = Device->SourceData;
            ALsizei SrcDataSize;

            /* Load the previous samples into the source data first. */
            memcpy(SrcData, voice->PrevSamples[chan], MAX_PRE_SAMPLES*sizeof(ALfloat));
            SrcDataSize = MAX_PRE_SAMPLES;

            if(Source->SourceType == AL_STATIC)
            {
                const ALbuffer *ALBuffer = BufferListItem->buffer;
                const ALubyte *Data = ALBuffer->data;
                ALsizei DataSize;

                /* Offset buffer data to current channel */
                Data += chan*SampleSize;

                /* If current pos is beyond the loop range, do not loop */
                if(!islooping || DataPosInt >= ALBuffer->LoopEnd)
                {
                    islooping = false;

                    /* Load what's left to play from the source buffer, and
                     * clear the rest of the temp buffer */
                    DataSize = minu(SrcBufferSize - SrcDataSize,
                                    ALBuffer->SampleLen - DataPosInt);

                    LoadSamples(&SrcData[SrcDataSize], &Data[DataPosInt * NumChannels*SampleSize],
                                NumChannels, ALBuffer->FmtType, DataSize);
                    SrcDataSize += DataSize;

                    SilenceSamples(&SrcData[SrcDataSize], SrcBufferSize - SrcDataSize);
                    SrcDataSize += SrcBufferSize - SrcDataSize;
                }
                else
                {
                    ALsizei LoopStart = ALBuffer->LoopStart;
                    ALsizei LoopEnd   = ALBuffer->LoopEnd;

                    /* Load what's left of this loop iteration, then load
                     * repeats of the loop section */
                    DataSize = minu(SrcBufferSize - SrcDataSize, LoopEnd - DataPosInt);

                    LoadSamples(&SrcData[SrcDataSize], &Data[DataPosInt * NumChannels*SampleSize],
                                NumChannels, ALBuffer->FmtType, DataSize);
                    SrcDataSize += DataSize;

                    DataSize = LoopEnd-LoopStart;
                    while(SrcBufferSize > SrcDataSize)
                    {
                        DataSize = mini(SrcBufferSize - SrcDataSize, DataSize);

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
                ALsizei pos = DataPosInt;

                while(tmpiter && SrcBufferSize > SrcDataSize)
                {
                    const ALbuffer *ALBuffer;
                    if((ALBuffer=tmpiter->buffer) != NULL)
                    {
                        const ALubyte *Data = ALBuffer->data;
                        ALsizei DataSize = ALBuffer->SampleLen;

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
                    if(!tmpiter && islooping)
                        tmpiter = ATOMIC_LOAD(&Source->queue, almemory_order_acquire);
                    else if(!tmpiter)
                    {
                        SilenceSamples(&SrcData[SrcDataSize], SrcBufferSize - SrcDataSize);
                        SrcDataSize += SrcBufferSize - SrcDataSize;
                    }
                }
            }

            /* Store the last source samples used for next time. */
            memcpy(voice->PrevSamples[chan],
                &SrcData[(increment*DstBufferSize + DataPosFrac)>>FRACTIONBITS],
                MAX_PRE_SAMPLES*sizeof(ALfloat)
            );

            /* Now resample, then filter and mix to the appropriate outputs. */
            ResampledData = Resample(&voice->ResampleState,
                &SrcData[MAX_PRE_SAMPLES], DataPosFrac, increment,
                Device->ResampledData, DstBufferSize
            );
            {
                DirectParams *parms = &voice->Direct.Params[chan];
                const ALfloat *samples;

                samples = DoFilters(
                    &parms->LowPass, &parms->HighPass, Device->FilteredData,
                    ResampledData, DstBufferSize, parms->FilterType
                );
                if(!(voice->Flags&VOICE_IS_HRTF))
                {
                    if(!Counter)
                        memcpy(parms->Gains.Current, parms->Gains.Target,
                               sizeof(parms->Gains.Current));
                    if(!(voice->Flags&VOICE_HAS_NFC))
                        MixSamples(samples, voice->Direct.Channels, voice->Direct.Buffer,
                            parms->Gains.Current, parms->Gains.Target, Counter, OutPos,
                            DstBufferSize
                        );
                    else
                    {
                        static void (*const NfcUpdate[MAX_AMBI_ORDER])(
                            NfcFilter*,float*,const float*,const int
                        ) = {
                            NfcFilterUpdate1, NfcFilterUpdate2, NfcFilterUpdate3
                        };
                        ALfloat *nfcsamples = Device->NFCtrlData;
                        ALsizei ord, chanoffset = 0;

                        MixSamples(samples,
                            voice->Direct.ChannelsPerOrder[0], voice->Direct.Buffer,
                            parms->Gains.Current, parms->Gains.Target, Counter, OutPos,
                            DstBufferSize
                        );
                        chanoffset += voice->Direct.ChannelsPerOrder[0];
                        for(ord = 1;ord < MAX_AMBI_ORDER+1;ord++)
                        {
                            if(voice->Direct.ChannelsPerOrder[ord] <= 0)
                                break;
                            NfcUpdate[ord-1](&parms->NFCtrlFilter[ord-1], nfcsamples, samples,
                                             DstBufferSize);
                            MixSamples(nfcsamples, voice->Direct.ChannelsPerOrder[ord],
                                voice->Direct.Buffer+chanoffset, parms->Gains.Current+chanoffset,
                                parms->Gains.Target+chanoffset, Counter, OutPos, DstBufferSize
                            );
                            chanoffset += voice->Direct.ChannelsPerOrder[ord];
                        }
                    }
                }
                else
                {
                    MixHrtfParams hrtfparams;
                    int lidx, ridx;

                    lidx = GetChannelIdxByName(Device->RealOut, FrontLeft);
                    ridx = GetChannelIdxByName(Device->RealOut, FrontRight);
                    assert(lidx != -1 && ridx != -1);

                    if(!Counter)
                    {
                        parms->Hrtf.Old = parms->Hrtf.Target;
                        hrtfparams.Coeffs = SAFE_CONST(ALfloat2*,parms->Hrtf.Target.Coeffs);
                        hrtfparams.Delay[0] = parms->Hrtf.Target.Delay[0];
                        hrtfparams.Delay[1] = parms->Hrtf.Target.Delay[1];
                        hrtfparams.Gain = parms->Hrtf.Target.Gain;
                        hrtfparams.GainStep = 0.0f;
                        MixHrtfSamples(
                            voice->Direct.Buffer[lidx], voice->Direct.Buffer[ridx],
                            samples, voice->Offset, OutPos, IrSize, &hrtfparams,
                            &parms->Hrtf.State, DstBufferSize
                        );
                    }
                    else
                    {
                        HrtfState backupstate = parms->Hrtf.State;
                        ALfloat gain;

                        /* The old coefficients need to fade to silence
                         * completely since they'll be replaced after the mix.
                         * So it needs to fade out over DstBufferSize instead
                         * of Counter.
                         */
                        hrtfparams.Coeffs = SAFE_CONST(ALfloat2*,parms->Hrtf.Old.Coeffs);
                        hrtfparams.Delay[0] = parms->Hrtf.Old.Delay[0];
                        hrtfparams.Delay[1] = parms->Hrtf.Old.Delay[1];
                        hrtfparams.Gain = parms->Hrtf.Old.Gain;
                        hrtfparams.GainStep = -hrtfparams.Gain /
                                              (ALfloat)DstBufferSize;
                        MixHrtfSamples(
                            voice->Direct.Buffer[lidx], voice->Direct.Buffer[ridx],
                            samples, voice->Offset, OutPos, IrSize, &hrtfparams,
                            &backupstate, DstBufferSize
                        );

                        /* The new coefficients need to fade in completely
                         * since they're replacing the old ones. To keep the
                         * source gain fading consistent, interpolate between
                         * the old and new target gain given how much of the
                         * fade time this mix handles.
                         */
                        gain = lerp(parms->Hrtf.Old.Gain, parms->Hrtf.Target.Gain,
                                    (ALfloat)DstBufferSize/Counter);
                        hrtfparams.Coeffs = SAFE_CONST(ALfloat2*,parms->Hrtf.Target.Coeffs);
                        hrtfparams.Delay[0] = parms->Hrtf.Target.Delay[0];
                        hrtfparams.Delay[1] = parms->Hrtf.Target.Delay[1];
                        hrtfparams.Gain = 0.0f;
                        hrtfparams.GainStep = gain / (ALfloat)DstBufferSize;
                        MixHrtfSamples(
                            voice->Direct.Buffer[lidx], voice->Direct.Buffer[ridx],
                            samples, voice->Offset, OutPos, IrSize, &hrtfparams,
                            &parms->Hrtf.State, DstBufferSize
                        );
                        /* Update the old parameters with the result. */
                        parms->Hrtf.Old = parms->Hrtf.Target;
                        if(DstBufferSize < Counter)
                            parms->Hrtf.Old.Gain = hrtfparams.Gain;
                    }
                }
            }

            for(send = 0;send < Device->NumAuxSends;send++)
            {
                SendParams *parms = &voice->Send[send].Params[chan];
                const ALfloat *samples;

                if(!voice->Send[send].Buffer)
                    continue;

                samples = DoFilters(
                    &parms->LowPass, &parms->HighPass, Device->FilteredData,
                    ResampledData, DstBufferSize, parms->FilterType
                );

                if(!Counter)
                    memcpy(parms->Gains.Current, parms->Gains.Target,
                           sizeof(parms->Gains.Current));
                MixSamples(samples, voice->Send[send].Channels, voice->Send[send].Buffer,
                    parms->Gains.Current, parms->Gains.Target, Counter, OutPos, DstBufferSize
                );
            }
        }
        /* Update positions */
        DataPosFrac += increment*DstBufferSize;
        DataPosInt  += DataPosFrac>>FRACTIONBITS;
        DataPosFrac &= FRACTIONMASK;

        OutPos += DstBufferSize;
        voice->Offset += DstBufferSize;
        Counter = maxi(DstBufferSize, Counter) - DstBufferSize;

        /* Handle looping sources */
        while(1)
        {
            const ALbuffer *ALBuffer;
            ALsizei DataSize = 0;
            ALsizei LoopStart = 0;
            ALsizei LoopEnd = 0;

            if((ALBuffer=BufferListItem->buffer) != NULL)
            {
                DataSize = ALBuffer->SampleLen;
                LoopStart = ALBuffer->LoopStart;
                LoopEnd = ALBuffer->LoopEnd;
                if(LoopEnd > DataPosInt)
                    break;
            }

            if(islooping && Source->SourceType == AL_STATIC)
            {
                assert(LoopEnd > LoopStart);
                DataPosInt = ((DataPosInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                break;
            }

            if(DataSize > DataPosInt)
                break;

            if(!(BufferListItem=BufferListItem->next))
            {
                if(islooping)
                    BufferListItem = ATOMIC_LOAD(&Source->queue, almemory_order_acquire);
                else
                {
                    isplaying = false;
                    BufferListItem = NULL;
                    DataPosInt = 0;
                    DataPosFrac = 0;
                    break;
                }
            }

            DataPosInt -= DataSize;
        }
    } while(isplaying && OutPos < SamplesToDo);

    voice->Flags |= VOICE_IS_MOVING;

    /* Update source info */
    ATOMIC_STORE(&voice->position,          DataPosInt, almemory_order_relaxed);
    ATOMIC_STORE(&voice->position_fraction, DataPosFrac, almemory_order_relaxed);
    ATOMIC_STORE(&voice->current_buffer,    BufferListItem, almemory_order_release);
    return isplaying;
}
