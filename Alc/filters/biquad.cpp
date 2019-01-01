
#include "config.h"

#include <cmath>

#include "AL/alc.h"
#include "AL/al.h"

#include "alMain.h"
#include "biquad.h"


void BiquadFilter::setParams(BiquadType type, float gain, float f0norm, float rcpQ)
{
    // Limit gain to -100dB
    assert(gain > 0.00001f);

    const float w0{F_TAU * f0norm};
    const float sin_w0{std::sin(w0)};
    const float cos_w0{std::cos(w0)};
    const float alpha{sin_w0/2.0f * rcpQ};

    float sqrtgain_alpha_2;
    float a[3]{ 1.0f, 0.0f, 0.0f };
    float b[3]{ 1.0f, 0.0f, 0.0f };

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

    a1 = a[1] / a[0];
    a2 = a[2] / a[0];
    b0 = b[0] / a[0];
    b1 = b[1] / a[0];
    b2 = b[2] / a[0];
}


void BiquadFilter::process(float *dst, const float *src, int numsamples)
{
    ASSUME(numsamples > 0);

    const float b0{this->b0};
    const float b1{this->b1};
    const float b2{this->b2};
    const float a1{this->a1};
    const float a2{this->a2};
    float z1{this->z1};
    float z2{this->z2};

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
    std::transform(src, src+numsamples, dst, proc_sample);

    this->z1 = z1;
    this->z2 = z2;
}
