#ifndef AL_HANN_WINDOW_HPP
#define AL_HANN_WINDOW_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <ranges>


/* Define a Hann window, used to filter the STFT input and output. */
template<std::size_t N>
struct MakeHannWindow : std::array<float, N> {
    MakeHannWindow() noexcept
    {
        static constexpr auto scale = std::numbers::pi / double{N+1};
        /* Create lookup table of the Hann window for the desired size. */
        auto const end = std::ranges::transform(std::views::iota(0u, unsigned{N/2}), this->begin(),
            [](unsigned const i) -> float
        {
            const auto val = std::sin((i+1.0) * scale);
            return static_cast<float>(val * val);
        }).out;
        std::ranges::copy(this->begin(), end, this->rbegin());
    }
};


template<std::size_t N> alignas(16) inline
auto const gHannWindow = MakeHannWindow<N>{};

#endif /* AL_HANN_WINDOW_HPP */
