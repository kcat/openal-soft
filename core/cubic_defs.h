#ifndef CORE_CUBIC_DEFS_H
#define CORE_CUBIC_DEFS_H

#include <array>

/* The number of distinct phase intervals within the cubic filter tables. */
inline auto constexpr CubicPhaseBits = 5u;
inline auto constexpr CubicPhaseCount = 1u << CubicPhaseBits;

struct CubicCoefficients {
    alignas(16) std::array<float,4> mCoeffs;
    alignas(16) std::array<float,4> mDeltas;
};

#endif /* CORE_CUBIC_DEFS_H */
