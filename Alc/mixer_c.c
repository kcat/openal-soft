#include "config.h"

#include <assert.h>

#include "alMain.h"
#include "alu.h"
#include "alSource.h"
#include "alAuxEffectSlot.h"


static inline ALfloat point32(const ALfloat *vals, ALuint UNUSED(frac))
{ return vals[0]; }
static inline ALfloat lerp32(const ALfloat *vals, ALuint frac)
{ return lerp(vals[0], vals[1], frac * (1.0f/FRACTIONONE)); }
static inline ALfloat cubic32(const ALfloat *vals, ALuint frac)
{ return cubic(vals[-1], vals[0], vals[1], vals[2], frac * (1.0f/FRACTIONONE)); }

const ALfloat *Resample_copy32_C(const ALfloat *src, ALuint UNUSED(frac),
  ALuint increment, ALfloat *restrict dst, ALuint numsamples)
{
    assert(increment==FRACTIONONE);
#if defined(HAVE_SSE) || defined(HAVE_NEON)
    /* Avoid copying the source data if it's aligned like the destination. */
    if((((intptr_t)src)&15) == (((intptr_t)dst)&15))
        return src;
#endif
    memcpy(dst, src, numsamples*sizeof(ALfloat));
    return dst;
}

#define DECL_TEMPLATE(Sampler)                                                \
const ALfloat *Resample_##Sampler##_C(const ALfloat *src, ALuint frac,        \
  ALuint increment, ALfloat *restrict dst, ALuint numsamples)                 \
{                                                                             \
    ALuint i;                                                                 \
    for(i = 0;i < numsamples;i++)                                             \
    {                                                                         \
        dst[i] = Sampler(src, frac);                                          \
                                                                              \
        frac += increment;                                                    \
        src  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
    }                                                                         \
    return dst;                                                               \
}

DECL_TEMPLATE(point32)
DECL_TEMPLATE(lerp32)
DECL_TEMPLATE(cubic32)

#undef DECL_TEMPLATE


void ALfilterState_processC(ALfilterState *filter, ALfloat *restrict dst, const ALfloat *src, ALuint numsamples)
{
    ALuint i;
    for(i = 0;i < numsamples;i++)
        *(dst++) = ALfilterState_processSingle(filter, *(src++));
}


static inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*restrict Values)[2],
                                   const ALuint IrSize,
                                   ALfloat (*restrict Coeffs)[2],
                                   const ALfloat (*restrict CoeffStep)[2],
                                   ALfloat left, ALfloat right)
{
    ALuint c;
    for(c = 0;c < IrSize;c++)
    {
        const ALuint off = (Offset+c)&HRIR_MASK;
        Values[off][0] += Coeffs[c][0] * left;
        Values[off][1] += Coeffs[c][1] * right;
        Coeffs[c][0] += CoeffStep[c][0];
        Coeffs[c][1] += CoeffStep[c][1];
    }
}

static inline void ApplyCoeffs(ALuint Offset, ALfloat (*restrict Values)[2],
                               const ALuint IrSize,
                               ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right)
{
    ALuint c;
    for(c = 0;c < IrSize;c++)
    {
        const ALuint off = (Offset+c)&HRIR_MASK;
        Values[off][0] += Coeffs[c][0] * left;
        Values[off][1] += Coeffs[c][1] * right;
    }
}

#define SUFFIX C
#include "mixer_inc.c"
#undef SUFFIX


void MixDirect_C(ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *data,
                 MixGains *Gains, ALuint Counter, ALuint OutPos, ALuint BufferSize)
{
    ALfloat DrySend, Step;
    ALuint c;

    for(c = 0;c < MaxChannels;c++)
    {
        ALuint pos = 0;
        DrySend = Gains->Current[c];
        Step = Gains->Step[c];
        if(Step != 1.0f && Counter > 0)
        {
            for(;pos < BufferSize && pos < Counter;pos++)
            {
                OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
                DrySend *= Step;
            }
            if(pos == Counter)
                DrySend = Gains->Target[c];
            Gains->Current[c] = DrySend;
        }

        if(!(DrySend > GAIN_SILENCE_THRESHOLD))
            continue;
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
    }
}


void MixSend_C(ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *data,
               MixGainMono *Gain, ALuint Counter, ALuint OutPos, ALuint BufferSize)
{
    ALfloat WetSend, Step;

    {
        ALuint pos = 0;
        WetSend = Gain->Current;
        Step = Gain->Step;
        if(Step != 1.0f && Counter > 0)
        {
            for(;pos < BufferSize && pos < Counter;pos++)
            {
                OutBuffer[0][OutPos+pos] += data[pos]*WetSend;
                WetSend *= Step;
            }
            if(pos == Counter)
                WetSend = Gain->Target;
            Gain->Current = WetSend;
        }

        if(!(WetSend > GAIN_SILENCE_THRESHOLD))
            return;
        for(;pos < BufferSize;pos++)
            OutBuffer[0][OutPos+pos] += data[pos] * WetSend;
    }
}
