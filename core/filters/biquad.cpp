
#include "config.h"

#include "biquad.h"

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <ranges>

#include "alnumeric.h"
#include "gsl/gsl"
#include "opthelpers.h"


namespace {

/* The number of steps for the filter to transition from the current to target
 * coefficients. More steps create a smoother transition, but increases the
 * amount of time to reach the target coefficients.
 */
constexpr auto InterpSteps = 8;
/* The number of sample frames to process for each interpolation step. More
 * sample frames improve performance, but increases the amount of time to reach
 * the target coefficients.
 */
constexpr auto SamplesPerStep = 32;
constexpr auto SamplesPerStepMask = SamplesPerStep-1;

static_assert(std::popcount(gsl::narrow<u32>(SamplesPerStep)) == 1,
    "SamplesPerStep must be a power of 2");

/* Sets dst to the given value, returns true if it's meaningfully different. */
[[nodiscard]]
auto check_set(f32 &dst, f32 const value) noexcept -> bool
{
    const auto is_diff = !(std::abs(value - dst) <= 0.015625f/* 1/64 */);
    dst = value;
    return is_diff;
}

} // namespace


auto BiquadFilter::SetParams(BiquadType const type, f32 const f0norm, f32 gain, f32 const rcpQ,
    Coefficients &coeffs) -> bool
{
    /* HACK: Limit gain to -100dB. This shouldn't ever happen, all callers
     * already clamp to minimum of 0.001, or have a limited range of values
     * that don't go below 0.126. But it seems to with some callers. This needs
     * to be investigated.
     */
    gain = std::max(gain, 0.00001f);

    auto const w0 = std::numbers::pi_v<f32>*2.0f * f0norm;
    auto const sin_w0 = std::sin(w0);
    auto const cos_w0 = std::cos(w0);
    auto const alpha = sin_w0/2.0f * rcpQ;

    auto sqrtgain_alpha_2 = f32{};
    auto a = std::array<f32, 3>{{1.0f, 0.0f, 0.0f}};
    auto b = std::array<f32, 3>{{1.0f, 0.0f, 0.0f}};

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

    auto is_diff = check_set(coeffs.mB0, b[0] / a[0]);
    is_diff |= check_set(coeffs.mB1, b[1] / a[0]);
    is_diff |= check_set(coeffs.mB2, b[2] / a[0]);
    is_diff |= check_set(coeffs.mA1, a[1] / a[0]);
    is_diff |= check_set(coeffs.mA2, a[2] / a[0]);
    return is_diff;
}

void BiquadInterpFilter::setParams(BiquadType const type, f32 const f0norm, f32 const gain,
    f32 const rcpQ)
{
    if(!SetParams(type, f0norm, gain, rcpQ, mTargetCoeffs))
    {
        if(mCounter <= 0)
        {
            mCounter = 0;
            mCoeffs = mTargetCoeffs;
        }
    }
    else if(mCounter >= 0)
        mCounter = InterpSteps*SamplesPerStep;
    else
    {
        mCounter = 0;
        mCoeffs = mTargetCoeffs;
    }
}

void BiquadInterpFilter::copyParamsFrom(BiquadInterpFilter const &other) noexcept
{
    auto is_diff = check_set(mTargetCoeffs.mB0, other.mTargetCoeffs.mB0);
    is_diff |= check_set(mTargetCoeffs.mB1, other.mTargetCoeffs.mB1);
    is_diff |= check_set(mTargetCoeffs.mB2, other.mTargetCoeffs.mB2);
    is_diff |= check_set(mTargetCoeffs.mA1, other.mTargetCoeffs.mA1);
    is_diff |= check_set(mTargetCoeffs.mA2, other.mTargetCoeffs.mA2);
    if(!is_diff)
    {
        if(mCounter <= 0)
        {
            mCounter = 0;
            mCoeffs = mTargetCoeffs;
        }
    }
    else if(mCounter >= 0)
        mCounter = InterpSteps*SamplesPerStep;
    else
    {
        mCounter = 0;
        mCoeffs = mTargetCoeffs;
    }
}


void BiquadFilter::process(std::span<f32 const> const src, std::span<f32> const dst)
{
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
    std::ranges::transform(src, dst.begin(), [coeffs=mCoeffs,&z1,&z2](f32 const x) noexcept -> f32
    {
        auto const y = x*coeffs.mB0 + z1;
        z1 = x*coeffs.mB1 - y*coeffs.mA1 + z2;
        z2 = x*coeffs.mB2 - y*coeffs.mA2;
        return y;
    });

    mZ1 = z1;
    mZ2 = z2;
}

