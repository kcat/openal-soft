
#include "config.h"

#include "biquad.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "opthelpers.h"


template<typename Real>
void BiquadFilterR<Real>::setParams(BiquadType type, Real f0norm, Real gain, Real rcpQ)
{
    // Limit gain to -100dB
    assert(gain > 0.00001f);

    const Real w0{al::MathDefs<Real>::Tau() * f0norm};
    const Real sin_w0{std::sin(w0)};
    const Real cos_w0{std::cos(w0)};
    const Real alpha{sin_w0/2.0f * rcpQ};

    Real sqrtgain_alpha_2;
    Real a[3]{ 1.0f, 0.0f, 0.0f };
    Real b[3]{ 1.0f, 0.0f, 0.0f };

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

template<typename Real>
void BiquadFilterR<Real>::process(const al::span<const Real> src, Real *dst)
{
    const Real b0{mB0};
    const Real b1{mB1};
    const Real b2{mB2};
    const Real a1{mA1};
    const Real a2{mA2};
    Real z1{mZ1};
    Real z2{mZ2};

    /* Processing loop is Transposed Direct Form II. This requires less storage
     * compared to Direct Form I (only two delay components, instead of a four-
     * sample history; the last two inputs and outputs), and works better for
     * floating-point which favors summing similarly-sized values while being
     * less bothered by overflow.
     *
     * See: http://www.earlevel.com/main/2003/02/28/biquads/
     */
    auto proc_sample = [b0,b1,b2,a1,a2,&z1,&z2](Real input) noexcept -> Real
    {
        const Real output{input*b0 + z1};
        z1 = input*b1 - output*a1 + z2;
        z2 = input*b2 - output*a2;
        return output;
    };
    std::transform(src.cbegin(), src.cend(), dst, proc_sample);

    mZ1 = z1;
    mZ2 = z2;
}

template class BiquadFilterR<float>;
template class BiquadFilterR<double>;
