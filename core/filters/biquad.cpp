
#include "config.h"

#include "biquad.h"

#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>


void BiquadFilter::setParams(BiquadType type, float f0norm, float gain, float rcpQ)
{
    /* HACK: Limit gain to -100dB. This shouldn't ever happen, all callers
     * already clamp to minimum of 0.001, or have a limited range of values
     * that don't go below 0.126. But it seems to with some callers. This needs
     * to be investigated.
     */
    gain = std::max(gain, 0.00001f);

    const auto w0 = std::numbers::pi_v<float>*2.0f * f0norm;
    const auto sin_w0 = std::sin(w0);
    const auto cos_w0 = std::cos(w0);
    const auto alpha = sin_w0/2.0f * rcpQ;

    auto sqrtgain_alpha_2 = float{};
    auto a = std::array<float,3>{{1.0f, 0.0f, 0.0f}};
    auto b = std::array<float,3>{{1.0f, 0.0f, 0.0f}};

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
            b[1] =  0.0f;
            b[2] = -alpha;
            a[0] =  1.0f + alpha;
            a[1] = -2.0f * cos_w0;
            a[2] =  1.0f - alpha;
            break;
    }

    mA1 = a[1] / a[0];
    mA2 = a[2] / a[0];
    mB0 = b[0] / a[0];
    mB1 = b[1] / a[0];
    mB2 = b[2] / a[0];
}

void BiquadFilter::process(const std::span<const float> src, const std::span<float> dst)
{
    const auto b0 = mB0;
    const auto b1 = mB1;
    const auto b2 = mB2;
    const auto a1 = mA1;
    const auto a2 = mA2;
    auto z1 = mZ1;
    auto z2 = mZ2;

    /* Processing loop is Transposed Direct Form II. This requires less storage
     * compared to Direct Form I (only two delay components, instead of a four-
     * sample history; the last two inputs and outputs), and works better for
     * floating-point which favors summing similarly-sized values while being
     * less bothered by overflow.
     *
     * See: http://www.earlevel.com/main/2003/02/28/biquads/
     */
    std::transform(src.begin(), src.end(), dst.begin(),
        [b0,b1,b2,a1,a2,&z1,&z2](float input) noexcept -> float
    {
        const auto output = input*b0 + z1;
        z1 = input*b1 - output*a1 + z2;
        z2 = input*b2 - output*a2;
        return output;
    });

    mZ1 = z1;
    mZ2 = z2;
}

void BiquadFilter::dualProcess(BiquadFilter &other, const std::span<const float> src,
    const std::span<float> dst)
{
    const auto b00 = mB0;
    const auto b01 = mB1;
    const auto b02 = mB2;
    const auto a01 = mA1;
    const auto a02 = mA2;
    const auto b10 = other.mB0;
    const auto b11 = other.mB1;
    const auto b12 = other.mB2;
    const auto a11 = other.mA1;
    const auto a12 = other.mA2;
    auto z01 = mZ1;
    auto z02 = mZ2;
    auto z11 = other.mZ1;
    auto z12 = other.mZ2;

    std::transform(src.begin(), src.end(), dst.begin(),
        [b00,b01,b02,a01,a02,b10,b11,b12,a11,a12,&z01,&z02,&z11,&z12](float input) noexcept
        -> float
    {
        const auto tmpout = input*b00 + z01;
        z01 = input*b01 - tmpout*a01 + z02;
        z02 = input*b02 - tmpout*a02;
        input = tmpout;

        const auto output = input*b10 + z11;
        z11 = input*b11 - output*a11 + z12;
        z12 = input*b12 - output*a12;
        return output;
    });

    mZ1 = z01;
    mZ2 = z02;
    other.mZ1 = z11;
    other.mZ2 = z12;
}