void BiquadInterpFilter::process(std::span<f32 const> src, std::span<f32> dst)
{
    if(mCounter > 0)
    {
        auto counter = mCounter / SamplesPerStep;
        auto steprem = gsl::narrow_cast<usize>(SamplesPerStep - (mCounter&SamplesPerStepMask));
        while(counter > 0)
        {
            auto const td = std::min(steprem, src.size());
            BiquadFilter::process(src | std::views::take(td), dst);

            steprem -= td;
            if(steprem)
            {
                steprem = SamplesPerStep - steprem;
                mCounter = (counter*SamplesPerStep) | gsl::narrow_cast<i32>(steprem);
                return;
            }

            src = src.subspan(td);
            dst = dst.subspan(td);

            steprem = SamplesPerStep;
            --counter;
            if(!counter)
            {
                mCounter = 0;
                mCoeffs = mTargetCoeffs;
                break;
            }

            auto const a = 1.0f / static_cast<f32>(counter+1);
            mCoeffs.mB0 = lerpf(mCoeffs.mB0, mTargetCoeffs.mB0, a);
            mCoeffs.mB1 = lerpf(mCoeffs.mB1, mTargetCoeffs.mB1, a);
            mCoeffs.mB2 = lerpf(mCoeffs.mB2, mTargetCoeffs.mB2, a);
            mCoeffs.mA1 = lerpf(mCoeffs.mA1, mTargetCoeffs.mA1, a);
            mCoeffs.mA2 = lerpf(mCoeffs.mA2, mTargetCoeffs.mA2, a);

            if(src.empty())
            {
                mCounter = counter*SamplesPerStep;
                return;
            }
        }
    }

    BiquadFilter::process(src, dst);
}


void BiquadFilter::dualProcess(BiquadFilter &other, std::span<f32 const> const src,
    std::span<f32> const dst)
{
    ASSUME(this != &other);
    auto z01 = mZ1;
    auto z02 = mZ2;
    auto z11 = other.mZ1;
    auto z12 = other.mZ2;

    std::ranges::transform(src, dst.begin(),
        [coeffs0=mCoeffs,coeffs1=other.mCoeffs,&z01,&z02,&z11,&z12](f32 const x0) noexcept -> f32
    {
        auto const y0 = x0*coeffs0.mB0 + z01;
        z01 = x0*coeffs0.mB1 - y0*coeffs0.mA1 + z02;
        z02 = x0*coeffs0.mB2 - y0*coeffs0.mA2;
        auto const x1 = y0;

        auto const y1 = x1*coeffs1.mB0 + z11;
        z11 = x1*coeffs1.mB1 - y1*coeffs1.mA1 + z12;
        z12 = x1*coeffs1.mB2 - y1*coeffs1.mA2;
        return y1;
    });

    mZ1 = z01;
    mZ2 = z02;
    other.mZ1 = z11;
    other.mZ2 = z12;
}

void BiquadInterpFilter::dualProcess(BiquadInterpFilter &other, std::span<f32 const> src,
    std::span<f32> dst)
{
    ASSUME(this != &other);
    if(auto const maxcounter = std::max(mCounter, other.mCounter); maxcounter > 0)
    {
        auto counter = maxcounter / SamplesPerStep;
        auto steprem = gsl::narrow_cast<usize>(SamplesPerStep - (maxcounter&SamplesPerStepMask));
        while(counter > 0)
        {
            auto const td = std::min(steprem, src.size());
            BiquadFilter::dualProcess(other, src | std::views::take(td), dst);

            steprem -= td;
            if(steprem)
            {
                steprem = SamplesPerStep - steprem;
                mCounter = (counter*SamplesPerStep) | gsl::narrow_cast<i32>(steprem);
                other.mCounter = mCounter;
                return;
            }

            src = src.subspan(td);
            dst = dst.subspan(td);

            steprem = SamplesPerStep;
            --counter;
            if(!counter)
            {
                mCounter = 0;
                mCoeffs = mTargetCoeffs;
                other.mCounter = 0;
                other.mCoeffs = other.mTargetCoeffs;
                break;
            }

            auto const a = 1.0f / static_cast<f32>(counter+1);
            mCoeffs.mB0 = lerpf(mCoeffs.mB0, mTargetCoeffs.mB0, a);
            mCoeffs.mB1 = lerpf(mCoeffs.mB1, mTargetCoeffs.mB1, a);
            mCoeffs.mB2 = lerpf(mCoeffs.mB2, mTargetCoeffs.mB2, a);
            mCoeffs.mA1 = lerpf(mCoeffs.mA1, mTargetCoeffs.mA1, a);
            mCoeffs.mA2 = lerpf(mCoeffs.mA2, mTargetCoeffs.mA2, a);
            other.mCoeffs.mB0 = lerpf(other.mCoeffs.mB0, other.mTargetCoeffs.mB0, a);
            other.mCoeffs.mB1 = lerpf(other.mCoeffs.mB1, other.mTargetCoeffs.mB1, a);
            other.mCoeffs.mB2 = lerpf(other.mCoeffs.mB2, other.mTargetCoeffs.mB2, a);
            other.mCoeffs.mA1 = lerpf(other.mCoeffs.mA1, other.mTargetCoeffs.mA1, a);
            other.mCoeffs.mA2 = lerpf(other.mCoeffs.mA2, other.mTargetCoeffs.mA2, a);

            if(src.empty())
            {
                mCounter = counter*SamplesPerStep;
                other.mCounter = mCounter;
                return;
            }
        }
    }

    BiquadFilter::dualProcess(other, src, dst);
}
