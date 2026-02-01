#ifndef CORE_BSINC_TABLES_H
#define CORE_BSINC_TABLES_H

#include <array>
#include <span>

#include "altypes.hpp"
#include "bsinc_defs.h"
#include "opthelpers.h"

struct BSincTable {
    f32 scaleBase, scaleRange;
    std::array<u32, BSincScaleCount> m;
    std::array<u32, BSincScaleCount> filterOffset;
    std::span<float const> Tab;
};

DECL_HIDDEN extern constinit const BSincTable gBSinc12;
DECL_HIDDEN extern constinit const BSincTable gBSinc24;
DECL_HIDDEN extern constinit const BSincTable gBSinc48;

#endif /* CORE_BSINC_TABLES_H */
