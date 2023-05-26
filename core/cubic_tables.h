#ifndef CORE_CUBIC_TABLES_H
#define CORE_CUBIC_TABLES_H

#include "alspan.h"
#include "cubic_defs.h"


struct CubicTable {
    al::span<const CubicCoefficients,CubicPhaseCount> Tab;
};

/* A Catmull-Rom spline. The spline passes through the center two samples,
 * ensuring no discontinuity while moving through a series of samples.
 */
extern const CubicTable gCubicSpline;

#endif /* CORE_CUBIC_TABLES_H */
