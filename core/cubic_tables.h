#ifndef CORE_CUBIC_TABLES_H
#define CORE_CUBIC_TABLES_H

#include "alspan.h"
#include "cubic_defs.h"


struct CubicTable {
    al::span<const CubicCoefficients,CubicPhaseCount> Tab;
};

/* A Gaussian-like resample filter. */
extern const CubicTable gGaussianFilter;

#endif /* CORE_CUBIC_TABLES_H */
