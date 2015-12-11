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
static inline ALfloat fir4_32(const ALfloat *vals, ALuint frac)
{ return resample_fir4(vals[-1], vals[0], vals[1], vals[2], frac); }
static inline ALfloat fir8_32(const ALfloat *vals, ALuint frac)
{ return resample_fir8(vals[-3], vals[-2], vals[-1], vals[0], vals[1], vals[2], vals[3], vals[4], frac); }


const ALfloat *Resample_copy32_C(const BsincState* UNUSED(state), const ALfloat *src, ALuint UNUSED(frac),
  ALuint UNUSED(increment), ALfloat *restrict dst, ALuint numsamples)
{
#if defined(HAVE_SSE) || defined(HAVE_NEON)
    /* Avoid copying the source data if it's aligned like the destination. */
    if((((intptr_t)src)&15) == (((intptr_t)dst)&15))
        return src;
#endif
    memcpy(dst, src, numsamples*sizeof(ALfloat));
    return dst;
}

#define DECL_TEMPLATE(Sampler)                                                \
const ALfloat *Resample_##Sampler##_C(const BsincState* UNUSED(state),        \
  const ALfloat *src, ALuint frac, ALuint increment,                          \
  ALfloat *restrict dst, ALuint numsamples)                                   \
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
DECL_TEMPLATE(fir4_32)
DECL_TEMPLATE(fir8_32)

#undef DECL_TEMPLATE

const ALfloat *Resample_bsinc32_C(const BsincState *state, const ALfloat *src, ALuint frac,
                                  ALuint increment, ALfloat *restrict dst, ALuint dstlen)
{
    const ALfloat *fil, *scd, *phd, *spd;
    const ALfloat sf = state->sf;
    const ALuint m = state->m;
    const ALint l = state->l;
    ALuint j_f, pi, i;
    ALfloat pf, r;
    ALint j_s;

    for(i = 0;i < dstlen;i++)
    {
        // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
        pi = frac >> FRAC_PHASE_BITDIFF;
        pf = (frac & ((1<<FRAC_PHASE_BITDIFF)-1)) * (1.0f/(1<<FRAC_PHASE_BITDIFF));
#undef FRAC_PHASE_BITDIFF

        fil = state->coeffs[pi].filter;
        scd = state->coeffs[pi].scDelta;
        phd = state->coeffs[pi].phDelta;
        spd = state->coeffs[pi].spDelta;

        // Apply the scale and phase interpolated filter.
        r = 0.0f;
        for(j_f = 0,j_s = l;j_f < m;j_f++,j_s++)
            r += (fil[j_f] + sf*scd[j_f] + pf*(phd[j_f] + sf*spd[j_f])) *
                    src[j_s];
        dst[i] = r;

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
    return dst;
}


void ALfilterState_processC(ALfilterState *filter, ALfloat *restrict dst, const ALfloat *src, ALuint numsamples)
{
    ALuint i;
    for(i = 0;i < numsamples;i++)
        *(dst++) = ALfilterState_processSingle(filter, *(src++));
}


static inline void SetupCoeffs(ALfloat (*restrict OutCoeffs)[2],
                               const HrtfParams *hrtfparams,
                               ALuint IrSize, ALuint Counter)
{
    ALuint c;
    for(c = 0;c < IrSize;c++)
    {
        OutCoeffs[c][0] = hrtfparams->Coeffs[c][0] - (hrtfparams->CoeffStep[c][0]*Counter);
        OutCoeffs[c][1] = hrtfparams->Coeffs[c][1] - (hrtfparams->CoeffStep[c][1]*Counter);
    }
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

#define MixHrtf MixHrtf_C
#include "mixer_inc.c"
#undef MixHrtf


void Mix_C(const ALfloat *data, ALuint OutChans, ALfloat (*restrict OutBuffer)[BUFFERSIZE],
           MixGains *Gains, ALuint Counter, ALuint OutPos, ALuint BufferSize)
{
    ALfloat gain, step;
    ALuint c;

    for(c = 0;c < OutChans;c++)
    {
        ALuint pos = 0;
        gain = Gains[c].Current;
        step = Gains[c].Step;
        if(step != 0.0f && Counter > 0)
        {
            ALuint minsize = minu(BufferSize, Counter);
            for(;pos < minsize;pos++)
            {
                OutBuffer[c][OutPos+pos] += data[pos]*gain;
                gain += step;
            }
            if(pos == Counter)
                gain = Gains[c].Target;
            Gains[c].Current = gain;
        }

        if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
            continue;
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*gain;
    }
}
