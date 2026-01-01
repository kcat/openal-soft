#ifndef CORE_ALLPASS_IIR_HPP
#define CORE_ALLPASS_IIR_HPP

#include <array>
#include <ranges>
#include <span>

#include "alnumeric.h"
#include "altypes.hpp"


struct AllPassFilter {
    struct AllPassState {
        /* Last two delayed components for direct form II. */
        std::array<float, 2> z{};
    };
    std::array<AllPassState, 4> mState;
};

/* Filter coefficients for the 'base' all-pass IIR, which applies a frequency
 * dependent phase-shift of N degrees. The output of the filter requires a 1-
 * sample delay.
 */
constexpr inline auto Filter1Coeff = std::array{
    0.479400865589f, 0.876218493539f, 0.976597589508f, 0.997499255936f
};
/* Filter coefficients for the offset all-pass IIR, which applies a frequency
 * dependent phase-shift of N+90 degrees.
 */
constexpr inline auto Filter2Coeff = std::array{
    0.161758498368f, 0.733028932341f, 0.945349700329f, 0.990599156684f
};


constexpr auto processOne(AllPassFilter &self, std::span<float const, 4> const coeffs, float x)
    noexcept -> void
{
    auto state = self.mState;
    static_assert(state.size() == coeffs.size());
    for(const auto i : std::views::iota(0_uz, coeffs.size()))
    {
        const auto y = x*coeffs[i] + state[i].z[0];
        state[i].z[0] = state[i].z[1];
        state[i].z[1] = y*coeffs[i] - x;
        x = y;
    }
    self.mState = state;
}

constexpr auto process(AllPassFilter &self, std::span<float const, 4> const coeffs,
    std::span<float const> const src, bool const updateState, std::span<float> const dst) noexcept
    -> void
{
    auto state = self.mState;
    static_assert(state.size() == coeffs.size());
    std::ranges::transform(src | std::views::take(dst.size()), dst.begin(),
        [&state,coeffs](float x) noexcept -> float
    {
        for(const auto i : std::views::iota(0_uz, coeffs.size()))
        {
            const auto y = x*coeffs[i] + state[i].z[0];
            state[i].z[0] = state[i].z[1];
            state[i].z[1] = y*coeffs[i] - x;
            x = y;
        }
        return x;
    });
    if(updateState) [[likely]]
        self.mState = state;
}

#endif /* CORE_ALLPASS_IIR_HPP */
