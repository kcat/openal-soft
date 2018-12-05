
#include "config.h"

#include <cmath>

#include "AL/alc.h"
#include "AL/al.h"

#include "alMain.h"
#include "defs.h"


void BiquadFilter_setParams(BiquadFilter *filter, BiquadType type, float gain, float f0norm, float rcpQ)
{
    float alpha, sqrtgain_alpha_2;
    float w0, sin_w0, cos_w0;
    float a[3] = { 1.0f, 0.0f, 0.0f };
    float b[3] = { 1.0f, 0.0f, 0.0f };

    // Limit gain to -100dB
    assert(gain > 0.00001f);

    w0 = F_TAU * f0norm;
    sin_w0 = std::sin(w0);
    cos_w0 = std::cos(w0);
    alpha = sin_w0/2.0f * rcpQ;

    /* Calculate filter coefficients depending on filter type */
    switch(type)
    {
        case BiquadType::HighShelf:
            sqrtgain_alpha_2 = 2.0f * std::sqrt(gain) * alpha;
            b[0] =       gain*((gain+1.0f) + (gain-1.0f)*cos_w0 + sqrtgain_alpha_2);
            b[1] = -2.0f*gain*((gain-1.0f) + (gain+1.0f)*cos_w0                   );
            b[2] =       gain*((gain+1.0f) + (gain-1.0f)*cos_w0 - sqrtgain_alpha_2);
            a[0] =             (gain+1.0f) - (gain-1.0f)*cos_w0 + sqrtgain_alpha_2;
            a[1] =  2.0f*     ((gain-1.0f) - (gain+1.0f)*cos_w0                   );
            a[2] =             (gain+1.0f) - (gain-1.0f)*cos_w0 - sqrtgain_alpha_2;
            break;
        case BiquadType::LowShelf:
            sqrtgain_alpha_2 = 2.0f * std::sqrt(gain) * alpha;
            b[0] =       gain*((gain+1.0f) - (gain-1.0f)*cos_w0 + sqrtgain_alpha_2);
            b[1] =  2.0f*gain*((gain-1.0f) - (gain+1.0f)*cos_w0                   );
            b[2] =       gain*((gain+1.0f) - (gain-1.0f)*cos_w0 - sqrtgain_alpha_2);
            a[0] =             (gain+1.0f) + (gain-1.0f)*cos_w0 + sqrtgain_alpha_2;
            a[1] = -2.0f*     ((gain-1.0f) + (gain+1.0f)*cos_w0                   );
            a[2] =             (gain+1.0f) + (gain-1.0f)*cos_w0 - sqrtgain_alpha_2;
            break;
        case BiquadType::Peaking:
            gain = std::sqrt(gain);
            b[0] =  1.0f + alpha * gain;
            b[1] = -2.0f * cos_w0;
            b[2] =  1.0f - alpha * gain;
            a[0] =  1.0f + alpha / gain;
            a[1] = -2.0f * cos_w0;
            a[2] =  1.0f - alpha / gain;
            break;

        case BiquadType::LowPass:
            b[0] = (1.0f - cos_w0) / 2.0f;
            b[1] =  1.0f - cos_w0;
            b[2] = (1.0f - cos_w0) / 2.0f;
            a[0] =  1.0f + alpha;
            a[1] = -2.0f * cos_w0;
            a[2] =  1.0f - alpha;
            break;
        case BiquadType::HighPass:
            b[0] =  (1.0f + cos_w0) / 2.0f;
            b[1] = -(1.0f + cos_w0);
            b[2] =  (1.0f + cos_w0) / 2.0f;
            a[0] =   1.0f + alpha;
            a[1] =  -2.0f * cos_w0;
            a[2] =   1.0f - alpha;
            break;
        case BiquadType::BandPass:
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


void BiquadFilter_processC(BiquadFilter *filter, float *RESTRICT dst, const float *RESTRICT src, int numsamples)
{
    ASSUME(numsamples > 0);

    const float b0{filter->b0};
    const float b1{filter->b1};
    const float b2{filter->b2};
    const float a1{filter->a1};
    const float a2{filter->a2};
    float z1{filter->z1};
    float z2{filter->z2};

    /* Processing loop is Transposed Direct Form II. This requires less storage
     * compared to Direct Form I (only two delay components, instead of a four-
     * sample history; the last two inputs and outputs), and works better for
     * floating-point which favors summing similarly-sized values while being
     * less bothered by overflow.
     *
     * See: http://www.earlevel.com/main/2003/02/28/biquads/
     */
    auto proc_sample = [b0,b1,b2,a1,a2,&z1,&z2](float input) noexcept -> float
    {
        float output = input*b0 + z1;
        z1 = input*b1 - output*a1 + z2;
        z2 = input*b2 - output*a2;
        return output;
    };
    std::transform<const float*RESTRICT>(src, src+numsamples, dst, proc_sample);

    filter->z1 = z1;
    filter->z2 = z2;
}
