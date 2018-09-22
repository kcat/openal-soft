#include "config.h"

#include <xmmintrin.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"

#include "alSource.h"
#include "alAuxEffectSlot.h"
#include "defs.h"


const ALfloat *Resample_bsinc_SSE(const InterpState *state, const ALfloat *restrict src,
                                  ALsizei frac, ALint increment, ALfloat *restrict dst,
                                  ALsizei dstlen)
{
    const ALfloat *const filter = state->bsinc.filter;
    const __m128 sf4 = _mm_set1_ps(state->bsinc.sf);
    const ALsizei m = state->bsinc.m;
    const __m128 *fil, *scd, *phd, *spd;
    ALsizei pi, i, j, offset;
    ALfloat pf;
    __m128 r4;

    ASSUME(m > 0);
    ASSUME(dstlen > 0);

    src -= state->bsinc.l;
    for(i = 0;i < dstlen;i++)
    {
        // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
        pi = frac >> FRAC_PHASE_BITDIFF;
        pf = (frac & ((1<<FRAC_PHASE_BITDIFF)-1)) * (1.0f/(1<<FRAC_PHASE_BITDIFF));
#undef FRAC_PHASE_BITDIFF

        offset = m*pi*4;
        fil = (const __m128*)ASSUME_ALIGNED(filter + offset, 16); offset += m;
        scd = (const __m128*)ASSUME_ALIGNED(filter + offset, 16); offset += m;
        phd = (const __m128*)ASSUME_ALIGNED(filter + offset, 16); offset += m;
        spd = (const __m128*)ASSUME_ALIGNED(filter + offset, 16);

        // Apply the scale and phase interpolated filter.
        r4 = _mm_setzero_ps();
        {
            const ALsizei count = m >> 2;
            const __m128 pf4 = _mm_set1_ps(pf);

            ASSUME(count > 0);

#define MLA4(x, y, z) _mm_add_ps(x, _mm_mul_ps(y, z))
            for(j = 0;j < count;j++)
            {
                /* f = ((fil + sf*scd) + pf*(phd + sf*spd)) */
                const __m128 f4 = MLA4(
                    MLA4(fil[j], sf4, scd[j]),
                    pf4, MLA4(phd[j], sf4, spd[j])
                );
                /* r += f*src */
                r4 = MLA4(r4, f4, _mm_loadu_ps(&src[j*4]));
            }
#undef MLA4
        }
        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
        dst[i] = _mm_cvtss_f32(r4);

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
    return dst;
}


static inline void ApplyCoeffs(ALsizei Offset, ALfloat (*restrict Values)[2],
                               const ALsizei IrSize,
                               const ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right)
{
    const __m128 lrlr = _mm_setr_ps(left, right, left, right);
    __m128 vals = _mm_setzero_ps();
    __m128 coeffs;
    ALsizei i;

    Values = ASSUME_ALIGNED(Values, 16);
    Coeffs = ASSUME_ALIGNED(Coeffs, 16);
    if((Offset&1))
    {
        const ALsizei o0 = Offset&HRIR_MASK;
        const ALsizei o1 = (Offset+IrSize-1)&HRIR_MASK;
        __m128 imp0, imp1;

        coeffs = _mm_load_ps(&Coeffs[0][0]);
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o0][0]);
        imp0 = _mm_mul_ps(lrlr, coeffs);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi((__m64*)&Values[o0][0], vals);
        for(i = 1;i < IrSize-1;i += 2)
        {
            const ALsizei o2 = (Offset+i)&HRIR_MASK;

            coeffs = _mm_load_ps(&Coeffs[i+1][0]);
            vals = _mm_load_ps(&Values[o2][0]);
            imp1 = _mm_mul_ps(lrlr, coeffs);
            imp0 = _mm_shuffle_ps(imp0, imp1, _MM_SHUFFLE(1, 0, 3, 2));
            vals = _mm_add_ps(imp0, vals);
            _mm_store_ps(&Values[o2][0], vals);
            imp0 = imp1;
        }
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o1][0]);
        imp0 = _mm_movehl_ps(imp0, imp0);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi((__m64*)&Values[o1][0], vals);
    }
    else
    {
        for(i = 0;i < IrSize;i += 2)
        {
            const ALsizei o = (Offset + i)&HRIR_MASK;

            coeffs = _mm_load_ps(&Coeffs[i][0]);
            vals = _mm_load_ps(&Values[o][0]);
            vals = _mm_add_ps(vals, _mm_mul_ps(lrlr, coeffs));
            _mm_store_ps(&Values[o][0], vals);
        }
    }
}

