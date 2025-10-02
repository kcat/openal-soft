
#include "cubic_tables.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <ranges>

#include "alnumeric.h"
#include "cubic_defs.h"
#include "gsl/gsl"

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
    static constexpr auto IndexScale = 512.0 / double{CubicPhaseCount*2};
    /* Fill in the main coefficients. */
    for(const auto pi : std::views::iota(0_uz, std::size_t{CubicPhaseCount}))
    {
        const auto coeff0 = GetCoeff(gsl::narrow_cast<double>(CubicPhaseCount + pi)*IndexScale);
        const auto coeff1 = GetCoeff(gsl::narrow_cast<double>(pi)*IndexScale);
        const auto coeff2 = GetCoeff(gsl::narrow_cast<double>(CubicPhaseCount - pi)*IndexScale);
        const auto coeff3 = GetCoeff(gsl::narrow_cast<double>(CubicPhaseCount*2_uz - pi)
            *IndexScale);

        const auto scale = 1.0 / (coeff0 + coeff1 + coeff2 + coeff3);
        mTable[pi].mCoeffs[0] = gsl::narrow_cast<float>(coeff0 * scale);
        mTable[pi].mCoeffs[1] = gsl::narrow_cast<float>(coeff1 * scale);
        mTable[pi].mCoeffs[2] = gsl::narrow_cast<float>(coeff2 * scale);
        mTable[pi].mCoeffs[3] = gsl::narrow_cast<float>(coeff3 * scale);
    }

    /* Fill in the coefficient deltas. */
    for(const auto pi : std::views::iota(0_uz, CubicPhaseCount-1_uz))
    {
        mTable[pi].mDeltas[0] = mTable[pi+1].mCoeffs[0] - mTable[pi].mCoeffs[0];
        mTable[pi].mDeltas[1] = mTable[pi+1].mCoeffs[1] - mTable[pi].mCoeffs[1];
        mTable[pi].mDeltas[2] = mTable[pi+1].mCoeffs[2] - mTable[pi].mCoeffs[2];
        mTable[pi].mDeltas[3] = mTable[pi+1].mCoeffs[3] - mTable[pi].mCoeffs[3];
    }

    constexpr auto pi = CubicPhaseCount - 1_uz;
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
    for(const auto pi : std::views::iota(0_uz, std::size_t{CubicPhaseCount}))
    {
        const auto mu = gsl::narrow_cast<double>(pi) / double{CubicPhaseCount};
        const auto mu2 = mu*mu;
        const auto mu3 = mu*mu2;
        mTable[pi].mCoeffs[0] = gsl::narrow_cast<float>(      -third*mu + 0.5*mu2  - sixth*mu3);
        mTable[pi].mCoeffs[1] = gsl::narrow_cast<float>(1.0 -    0.5*mu -     mu2  +   0.5*mu3);
        mTable[pi].mCoeffs[2] = gsl::narrow_cast<float>(             mu + 0.5*mu2  -   0.5*mu3);
        mTable[pi].mCoeffs[3] = gsl::narrow_cast<float>(      -sixth*mu            + sixth*mu3);
    }

    for(const auto pi : std::views::iota(0_uz, CubicPhaseCount-1_uz))
    {
        mTable[pi].mDeltas[0] = mTable[pi+1].mCoeffs[0] - mTable[pi].mCoeffs[0];
        mTable[pi].mDeltas[1] = mTable[pi+1].mCoeffs[1] - mTable[pi].mCoeffs[1];
        mTable[pi].mDeltas[2] = mTable[pi+1].mCoeffs[2] - mTable[pi].mCoeffs[2];
        mTable[pi].mDeltas[3] = mTable[pi+1].mCoeffs[3] - mTable[pi].mCoeffs[3];
    }

    constexpr auto pi = CubicPhaseCount - 1_uz;
    mTable[pi].mDeltas[0] =                 0.0f - mTable[pi].mCoeffs[0];
    mTable[pi].mDeltas[1] = mTable[0].mCoeffs[0] - mTable[pi].mCoeffs[1];
    mTable[pi].mDeltas[2] = mTable[0].mCoeffs[1] - mTable[pi].mCoeffs[2];
    mTable[pi].mDeltas[3] = mTable[0].mCoeffs[2] - mTable[pi].mCoeffs[3];
}
constinit const SplineTable gSplineFilter;


CubicFilter::CubicFilter() noexcept
{
    static constexpr auto IndexScale = 512.0 / double{sTableSteps*2};
    /* Only half the coefficients need to be iterated here, since Coeff2 and
     * Coeff3 are just Coeff1 and Coeff0 in reverse respectively.
     */
    for(const auto i : std::views::iota(0_uz, sTableSteps/2_uz + 1_uz))
    {
        const auto coeff0 = GetCoeff(gsl::narrow_cast<double>(sTableSteps + i)*IndexScale);
        const auto coeff1 = GetCoeff(gsl::narrow_cast<double>(i)*IndexScale);
        const auto coeff2 = GetCoeff(gsl::narrow_cast<double>(sTableSteps - i)*IndexScale);
        const auto coeff3 = GetCoeff(gsl::narrow_cast<double>(sTableSteps*2_uz - i)*IndexScale);

        const auto scale = 1.0 / (coeff0 + coeff1 + coeff2 + coeff3);
        mFilter[sTableSteps + i] = gsl::narrow_cast<float>(coeff0 * scale);
        mFilter[i] = gsl::narrow_cast<float>(coeff1 * scale);
        mFilter[sTableSteps - i] = gsl::narrow_cast<float>(coeff2 * scale);
        mFilter[sTableSteps*2 - i] = gsl::narrow_cast<float>(coeff3 * scale);
    }
}
