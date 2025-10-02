#ifndef UTILS_SOFA_SUPPORT_H
#define UTILS_SOFA_SUPPORT_H

#include <memory>
#include <span>
#include <vector>

#include "mysofa.h"


struct MySofaDeleter {
    void operator()(MYSOFA_HRTF *sofa) const { mysofa_free(sofa); }
};
using MySofaHrtfPtr = std::unique_ptr<MYSOFA_HRTF,MySofaDeleter>;

// Per-field measurement info.
struct SofaField {
    using uint = unsigned int;

    double mDistance{0.0};
    uint mEvCount{0u};
    uint mEvStart{0u};
    std::vector<uint> mAzCounts;
};

const char *SofaErrorStr(int err);

auto GetCompatibleLayout(std::span<const float> xyzs) -> std::vector<SofaField>;

#endif /* UTILS_SOFA_SUPPORT_H */
