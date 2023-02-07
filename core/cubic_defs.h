#ifndef CORE_CUBIC_DEFS_H
#define CORE_CUBIC_DEFS_H

/* The number of distinct phase intervals within the cubic filter tables. */
constexpr unsigned int CubicPhaseBits{5};
constexpr unsigned int CubicPhaseCount{1 << CubicPhaseBits};

struct CubicCoefficients {
    float mCoeffs[4];
    float mDeltas[4];
};

#endif /* CORE_CUBIC_DEFS_H */
