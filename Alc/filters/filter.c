
#include "config.h"

#include "AL/alc.h"
#include "AL/al.h"

#include "alMain.h"
#include "defs.h"

extern inline void BiquadState_clear(BiquadState *filter);
extern inline void BiquadState_copyParams(BiquadState *restrict dst, const BiquadState *restrict src);
extern inline void BiquadState_processPassthru(BiquadState *filter, const ALfloat *restrict src, ALsizei numsamples);
extern inline ALfloat calc_rcpQ_from_slope(ALfloat gain, ALfloat slope);
extern inline ALfloat calc_rcpQ_from_bandwidth(ALfloat f0norm, ALfloat bandwidth);


void BiquadState_setParams(BiquadState *filter, BiquadType type, ALfloat gain, ALfloat f0norm, ALfloat rcpQ)
{
    ALfloat alpha, sqrtgain_alpha_2;
    ALfloat w0, sin_w0, cos_w0;
    ALfloat a[3] = { 1.0f, 0.0f, 0.0f };
    ALfloat b[3] = { 1.0f, 0.0f, 0.0f };

    // Limit gain to -100dB
    assert(gain > 0.00001f);

    w0 = F_TAU * f0norm;
    sin_w0 = sinf(w0);
    cos_w0 = cosf(w0);
    alpha = sin_w0/2.0f * rcpQ;

    /* Calculate filter coefficients depending on filter type */
    switch(type)
    {
        case BiquadType_HighShelf:
            sqrtgain_alpha_2 = 2.0f * sqrtf(gain) * alpha;
            b[0] =       gain*((gain+1.0f) + (gain-1.0f)*cos_w0 + sqrtgain_alpha_2);
            b[1] = -2.0f*gain*((gain-1.0f) + (gain+1.0f)*cos_w0                   );
            b[2] =       gain*((gain+1.0f) + (gain-1.0f)*cos_w0 - sqrtgain_alpha_2);
            a[0] =             (gain+1.0f) - (gain-1.0f)*cos_w0 + sqrtgain_alpha_2;
            a[1] =  2.0f*     ((gain-1.0f) - (gain+1.0f)*cos_w0                   );
            a[2] =             (gain+1.0f) - (gain-1.0f)*cos_w0 - sqrtgain_alpha_2;
            break;
        case BiquadType_LowShelf:
            sqrtgain_alpha_2 = 2.0f * sqrtf(gain) * alpha;
            b[0] =       gain*((gain+1.0f) - (gain-1.0f)*cos_w0 + sqrtgain_alpha_2);
            b[1] =  2.0f*gain*((gain-1.0f) - (gain+1.0f)*cos_w0                   );
            b[2] =       gain*((gain+1.0f) - (gain-1.0f)*cos_w0 - sqrtgain_alpha_2);
            a[0] =             (gain+1.0f) + (gain-1.0f)*cos_w0 + sqrtgain_alpha_2;
            a[1] = -2.0f*     ((gain-1.0f) + (gain+1.0f)*cos_w0                   );
            a[2] =             (gain+1.0f) + (gain-1.0f)*cos_w0 - sqrtgain_alpha_2;
            break;
        case BiquadType_Peaking:
            gain = sqrtf(gain);
            b[0] =  1.0f + alpha * gain;
            b[1] = -2.0f * cos_w0;
            b[2] =  1.0f - alpha * gain;
            a[0] =  1.0f + alpha / gain;
            a[1] = -2.0f * cos_w0;
            a[2] =  1.0f - alpha / gain;
            break;

        case BiquadType_LowPass:
            b[0] = (1.0f - cos_w0) / 2.0f;
            b[1] =  1.0f - cos_w0;
            b[2] = (1.0f - cos_w0) / 2.0f;
            a[0] =  1.0f + alpha;
            a[1] = -2.0f * cos_w0;
            a[2] =  1.0f - alpha;
            break;
        case BiquadType_HighPass:
            b[0] =  (1.0f + cos_w0) / 2.0f;
            b[1] = -(1.0f + cos_w0);
            b[2] =  (1.0f + cos_w0) / 2.0f;
            a[0] =   1.0f + alpha;
            a[1] =  -2.0f * cos_w0;
            a[2] =   1.0f - alpha;
            break;
        case BiquadType_BandPass:
            b[0] =  alpha;
            b[1] =  0;
            b[2] = -alpha;
            a[0] =  1.0f + alpha;
            a[1] = -2.0f * cos_w0;
            a[2] =  1.0f - alpha;
            break;
    }

    filter->a1 = a[1] / a[0];
    filter->a2 = a[2] / a[0];
    filter->b0 = b[0] / a[0];
    filter->b1 = b[1] / a[0];
    filter->b2 = b[2] / a[0];
}


void BiquadState_processC(BiquadState *filter, ALfloat *restrict dst, const ALfloat *restrict src, ALsizei numsamples)
{
    ALsizei i;
    if(LIKELY(numsamples > 1))
    {
        ALfloat x0 = filter->x[0];
        ALfloat x1 = filter->x[1];
        ALfloat y0 = filter->y[0];
        ALfloat y1 = filter->y[1];

        for(i = 0;i < numsamples;i++)
        {
            dst[i] = filter->b0* src[i] +
                     filter->b1*x0 + filter->b2*x1 -
                     filter->a1*y0 - filter->a2*y1;
            y1 = y0; y0 = dst[i];
            x1 = x0; x0 = src[i];
        }

        filter->x[0] = x0;
        filter->x[1] = x1;
        filter->y[0] = y0;
        filter->y[1] = y1;
    }
    else if(numsamples == 1)
    {
        dst[0] = filter->b0 * src[0] +
                 filter->b1 * filter->x[0] +
                 filter->b2 * filter->x[1] -
                 filter->a1 * filter->y[0] -
                 filter->a2 * filter->y[1];
        filter->x[1] = filter->x[0];
        filter->x[0] = src[0];
        filter->y[1] = filter->y[0];
        filter->y[0] = dst[0];
    }
}
