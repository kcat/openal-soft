
#include "cubic_tables.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "alnumeric.h"
#include "cubic_defs.h"

/* These gaussian filter tables are inspired by the gaussian-like filter found
 * in the SNES. This is based on the public domain code developed by Near, with
 * the help of Ryphecha and nocash, from the nesdev.org forums.
 *
 * <https://forums.nesdev.org/viewtopic.php?p=251534#p251534>
 *
 * Additional changes were made here, the most obvious being that it has full
 * floating-point precision instead of 11-bit fixed-point, but also an offset
 * adjustment for the coefficients to better preserve phase.
 */
namespace {

[[nodiscard]]
auto GetCoeff(double idx) noexcept -> double
{
    const auto k = 0.5 + idx;
    if(k > 512.0) return 0.0;
    const auto s =  std::sin(std::numbers::pi*1.280/1024.0 * k);
    const auto t = (std::cos(std::numbers::pi*2.000/1023.0 * k) - 1.0) * 0.50;
    const auto u = (std::cos(std::numbers::pi*4.000/1023.0 * k) - 1.0) * 0.08;
    return s * (t + u + 1.0) / k;
}

} // namespace

GaussianTable::GaussianTable() noexcept
{
    static constexpr double IndexScale{512.0 / double{CubicPhaseCount*2}};
    /* Fill in the main coefficients. */
    for(std::size_t pi{0};pi < CubicPhaseCount;++pi)
    {
        const double coeff0{GetCoeff(static_cast<double>(CubicPhaseCount + pi)*IndexScale)};
        const double coeff1{GetCoeff(static_cast<double>(pi)*IndexScale)};
        const double coeff2{GetCoeff(static_cast<double>(CubicPhaseCount - pi)*IndexScale)};
        const double coeff3{GetCoeff(static_cast<double>(CubicPhaseCount*2_uz-pi)*IndexScale)};

        const double scale{1.0 / (coeff0 + coeff1 + coeff2 + coeff3)};
        mTable[pi].mCoeffs[0] = static_cast<float>(coeff0 * scale);
        mTable[pi].mCoeffs[1] = static_cast<float>(coeff1 * scale);
        mTable[pi].mCoeffs[2] = static_cast<float>(coeff2 * scale);
        mTable[pi].mCoeffs[3] = static_cast<float>(coeff3 * scale);
    }

    /* Fill in the coefficient deltas. */
    for(std::size_t pi{0};pi < CubicPhaseCount-1;++pi)
    {
        mTable[pi].mDeltas[0] = mTable[pi+1].mCoeffs[0] - mTable[pi].mCoeffs[0];
        mTable[pi].mDeltas[1] = mTable[pi+1].mCoeffs[1] - mTable[pi].mCoeffs[1];
        mTable[pi].mDeltas[2] = mTable[pi+1].mCoeffs[2] - mTable[pi].mCoeffs[2];
        mTable[pi].mDeltas[3] = mTable[pi+1].mCoeffs[3] - mTable[pi].mCoeffs[3];
    }

    const std::size_t pi{CubicPhaseCount - 1};
    mTable[pi].mDeltas[0] =                 0.0f - mTable[pi].mCoeffs[0];
    mTable[pi].mDeltas[1] = mTable[0].mCoeffs[0] - mTable[pi].mCoeffs[1];
    mTable[pi].mDeltas[2] = mTable[0].mCoeffs[1] - mTable[pi].mCoeffs[2];
    mTable[pi].mDeltas[3] = mTable[0].mCoeffs[2] - mTable[pi].mCoeffs[3];
}

consteval SplineTable::SplineTable() noexcept
{
    constexpr auto third = 1.0/3.0;
    constexpr auto sixth = 1.0/6.0;
    /* This filter table is based on a Catmull-Rom spline. It retains more of
     * the original high-frequency content, at the cost of increased harmonics.
     */
    for(std::size_t pi{0};pi < CubicPhaseCount;++pi)
    {
        const auto mu = static_cast<double>(pi) / double{CubicPhaseCount};
        const auto mu2 = mu*mu;
        const auto mu3 = mu*mu2;
        mTable[pi].mCoeffs[0] = static_cast<float>(      -third*mu + 0.5*mu2  - sixth*mu3);
        mTable[pi].mCoeffs[1] = static_cast<float>(1.0 -    0.5*mu -     mu2  +   0.5*mu3);
        mTable[pi].mCoeffs[2] = static_cast<float>(             mu + 0.5*mu2  -   0.5*mu3);
        mTable[pi].mCoeffs[3] = static_cast<float>(      -sixth*mu            + sixth*mu3);
    }

    for(std::size_t pi{0};pi < CubicPhaseCount-1;++pi)
    {
        mTable[pi].mDeltas[0] = mTable[pi+1].mCoeffs[0] - mTable[pi].mCoeffs[0];
        mTable[pi].mDeltas[1] = mTable[pi+1].mCoeffs[1] - mTable[pi].mCoeffs[1];
        mTable[pi].mDeltas[2] = mTable[pi+1].mCoeffs[2] - mTable[pi].mCoeffs[2];
        mTable[pi].mDeltas[3] = mTable[pi+1].mCoeffs[3] - mTable[pi].mCoeffs[3];
    }

    constexpr auto pi = std::size_t{CubicPhaseCount - 1};
    mTable[pi].mDeltas[0] =                 0.0f - mTable[pi].mCoeffs[0];
    mTable[pi].mDeltas[1] = mTable[0].mCoeffs[0] - mTable[pi].mCoeffs[1];
    mTable[pi].mDeltas[2] = mTable[0].mCoeffs[1] - mTable[pi].mCoeffs[2];
    mTable[pi].mDeltas[3] = mTable[0].mCoeffs[2] - mTable[pi].mCoeffs[3];
}
constinit const SplineTable gSplineFilter;


CubicFilter::CubicFilter() noexcept
{
    static constexpr double IndexScale{512.0 / double{sTableSteps*2}};
    /* Only half the coefficients need to be iterated here, since Coeff2 and
     * Coeff3 are just Coeff1 and Coeff0 in reverse respectively.
     */
    for(size_t i{0};i < sTableSteps/2 + 1;++i)
    {
        const double coeff0{GetCoeff(static_cast<double>(sTableSteps + i)*IndexScale)};
        const double coeff1{GetCoeff(static_cast<double>(i)*IndexScale)};
        const double coeff2{GetCoeff(static_cast<double>(sTableSteps - i)*IndexScale)};
        const double coeff3{GetCoeff(static_cast<double>(sTableSteps*2_uz - i)*IndexScale)};

        const double scale{1.0 / (coeff0 + coeff1 + coeff2 + coeff3)};
        mFilter[sTableSteps + i] = static_cast<float>(coeff0 * scale);
        mFilter[i] = static_cast<float>(coeff1 * scale);
        mFilter[sTableSteps - i] = static_cast<float>(coeff2 * scale);
        mFilter[sTableSteps*2 - i] = static_cast<float>(coeff3 * scale);
    }
}
