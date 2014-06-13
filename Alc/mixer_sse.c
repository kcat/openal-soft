#include "config.h"

#ifdef IN_IDE_PARSER
/* KDevelop's parser won't recognize these defines that get added by the -msse
 * switch used to compile this source. Without them, xmmintrin.h fails to
 * declare anything. */
#define __MMX__
#define __SSE__
#endif
#include <xmmintrin.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"

#include "alSource.h"
#include "alAuxEffectSlot.h"
#include "mixer_defs.h"


static inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*restrict Values)[2],
                                   const ALuint IrSize,
                                   ALfloat (*restrict Coeffs)[2],
                                   const ALfloat (*restrict CoeffStep)[2],
                                   ALfloat left, ALfloat right)
{
    const __m128 lrlr = _mm_setr_ps(left, right, left, right);
    __m128 coeffs, deltas, imp0, imp1;
    __m128 vals = _mm_setzero_ps();
    ALuint i;

    if((Offset&1))
    {
        const ALuint o0 = Offset&HRIR_MASK;
        const ALuint o1 = (Offset+IrSize-1)&HRIR_MASK;

        coeffs = _mm_load_ps(&Coeffs[0][0]);
        deltas = _mm_load_ps(&CoeffStep[0][0]);
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o0][0]);
        imp0 = _mm_mul_ps(lrlr, coeffs);
        coeffs = _mm_add_ps(coeffs, deltas);
        vals = _mm_add_ps(imp0, vals);
        _mm_store_ps(&Coeffs[0][0], coeffs);
        _mm_storel_pi((__m64*)&Values[o0][0], vals);
        for(i = 1;i < IrSize-1;i += 2)
        {
            const ALuint o2 = (Offset+i)&HRIR_MASK;

            coeffs = _mm_load_ps(&Coeffs[i+1][0]);
            deltas = _mm_load_ps(&CoeffStep[i+1][0]);
            vals = _mm_load_ps(&Values[o2][0]);
            imp1 = _mm_mul_ps(lrlr, coeffs);
            coeffs = _mm_add_ps(coeffs, deltas);
            imp0 = _mm_shuffle_ps(imp0, imp1, _MM_SHUFFLE(1, 0, 3, 2));
            vals = _mm_add_ps(imp0, vals);
            _mm_store_ps(&Coeffs[i+1][0], coeffs);
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
            const ALuint o = (Offset + i)&HRIR_MASK;

            coeffs = _mm_load_ps(&Coeffs[i][0]);
            deltas = _mm_load_ps(&CoeffStep[i][0]);
            vals = _mm_load_ps(&Values[o][0]);
            imp0 = _mm_mul_ps(lrlr, coeffs);
            coeffs = _mm_add_ps(coeffs, deltas);
            vals = _mm_add_ps(imp0, vals);
            _mm_store_ps(&Coeffs[i][0], coeffs);
            _mm_store_ps(&Values[o][0], vals);
        }
    }
}

static inline void ApplyCoeffs(ALuint Offset, ALfloat (*restrict Values)[2],
                               const ALuint IrSize,
                               ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right)
{
    const __m128 lrlr = _mm_setr_ps(left, right, left, right);
    __m128 vals = _mm_setzero_ps();
    __m128 coeffs;
    ALuint i;

    if((Offset&1))
    {
        const ALuint o0 = Offset&HRIR_MASK;
        const ALuint o1 = (Offset+IrSize-1)&HRIR_MASK;
        __m128 imp0, imp1;

        coeffs = _mm_load_ps(&Coeffs[0][0]);
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o0][0]);
        imp0 = _mm_mul_ps(lrlr, coeffs);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi((__m64*)&Values[o0][0], vals);
        for(i = 1;i < IrSize-1;i += 2)
        {
            const ALuint o2 = (Offset+i)&HRIR_MASK;

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
            const ALuint o = (Offset + i)&HRIR_MASK;

            coeffs = _mm_load_ps(&Coeffs[i][0]);
            vals = _mm_load_ps(&Values[o][0]);
            vals = _mm_add_ps(vals, _mm_mul_ps(lrlr, coeffs));
            _mm_store_ps(&Values[o][0], vals);
        }
    }
}

#define SUFFIX SSE
#include "mixer_inc.c"
#undef SUFFIX


void Mix_SSE(const ALfloat *data, ALuint OutChans, ALfloat (*restrict OutBuffer)[BUFFERSIZE],
             MixGains *Gains, ALuint Counter, ALuint OutPos, ALuint BufferSize)
{
    ALfloat gain, step;
    __m128 gain4, step4;
    ALuint c;

    for(c = 0;c < OutChans;c++)
    {
        ALuint pos = 0;
        gain = Gains[c].Current;
        step = Gains[c].Step;
        if(step != 1.0f && Counter > 0)
        {
            /* Mix with applying gain steps in aligned multiples of 4. */
            if(BufferSize-pos > 3 && Counter-pos > 3)
            {
                gain4 = _mm_setr_ps(
                    gain,
                    gain * step,
                    gain * step * step,
                    gain * step * step * step
                );
                step4 = _mm_set1_ps(step * step * step * step);
                do {
                    const __m128 val4 = _mm_load_ps(&data[pos]);
                    __m128 dry4 = _mm_load_ps(&OutBuffer[c][OutPos+pos]);
                    dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
                    gain4 = _mm_mul_ps(gain4, step4);
                    _mm_store_ps(&OutBuffer[c][OutPos+pos], dry4);
                    pos += 4;
                } while(BufferSize-pos > 3 && Counter-pos > 3);
                gain = _mm_cvtss_f32(gain4);
            }
            /* Mix with applying left over gain steps that aren't aligned multiples of 4. */
            for(;pos < BufferSize && pos < Counter;pos++)
            {
                OutBuffer[c][OutPos+pos] += data[pos]*gain;
                gain *= step;
            }
            if(pos == Counter)
                gain = Gains[c].Target;
            Gains[c].Current = gain;
            /* Mix until pos is aligned with 4 or the mix is done. */
            for(;pos < BufferSize && (pos&3) != 0;pos++)
                OutBuffer[c][OutPos+pos] += data[pos]*gain;
        }

        if(!(gain > GAIN_SILENCE_THRESHOLD))
            continue;
        gain4 = _mm_set1_ps(gain);
        for(;BufferSize-pos > 3;pos += 4)
        {
            const __m128 val4 = _mm_load_ps(&data[pos]);
            __m128 dry4 = _mm_load_ps(&OutBuffer[c][OutPos+pos]);
            dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
            _mm_store_ps(&OutBuffer[c][OutPos+pos], dry4);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*gain;
    }
}
