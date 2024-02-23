#ifndef CORE_CUBIC_TABLES_H
#define CORE_CUBIC_TABLES_H

#include "alspan.h"
#include "cubic_defs.h"


struct CubicTable {
    std::array<CubicCoefficients,CubicPhaseCount> mTable{};
};

struct GaussianTable : CubicTable {
    GaussianTable();
};
inline const GaussianTable gGaussianFilter;


struct CubicFilter {
    static constexpr size_t sTableBits{8};
    static constexpr size_t sTableSteps{1 << sTableBits};
    static constexpr size_t sTableMask{sTableSteps - 1};

    std::array<float,sTableSteps*2 + 1> mFilter{};

    CubicFilter();

    [[nodiscard]] constexpr
    auto getCoeff0(size_t i) const noexcept -> float { return mFilter[sTableSteps+i]; }
    [[nodiscard]] constexpr
    auto getCoeff1(size_t i) const noexcept -> float { return mFilter[i]; }
    [[nodiscard]] constexpr
    auto getCoeff2(size_t i) const noexcept -> float { return mFilter[sTableSteps-i]; }
    [[nodiscard]] constexpr
    auto getCoeff3(size_t i) const noexcept -> float { return mFilter[sTableSteps*2-i]; }
};
inline const CubicFilter gCubicTable;

#endif /* CORE_CUBIC_TABLES_H */
