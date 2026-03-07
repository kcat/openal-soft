module;

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <ranges>

export module window.hann;

/* Define a Hann window, used to filter the STFT input and output. */
template<std::size_t N>
struct MakeHannWindow : std::array<float, N> {
    MakeHannWindow() noexcept
    {
        static constexpr auto scale = std::numbers::pi / double{N};
        /* Create lookup table of the Hann window for the desired size. */
        std::ranges::transform(std::views::iota(0u, unsigned{N/2}), this->begin(),
            [](unsigned const i) -> float
        {
            const auto val = std::sin((i+0.5) * scale);
            return static_cast<float>(val * val);
        });
        std::ranges::copy(*this | std::views::take(N/2), this->rbegin());
    }
};


export template<std::size_t N> alignas(16) inline
auto const gHannWindow = MakeHannWindow<N>{};
