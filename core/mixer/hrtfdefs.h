#ifndef CORE_MIXER_HRTFDEFS_H
#define CORE_MIXER_HRTFDEFS_H

#include <array>
#include <span>

#include "alnumeric.h"
#include "core/filters/splitter.h"


using float2 = std::array<f32, 2>;
using ubyte = u8;
using ubyte2 = std::array<ubyte,2>;
using ushort = u16;
using uint = u32;
using uint2 = std::array<u32, 2>;

constexpr auto HrtfHistoryBits = 6_u32;
constexpr auto HrtfHistoryLength = 1_u32 << HrtfHistoryBits;
constexpr auto HrtfHistoryMask = HrtfHistoryLength - 1_u32;

constexpr auto HrirBits = 7_u32;
constexpr auto HrirLength = 1_u32 << HrirBits;
constexpr auto HrirMask = HrirLength - 1_u32;

constexpr auto MinIrLength = 8_u32;

using HrirArray = std::array<float2, HrirLength>;
using HrirSpan = std::span<float2, HrirLength>;
using ConstHrirSpan = std::span<float2 const, HrirLength>;

struct MixHrtfFilter {
    ConstHrirSpan const Coeffs;
    uint2 Delay;
    f32 Gain;
    f32 GainStep;
};

struct HrtfFilter {
    alignas(16) HrirArray Coeffs;
    uint2 Delay;
    f32 Gain;
};


struct HrtfChannelState {
    BandSplitter mSplitter;
    f32 mHfScale{};
    alignas(16) HrirArray mCoeffs{};
};

#endif /* CORE_MIXER_HRTFDEFS_H */
