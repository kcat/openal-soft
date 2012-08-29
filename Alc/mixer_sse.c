#include "config.h"

#ifdef HAVE_XMMINTRIN_H
#include <xmmintrin.h>
#endif

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"


static __inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                     ALfloat (*RESTRICT Coeffs)[2],
                                     ALfloat (*RESTRICT CoeffStep)[2],
                                     ALfloat left, ALfloat right)
{
    const __m128 lrlr = { left, right, left, right };
    __m128 vals = { 0.0f, 0.0f, 0.0f, 0.0f };
    __m128 coeffs, coeffstep;
    ALuint c;
    for(c = 0;c < HRIR_LENGTH;c += 2)
    {
        const ALuint o0 = (Offset++)&HRIR_MASK;
        const ALuint o1 = (Offset++)&HRIR_MASK;

        coeffs = _mm_load_ps(&Coeffs[c][0]);
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o0][0]);
        vals = _mm_loadh_pi(vals, (__m64*)&Values[o1][0]);

        vals = _mm_add_ps(vals, _mm_mul_ps(coeffs, lrlr));
        _mm_storel_pi((__m64*)&Values[o0][0], vals);
        _mm_storeh_pi((__m64*)&Values[o1][0], vals);

        coeffstep = _mm_load_ps(&CoeffStep[c][0]);
        coeffs = _mm_add_ps(coeffs, coeffstep);
        _mm_store_ps(&Coeffs[c][0], coeffs);
    }
}

static __inline void ApplyCoeffs(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                 ALfloat (*RESTRICT Coeffs)[2],
                                 ALfloat left, ALfloat right)
{
    const __m128 lrlr = { left, right, left, right };
    __m128 vals = { 0.0f, 0.0f, 0.0f, 0.0f };
    __m128 coeffs;
    ALuint c;
    for(c = 0;c < HRIR_LENGTH;c += 2)
    {
        const ALuint o0 = (Offset++)&HRIR_MASK;
        const ALuint o1 = (Offset++)&HRIR_MASK;

        coeffs = _mm_load_ps(&Coeffs[c][0]);
        vals = _mm_loadl_pi(vals, (__m64*)&Values[o0][0]);
        vals = _mm_loadh_pi(vals, (__m64*)&Values[o1][0]);

        vals = _mm_add_ps(vals, _mm_mul_ps(coeffs, lrlr));
        _mm_storel_pi((__m64*)&Values[o0][0], vals);
        _mm_storeh_pi((__m64*)&Values[o1][0], vals);
    }
}


static __inline void ApplyValue(ALfloat *RESTRICT Output, ALfloat value, const ALfloat *DrySend)
{
    const __m128 val4 = _mm_set1_ps(value);
    ALuint c;
    for(c = 0;c < MaxChannels;c += 4)
    {
        const __m128 gains = _mm_load_ps(&DrySend[c]);
        __m128 out = _mm_load_ps(&Output[c]);
        out = _mm_add_ps(out, _mm_mul_ps(val4, gains));
        _mm_store_ps(&Output[c], out);
    }
}


#define SUFFIX SSE
#define Sampler point32
#include "mixer_inc.c"
#undef Sampler
#define Sampler lerp32
#include "mixer_inc.c"
#undef Sampler
#define Sampler cubic32
#include "mixer_inc.c"
#undef Sampler
#undef SUFFIX
