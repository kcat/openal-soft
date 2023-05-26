#ifndef CORE_BSINC_TABLES_H
#define CORE_BSINC_TABLES_H

#include "bsinc_defs.h"


struct BSincTable {
    float scaleBase, scaleRange;
    unsigned int m[BSincScaleCount];
    unsigned int filterOffset[BSincScaleCount];
    const float *Tab;
};

extern const BSincTable gBSinc12;
extern const BSincTable gBSinc24;

#endif /* CORE_BSINC_TABLES_H */
