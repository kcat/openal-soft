#ifndef CORE_MIXER_HRTFDEFS_H
#define CORE_MIXER_HRTFDEFS_H

#include <array>
#include <cstdint>
#include <span>

#include "core/filters/splitter.h"


using u8x2 = std::array<std::uint8_t, 2>;
using u32x2 = std::array<std::uint32_t, 2>;
using f32x2 = std::array<float, 2>;

inline auto constexpr HrtfHistoryBits = 6u;
inline auto constexpr HrtfHistoryLength = 1u << HrtfHistoryBits;
inline auto constexpr HrtfHistoryMask = HrtfHistoryLength - 1u;

inline auto constexpr HrirBits = 7u;
inline auto constexpr HrirLength = 1u << HrirBits;
inline auto constexpr HrirMask = HrirLength - 1u;

inline auto constexpr MinIrLength = 8u;

using HrirArray = std::array<f32x2, HrirLength>;
using HrirSpan = std::span<f32x2, HrirLength>;
using ConstHrirSpan = std::span<f32x2 const, HrirLength>;

struct MixHrtfFilter {
    ConstHrirSpan const Coeffs;
    u32x2 Delay;
    float Gain;
    float GainStep;
};

struct HrtfFilter {
    alignas(16) HrirArray Coeffs;
    u32x2 Delay;
    float Gain;
};


struct HrtfChannelState {
    BandSplitter mSplitter;
    float mHfScale{};
    alignas(16) HrirArray mCoeffs{};
};

#endif /* CORE_MIXER_HRTFDEFS_H */
