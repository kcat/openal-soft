
#include "cubic_tables.h"

#include <array>
#include <stddef.h>

#include "cubic_defs.h"


namespace {

struct SplineFilterArray {
    alignas(16) std::array<CubicCoefficients,CubicPhaseCount> mTable{};

    constexpr SplineFilterArray()
    {
        /* Fill in the main coefficients. */
        for(size_t pi{0};pi < CubicPhaseCount;++pi)
        {
            const double mu{static_cast<double>(pi) / CubicPhaseCount};
            const double mu2{mu*mu}, mu3{mu2*mu};
            mTable[pi].mCoeffs[0] = static_cast<float>(-0.5*mu3 +      mu2 + -0.5*mu);
            mTable[pi].mCoeffs[1] = static_cast<float>( 1.5*mu3 + -2.5*mu2           + 1.0);
            mTable[pi].mCoeffs[2] = static_cast<float>(-1.5*mu3 +  2.0*mu2 +  0.5*mu);
            mTable[pi].mCoeffs[3] = static_cast<float>( 0.5*mu3 + -0.5*mu2);
        }

        /* Fill in the coefficient deltas. */
        for(size_t pi{0};pi < CubicPhaseCount-1;++pi)
        {
            mTable[pi].mDeltas[0] = mTable[pi+1].mCoeffs[0] - mTable[pi].mCoeffs[0];
            mTable[pi].mDeltas[1] = mTable[pi+1].mCoeffs[1] - mTable[pi].mCoeffs[1];
            mTable[pi].mDeltas[2] = mTable[pi+1].mCoeffs[2] - mTable[pi].mCoeffs[2];
            mTable[pi].mDeltas[3] = mTable[pi+1].mCoeffs[3] - mTable[pi].mCoeffs[3];
        }

        const size_t pi{CubicPhaseCount - 1};
        mTable[pi].mDeltas[0] = -mTable[pi].mCoeffs[0];
        mTable[pi].mDeltas[1] = -mTable[pi].mCoeffs[1];
        mTable[pi].mDeltas[2] = 1.0f - mTable[pi].mCoeffs[2];
        mTable[pi].mDeltas[3] = -mTable[pi].mCoeffs[3];
    }

    constexpr auto& getTable() const noexcept { return mTable; }
};

constexpr SplineFilterArray SplineFilter{};

} // namespace

const CubicTable gCubicSpline{SplineFilter.getTable()};
