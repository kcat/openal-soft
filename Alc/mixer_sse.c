#include "config.h"

#ifdef HAVE_XMMINTRIN_H
#include <xmmintrin.h>
#endif

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"

#include "alSource.h"
#include "mixer_defs.h"

static __inline ALfloat lerp32(const ALfloat *vals, ALint step, ALuint frac)
{ return lerp(vals[0], vals[step], frac * (1.0f/FRACTIONONE)); }

void Resample_lerp32_SSE(const ALfloat *data, ALuint frac,
  ALuint increment, ALuint NumChannels, ALfloat *RESTRICT OutBuffer,
  ALuint BufferSize)
{
    ALIGN(16) float value[3][4];
    ALuint pos = 0;
    ALuint i, j;

    for(i = 0;i < BufferSize+1-3;i+=4)
    {
        __m128 x, y, a;
        for(j = 0;j < 4;j++)
        {
            value[0][j] = data[(pos  )*NumChannels];
            value[1][j] = data[(pos+1)*NumChannels];
            value[2][j] = frac * (1.0f/FRACTIONONE);

            frac += increment;
            pos  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
        }

        x = _mm_load_ps(value[0]);
        y = _mm_load_ps(value[1]);
        y = _mm_sub_ps(y, x);

        a = _mm_load_ps(value[2]);
        y = _mm_mul_ps(y, a);

        x = _mm_add_ps(x, y);

        _mm_store_ps(&OutBuffer[i], x);
    }
    for(;i < BufferSize+1;i++)
    {
        OutBuffer[i] = lerp32(data + pos*NumChannels, NumChannels, frac);

        frac += increment;
        pos  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
}

void Resample_cubic32_SSE(const ALfloat *data, ALuint frac,
  ALuint increment, ALuint NumChannels, ALfloat *RESTRICT OutBuffer,
  ALuint BufferSize)
{
    /* Cubic interpolation mainly consists of a matrix4 * vector4 operation,
     * followed by scalars being applied to the resulting elements before all
     * four are added together for the final sample. */
    static const __m128 matrix[4] = {
        { -0.5,  1.0f, -0.5f,  0.0f },
        {  1.5, -2.5f,  0.0f,  1.0f },
        { -1.5,  2.0f,  0.5f,  0.0f },
        {  0.5, -0.5f,  0.0f,  0.0f },
    };
    ALIGN(16) float value[4];
    ALuint pos = 0;
    ALuint i, j;

    for(i = 0;i < BufferSize+1-3;i+=4)
    {
        __m128 result, final[4];

        for(j = 0;j < 4;j++)
        {
            __m128 val4, s;
            ALfloat mu;

            val4 = _mm_set_ps(data[(pos-1)*NumChannels],
                              data[(pos  )*NumChannels],
                              data[(pos+1)*NumChannels],
                              data[(pos+2)*NumChannels]);
            mu = frac * (1.0f/FRACTIONONE);
            s = _mm_set_ps(1.0f, mu, mu*mu, mu*mu*mu);

            /* result = matrix * val4 */
            result =                    _mm_mul_ps(val4, matrix[0]) ;
            result = _mm_add_ps(result, _mm_mul_ps(val4, matrix[1]));
            result = _mm_add_ps(result, _mm_mul_ps(val4, matrix[2]));
            result = _mm_add_ps(result, _mm_mul_ps(val4, matrix[3]));

            /* final[j] = result * { mu^0, mu^1, mu^2, mu^3 } */
            final[j] = _mm_mul_ps(result, s);

            frac += increment;
            pos  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
        }
        /* Transpose the final "matrix" so adding the rows will give the four
         * samples. TODO: Is this faster than doing..
         * _mm_store_ps(value, result);
         * OutBuffer[i] = value[0] + value[1] + value[2] + value[3];
         * ..for each sample?
         */
        _MM_TRANSPOSE4_PS(final[0], final[1], final[2], final[3]);
        result = _mm_add_ps(_mm_add_ps(final[0], final[1]),
                            _mm_add_ps(final[2], final[3]));

        _mm_store_ps(&OutBuffer[i], result);
    }
    for(;i < BufferSize+1;i++)
    {
        __m128 val4, s, result;
        ALfloat mu;

        val4 = _mm_set_ps(data[(pos-1)*NumChannels],
                          data[(pos  )*NumChannels],
                          data[(pos+1)*NumChannels],
                          data[(pos+2)*NumChannels]);
        mu = frac * (1.0f/FRACTIONONE);
        s = _mm_set_ps(1.0f, mu, mu*mu, mu*mu*mu);

        /* result = matrix * val4 */
        result =                    _mm_mul_ps(val4, matrix[0]) ;
        result = _mm_add_ps(result, _mm_mul_ps(val4, matrix[1]));
        result = _mm_add_ps(result, _mm_mul_ps(val4, matrix[2]));
        result = _mm_add_ps(result, _mm_mul_ps(val4, matrix[3]));

        /* value = result * { mu^0, mu^1, mu^2, mu^3 } */
        _mm_store_ps(value, _mm_mul_ps(result, s));

        OutBuffer[i] = value[0] + value[1] + value[2] + value[3];

        frac += increment;
        pos  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
}


static __inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                     const ALuint IrSize,
                                     ALfloat (*RESTRICT Coeffs)[2],
                                     ALfloat (*RESTRICT CoeffStep)[2],
                                     ALfloat left, ALfloat right)
{
    const __m128 lrlr = { left, right, left, right };
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

static __inline void ApplyCoeffs(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                 const ALuint IrSize,
                                 ALfloat (*RESTRICT Coeffs)[2],
                                 ALfloat left, ALfloat right)
{
    const __m128 lrlr = { left, right, left, right };
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


void MixDirect_SSE(ALsource *Source, ALCdevice *Device, DirectParams *params,
  const ALfloat *RESTRICT data, ALuint srcchan,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    ALfloat (*RESTRICT DryBuffer)[BUFFERSIZE] = Device->DryBuffer;
    ALfloat *RESTRICT ClickRemoval = Device->ClickRemoval;
    ALfloat *RESTRICT PendingClicks = Device->PendingClicks;
    ALfloat DrySend[MaxChannels];
    ALuint pos;
    ALuint c;
    (void)Source;

    for(c = 0;c < MaxChannels;c++)
        DrySend[c] = params->Gains[srcchan][c];

    pos = 0;
    if(OutPos == 0)
    {
        for(c = 0;c < MaxChannels;c++)
            ClickRemoval[c] -= data[pos]*DrySend[c];
    }
    for(c = 0;c < MaxChannels;c++)
    {
        const __m128 gain = _mm_set1_ps(DrySend[c]);
        for(pos = 0;pos < BufferSize-3;pos += 4)
        {
            const __m128 val4 = _mm_load_ps(&data[pos]);
            __m128 dry4 = _mm_load_ps(&DryBuffer[c][OutPos+pos]);
            dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain));
            _mm_store_ps(&DryBuffer[c][OutPos+pos], dry4);
        }
    }
    if(pos < BufferSize)
    {
        ALuint oldpos = pos;
        for(c = 0;c < MaxChannels;c++)
        {
            pos = oldpos;
            for(;pos < BufferSize;pos++)
                DryBuffer[c][OutPos+pos] += data[pos]*DrySend[c];
        }
    }
    if(OutPos+pos == SamplesToDo)
    {
        for(c = 0;c < MaxChannels;c++)
            PendingClicks[c] += data[pos]*DrySend[c];
    }
}
#define NO_MIXDIRECT


#define SUFFIX SSE
#include "mixer_inc.c"
#undef SUFFIX
