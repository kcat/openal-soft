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

void Resample_copy32_C(const ALfloat *data, ALuint UNUSED(frac),
  ALuint increment, ALfloat *restrict OutBuffer, ALuint BufferSize)
{
    assert(increment==FRACTIONONE);
    memcpy(OutBuffer, data, BufferSize*sizeof(ALfloat));
}

#define DECL_TEMPLATE(Sampler)                                                \
void Resample_##Sampler##_C(const ALfloat *data, ALuint frac,                 \
  ALuint increment, ALfloat *restrict OutBuffer, ALuint BufferSize)           \
{                                                                             \
    ALuint pos = 0;                                                           \
    ALuint i;                                                                 \
                                                                              \
    for(i = 0;i < BufferSize;i++)                                             \
    {                                                                         \
        OutBuffer[i] = Sampler(data + pos, frac);                             \
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


void MixDirect_C(DirectParams *params, const ALfloat *restrict data, ALuint srcchan,
  ALuint OutPos, ALuint BufferSize)
{
    ALfloat (*restrict OutBuffer)[BUFFERSIZE] = params->OutBuffer;
    ALuint Counter = maxu(params->Counter, OutPos) - OutPos;
    ALfloat DrySend, Step;
    ALuint c;

    for(c = 0;c < MaxChannels;c++)
    {
        ALuint pos = 0;
        DrySend = params->Mix.Gains.Current[srcchan][c];
        Step = params->Mix.Gains.Step[srcchan][c];
        if(Step != 1.0f && Counter > 0)
        {
            for(;pos < BufferSize && pos < Counter;pos++)
            {
                OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
                DrySend *= Step;
            }
            if(pos == Counter)
                DrySend = params->Mix.Gains.Target[srcchan][c];
            params->Mix.Gains.Current[srcchan][c] = DrySend;
        }

        if(!(DrySend > GAIN_SILENCE_THRESHOLD))
            continue;
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
    }
}


void MixSend_C(SendParams *params, const ALfloat *restrict data,
  ALuint OutPos, ALuint BufferSize)
{
    ALfloat (*restrict OutBuffer)[BUFFERSIZE] = params->OutBuffer;
    ALuint Counter = maxu(params->Counter, OutPos) - OutPos;
    ALfloat WetSend, Step;

    {
        ALuint pos = 0;
        WetSend = params->Gain.Current;
        Step = params->Gain.Step;
        if(Step != 1.0f && Counter > 0)
        {
            for(;pos < BufferSize && pos < Counter;pos++)
            {
                OutBuffer[0][OutPos+pos] += data[pos]*WetSend;
                WetSend *= Step;
            }
            if(pos == Counter)
                WetSend = params->Gain.Target;
            params->Gain.Current = WetSend;
        }

        if(!(WetSend > GAIN_SILENCE_THRESHOLD))
            return;
        for(;pos < BufferSize;pos++)
            OutBuffer[0][OutPos+pos] += data[pos] * WetSend;
    }
}
