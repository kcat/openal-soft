#ifndef CORE_MIXER_DEFS_H
#define CORE_MIXER_DEFS_H

#include <array>
#include <ranges>
#include <span>
#include <variant>

#include "alnumeric.h"
#include "core/bufferline.h"
#include "core/cubic_defs.h"

struct HrtfChannelState;
struct HrtfFilter;
struct MixHrtfFilter;

using f32x2 = std::array<f32, 2>;


inline constexpr auto MixerFracBits = 16_i32;
inline constexpr auto MixerFracOne = 1_i32 << MixerFracBits;
inline constexpr auto MixerFracMask = MixerFracOne - 1_i32;
inline constexpr auto MixerFracHalf = MixerFracOne >> 1_i32;

inline constexpr auto GainSilenceThreshold = 0.00001_f32; /* -100dB */


enum class Resampler : u8 {
    Point,
    Linear,
    Spline,
    Gaussian,
    FastBSinc12,
    BSinc12,
    FastBSinc24,
    BSinc24,
    FastBSinc48,
    BSinc48,

    Max = BSinc48
};

/* Interpolator state. Kind of a misnomer since the interpolator itself is
 * stateless. This just keeps it from having to recompute scale-related
 * mappings for every sample.
 */
struct BsincState {
    f32 sf; /* Scale interpolation factor. */
    u32 m; /* Coefficient count. */
    u32 l; /* Left coefficient offset. */
    /* Filter coefficients, followed by the phase, scale, and scale-phase
     * delta coefficients. Starting at phase index 0, each subsequent phase
     * index follows contiguously.
     */
    std::span<f32 const> filter;
};

struct CubicState {
    /* Filter coefficients, and coefficient deltas. Starting at phase index 0,
     * each subsequent phase index follows contiguously.
     */
    std::span<CubicCoefficients const, CubicPhaseCount> filter;
    explicit CubicState(std::span<CubicCoefficients const,CubicPhaseCount> const f) : filter{f} { }
};

using InterpState = std::variant<std::monostate, CubicState, BsincState>;

using ResamplerFunc = void(*)(InterpState const *state, std::span<f32 const> src, u32 frac,
    u32 increment, std::span<f32> dst);

[[nodiscard]]
auto PrepareResampler(Resampler resampler, u32 increment, InterpState *state) -> ResamplerFunc;


template<typename TypeTag, typename InstTag>
void Resample_(InterpState const *state, std::span<f32 const> src, u32 frac, u32 increment,
    std::span<f32> dst);

template<typename InstTag>
void Mix_(std::span<f32 const> InSamples, std::span<FloatBufferLine> OutBuffer,
    std::span<f32> CurrentGains, std::span<f32 const> TargetGains, usize Counter, usize OutPos);
template<typename InstTag>
void Mix_(std::span<f32 const> InSamples, std::span<f32> OutBuffer, f32 &CurrentGain,
    f32 TargetGain, usize Counter);

template<typename InstTag>
void MixHrtf_(std::span<f32 const> InSamples, std::span<f32x2> AccumSamples, u32 IrSize,
    MixHrtfFilter const *hrtfparams, usize SamplesToDo);
template<typename InstTag>
void MixHrtfBlend_(std::span<f32 const> InSamples, std::span<f32x2> AccumSamples, u32 IrSize,
    HrtfFilter const *oldparams, MixHrtfFilter const *newparams, usize SamplesToDo);
template<typename InstTag>
void MixDirectHrtf_(FloatBufferSpan LeftOut, FloatBufferSpan RightOut,
    std::span<FloatBufferLine const> InSamples, std::span<f32x2> AccumSamples,
    std::span<f32, BufferLineSize> TempBuf, std::span<HrtfChannelState> ChanState, usize IrSize,
    usize SamplesToDo);

/* Vectorized resampler helpers */
template<usize N>
constexpr void InitPosArrays(u32 const pos, u32 const frac, u32 const increment,
    std::span<u32, N> const frac_arr, std::span<u32, N> const pos_arr)
{
    static_assert(pos_arr.size() == frac_arr.size());
    pos_arr[0] = pos;
    frac_arr[0] = frac;
    for(auto const i : std::views::iota(1_uz, pos_arr.size()))
    {
        auto const frac_tmp = frac_arr[i-1] + increment;
        pos_arr[i] = pos_arr[i-1] + (frac_tmp>>MixerFracBits);
        frac_arr[i] = frac_tmp&MixerFracMask;
    }
}

#endif /* CORE_MIXER_DEFS_H */
