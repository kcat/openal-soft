
#include "cubic_tables.h"

#include <array>
#include <cmath>
#include <cstddef>

#include "alnumbers.h"
#include "cubic_defs.h"


namespace {

struct GaussFilterArray {
    alignas(16) std::array<CubicCoefficients,CubicPhaseCount> mTable{};

    /* This filter table is inspired by the gaussian-like filter found in the
     * SNES. This is based on the public domain code developed by Near, with
     * the help of Ryphecha and nocash, from the nesdev.org forums.
     *
     * <https://forums.nesdev.org/viewtopic.php?p=251534#p251534>
     *
     * Additional changes were made here, the most obvious being that is has
     * full floating-point precision instead of 11-bit fixed-point, but also an
     * offset adjustment for the phase coefficients to more cleanly transition
     * from the end of one sample set to the start of the next.
     */
    [[nodiscard]]
    auto GetCoeff(std::size_t idx) noexcept -> double
    {
        static constexpr double IndexScale{512.0 / double{CubicPhaseCount*2}};
        const double k{0.5 + static_cast<double>(idx)*IndexScale};
        if(k > 512.0) return 0.0;
        const double s{ std::sin(al::numbers::pi*1.280/1024 * k)};
        const double t{(std::cos(al::numbers::pi*2.000/1023 * k) - 1.0) * 0.50};
        const double u{(std::cos(al::numbers::pi*4.000/1023 * k) - 1.0) * 0.08};
        return s * (t + u + 1.0) / k;
    }

    GaussFilterArray()
    {
        /* Fill in the main coefficients. */
        for(std::size_t pi{0};pi < CubicPhaseCount;++pi)
        {
            const double coeff0{GetCoeff(CubicPhaseCount + pi)};
            const double coeff1{GetCoeff(pi)};
            const double coeff2{GetCoeff(CubicPhaseCount - pi)};
            const double coeff3{GetCoeff(CubicPhaseCount*2 - pi)};

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
        mTable[pi].mDeltas[1] = mTable[0].mCoeffs[2] - mTable[pi].mCoeffs[1];
        mTable[pi].mDeltas[2] = mTable[0].mCoeffs[1] - mTable[pi].mCoeffs[2];
        mTable[pi].mDeltas[3] = mTable[0].mCoeffs[0] - mTable[pi].mCoeffs[3];
    }

    [[nodiscard]] constexpr auto& getTable() const noexcept { return mTable; }
};

const GaussFilterArray GaussFilter{};

} // namespace

const CubicTable gGaussianFilter{GaussFilter.getTable()};
