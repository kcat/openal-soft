#ifndef CORE_CPU_CAPS_H
#define CORE_CPU_CAPS_H

#include <optional>
#include <string>

#include "bitset.hpp"


enum class CPUCap {
    SSE, SSE2, SSE3, SSE4_1, NEON,
    Count
};

using CPUCapBitset = al::bitset<CPUCap>;
inline CPUCapBitset CPUCapFlags;

struct CPUInfo {
    std::string mVendor;
    std::string mName;
    CPUCapBitset mCaps;
};

std::optional<CPUInfo> GetCPUInfo();

#endif /* CORE_CPU_CAPS_H */