#define MixHrtf MixHrtf_SSE
#define MixHrtfBlend MixHrtfBlend_SSE
#define MixDirectHrtf MixDirectHrtf_SSE
#include "hrtf_inc.c"


void Mix_SSE(const ALfloat *data, ALsizei OutChans, ALfloat (*restrict OutBuffer)[BUFFERSIZE],
             ALfloat *CurrentGains, const ALfloat *TargetGains, ALsizei Counter, ALsizei OutPos,
             ALsizei BufferSize)
{
    const ALfloat delta = (Counter > 0) ? 1.0f/(ALfloat)Counter : 0.0f;
    ALsizei c;

    ASSUME(OutChans > 0);
    ASSUME(BufferSize > 0);

    for(c = 0;c < OutChans;c++)
    {
        ALsizei pos = 0;
        ALfloat gain = CurrentGains[c];
        const ALfloat diff = TargetGains[c] - gain;

        if(fabsf(diff) > FLT_EPSILON)
        {
            ALsizei minsize = mini(BufferSize, Counter);
            const ALfloat step = diff * delta;
            ALfloat step_count = 0.0f;
            /* Mix with applying gain steps in aligned multiples of 4. */
            if(LIKELY(minsize > 3))
            {
                const __m128 four4 = _mm_set1_ps(4.0f);
                const __m128 step4 = _mm_set1_ps(step);
                const __m128 gain4 = _mm_set1_ps(gain);
                __m128 step_count4 = _mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f);
                ALsizei todo = minsize >> 2;
                do {
                    const __m128 val4 = _mm_load_ps(&data[pos]);
                    __m128 dry4 = _mm_load_ps(&OutBuffer[c][OutPos+pos]);
#define MLA4(x, y, z) _mm_add_ps(x, _mm_mul_ps(y, z))
                    /* dry += val * (gain + step*step_count) */
                    dry4 = MLA4(dry4, val4, MLA4(gain4, step4, step_count4));
#undef MLA4
                    _mm_store_ps(&OutBuffer[c][OutPos+pos], dry4);
                    step_count4 = _mm_add_ps(step_count4, four4);
                    pos += 4;
                } while(--todo);
                /* NOTE: step_count4 now represents the next four counts after
                 * the last four mixed samples, so the lowest element
                 * represents the next step count to apply.
                 */
                step_count = _mm_cvtss_f32(step_count4);
            }
            /* Mix with applying left over gain steps that aren't aligned multiples of 4. */
            for(;pos < minsize;pos++)
            {
                OutBuffer[c][OutPos+pos] += data[pos]*(gain + step*step_count);
                step_count += 1.0f;
            }
            if(pos == Counter)
                gain = TargetGains[c];
            else
                gain += step*step_count;
            CurrentGains[c] = gain;

            /* Mix until pos is aligned with 4 or the mix is done. */
            minsize = mini(BufferSize, (pos+3)&~3);
            for(;pos < minsize;pos++)
                OutBuffer[c][OutPos+pos] += data[pos]*gain;
        }

        if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
            continue;
        if(LIKELY(BufferSize-pos > 3))
        {
            ALsizei todo = (BufferSize-pos) >> 2;
            const __m128 gain4 = _mm_set1_ps(gain);
            do {
                const __m128 val4 = _mm_load_ps(&data[pos]);
                __m128 dry4 = _mm_load_ps(&OutBuffer[c][OutPos+pos]);
                dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
                _mm_store_ps(&OutBuffer[c][OutPos+pos], dry4);
                pos += 4;
            } while(--todo);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*gain;
    }
}

void MixRow_SSE(ALfloat *OutBuffer, const ALfloat *Gains, const ALfloat (*restrict data)[BUFFERSIZE], ALsizei InChans, ALsizei InPos, ALsizei BufferSize)
{
    ALsizei c;

    ASSUME(InChans > 0);
    ASSUME(BufferSize > 0);

    for(c = 0;c < InChans;c++)
    {
        ALsizei pos = 0;
        const ALfloat gain = Gains[c];
        if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        if(LIKELY(BufferSize > 3))
        {
            ALsizei todo = BufferSize >> 2;
            const __m128 gain4 = _mm_set1_ps(gain);
            do {
                const __m128 val4 = _mm_load_ps(&data[c][InPos+pos]);
                __m128 dry4 = _mm_load_ps(&OutBuffer[pos]);
                dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
                _mm_store_ps(&OutBuffer[pos], dry4);
                pos += 4;
            } while(--todo);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[pos] += data[c][InPos+pos]*gain;
    }
}
