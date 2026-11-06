#ifndef CORE_BSINC_TABLES_HPP
#define CORE_BSINC_TABLES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>

#include "bsinc_defs.h"
#include "cemath.hpp"
#include "gsl/gsl"
#include "resampler_limits.hpp"
#include "zudl.hpp"


namespace ce {

/* The zero-order modified Bessel function of the first kind, used for the
 * Kaiser window.
 *
 *   I_0(x) = sum_{k=0}^inf (1 / k!)^2 (x / 2)^(2 k)
 *          = sum_{k=0}^inf ((x / 2)^k / k!)^2
 *
 * This implementation only handles nu = 0, and isn't the most precise (it
 * starts with the largest value and accumulates successively smaller values,
 * compounding the rounding and precision error), but it's good enough.
 */
template<typename T, std::floating_point U>
constexpr auto cyl_bessel_i(T nu, U x) -> U
{
    if(nu != T{0})
        throw std::runtime_error{"cyl_bessel_i: nu != 0"};

    /* Start at k=1 since k=0 is trivial. */
    const auto x2 = x/2;
    auto term = 1.0;
    auto sum = 1.0;
    auto k = 1;

    /* Let the integration converge until the term of the sum is no longer
     * significant.
     */
    double last_sum;
    do {
        const auto y = x2 / k;
        ++k;
        last_sum = sum;
        term *= y * y;
        sum += term;
    } while(sum != last_sum);
    return gsl::narrow_cast<U>(sum);
}

}


/* This is the normalized cardinal sine (sinc) function.
 *
 *   sinc(x) = { 1,                   x = 0
 *             { sin(pi x) / (pi x),  otherwise.
 */
constexpr auto Sinc(double const x) -> double
{
    if(!(ce::abs(x) > std::numeric_limits<double>::epsilon()))
        return 1.0;
    return ce::sin(std::numbers::pi*x) / (std::numbers::pi*x);
}

/* Calculate a Kaiser window from the given beta value and a normalized k
 * [-1, 1].
 *
 *   w(k) = { I_0(B sqrt(1 - k^2)) / I_0(B),  -1 <= k <= 1
 *          { 0,                              elsewhere.
 *
 * Where k can be calculated as:
 *
 *   k = i / l,         where -l <= i <= l.
 *
 * or:
 *
 *   k = 2 i / M - 1,   where 0 <= i <= M.
 */
constexpr auto Kaiser(double const beta, double const k, double const besseli_0_beta) -> double
{
    if(!(k >= -1.0 && k <= 1.0))
        return 0.0;
    return ce::cyl_bessel_i(0, beta * sqrt(1.0 - k*k)) / besseli_0_beta;
}

/* Calculates the (normalized frequency) transition width of the Kaiser window.
 * Rejection is in dB.
 */
constexpr auto CalcKaiserWidth(double const rejection, double const order) noexcept -> double
{
    if(rejection > 21.19)
        return (rejection-7.95) / (2.285 * std::numbers::pi*2.0 * order);
    /* This enforces a minimum rejection of just above 21.18dB */
    return 5.79 / (std::numbers::pi*2.0) / order;
}

/* Calculates the beta value of the Kaiser window. Rejection is in dB. */
constexpr auto CalcKaiserBeta(double const rejection) -> double
{
    if(rejection > 50.0)
        return 0.1102 * (rejection-8.7);
    if(rejection >= 21.0)
        return 0.5842*std::pow(rejection-21.0, 0.4) + 0.07886*(rejection-21.0);
    return 0.0;
}

struct BSincHeader {
    double beta{};
    double scaleBase{};
    double scaleLimit{};

    std::array<double, BSincScaleCount> a{};
    std::array<std::uint32_t, BSincScaleCount> m{};
    std::size_t total_size{};

    consteval
    BSincHeader(double const rejection, double const order, double const maxScale) noexcept
        : beta{CalcKaiserBeta(rejection)}, scaleBase{CalcKaiserWidth(rejection, order) / 2.0}
        , scaleLimit{1.0 / maxScale}
    {
        const auto base_a = (order+1.0) / 2.0;
        for(const auto si : std::views::iota(0_uz, BSincScaleCount))
        {
            const auto scale = std::lerp(scaleBase, 1.0,
                gsl::narrow_cast<double>(si+1u)/BSincScaleCount);
            a[si] = std::min(base_a/scale, base_a*maxScale);
            /* std::ceil() isn't constexpr until C++23, this should behave the
             * same.
             */
            auto a_ = gsl::narrow_cast<std::uint32_t>(a[si]);
            a_ += (gsl::narrow_cast<double>(a_) != a[si]) ? 1u : 0u;
            m[si] = a_ * 2u;

            total_size += 4_uz * BSincPhaseCount * ((m[si]+3u) & ~3u);
        }
    }
};

/* 11th and 23rd order filters (12 and 24-point respectively) with a 60dB drop
 * at nyquist. Each filter will scale up to double size when downsampling, to
 * 23rd and 47th order respectively.
 */
inline constexpr auto bsinc12_hdr = BSincHeader{60, 11, 2};
inline constexpr auto bsinc24_hdr = BSincHeader{60, 23, 2};
/* 47th order filter (48-point) with an 80dB drop at nyquist. The filter order
 * doesn't increase when downsampling.
 */
inline constexpr auto bsinc48_hdr = BSincHeader{80, 47, 1};


template<const BSincHeader &hdr>
struct BSincFilterArray {
    static constexpr auto BSincPointsMax = (hdr.m[0]+3_uz) & ~3_uz;
    static constexpr auto besseli_0_beta = ce::cyl_bessel_i(0, hdr.beta);
    static_assert(BSincPointsMax <= MaxResamplerPadding, "MaxResamplerPadding is too small");

    alignas(16) std::array<float, hdr.total_size> mTable{};

    /* This could be made constexpr/consteval with constexpr-capable sin and
     * sqrt functions, which we can suitably make. However, the size of the
     * filter tables not only requires significantly increasing the constexpr
     * step limit, compilation takes *minutes*, at least with Clang (GCC just
     * gives up on it without a reason after about a minute of compiling, only
     * saying it's not constexpr). Pretty absurd since it's instantaneous at
     * runtime. Would be better to just compile the darn functions with
     * optimizations and then execute the compiled code, but we can't easily do
     * that as a pre-compile step with cross-compiling.
     *
     * Avoiding our strict number types and using less accurate math functions
     * can help a bit, but even ignoring the potential quality issues, the
     * speed improvement isn't enough to make it viable. I don't know how else
     * to make this more efficient to make the resamplers constexpr.
     */
    BSincFilterArray() noexcept
    {
        using filter_type = std::array<std::array<double, BSincPointsMax>, BSincPhaseCount>;
        auto filter = std::vector<filter_type>(BSincScaleCount);

        /* Calculate the Kaiser-windowed Sinc filter coefficients for each
         * scale and phase index.
         */
        for(const auto si : std::views::iota(0_uz, BSincScaleCount))
        {
            const auto a = hdr.a[si];
            const auto m = hdr.m[si];
            const auto l = (m>>1) - 1.0;
            const auto o = (std::size_t{BSincPointsMax}-m) / 2u;
            const auto scale = std::lerp(hdr.scaleBase, 1.0,
                gsl::narrow_cast<double>(si+1u)/BSincScaleCount);

            /* Calculate an appropriate cutoff frequency. An explanation may be
             * in order here.
             *
             * When up-sampling, or down-sampling by less than the max scaling
             * factor (when scale >= scaleLimit), the filter order increases as
             * the down-sampling factor is reduced, enabling a consistent
             * filter response output.
             *
             * When down-sampling by more than the max scale factor, the filter
             * order stays constant to avoid further increasing the processing
             * cost, causing the transition width to increase. This would
             * normally be compensated for by reducing the cutoff frequency, to
             * keep the transition band under the nyquist frequency and avoid
             * aliasing. However, this has the side-effect of attenuating more
             * of the original high frequency content, which can be significant
             * with more extreme down-sampling scales.
             *
             * To combat this, we can allow for some aliasing to keep the
             * cutoff frequency higher than it would otherwise be. We can allow
             * the transition band to "wrap around" the nyquist frequency, so
             * the output would have some low-level aliasing that overlays with
             * the attenuated frequencies in the transition band. This allows
             * the cutoff frequency to remain fixed as the transition width
             * increases, until the stop frequency aliases back to the cutoff
             * frequency and the transition band becomes fully wrapped over
             * itself, at which point the cutoff frequency will lower at half
             * the rate the transition width increases.
             *
             * This has an additional benefit when dealing with typical output
             * rates like 44 or 48khz. Since human hearing maxes out at 20khz,
             * and these rates handle frequencies up to 22 or 24khz, this lets
             * some aliasing get masked. For example, the bsinc24 filter with
             * 48khz output has a cutoff of 20khz when down-sampling, and a
             * 4khz transition band. When down-sampling by more extreme scales,
             * the cutoff frequency can stay at 20khz while the transition
             * width doubles before any aliasing noise may become audible.
             *
             * This is what we do here.
             *
             * 'max_cutoff` is the upper bound normalized cutoff frequency for
             * this scale factor, that aligns with the same absolute frequency
             * as nominal resample factors. When up-sampling (scale == 1), the
             * cutoff can't be raised further than this, or else it would
             * prematurely add audible aliasing noise.
             *
             * 'width' is the normalized transition width for this scale
             * factor.
             *
             * '(scale - width)*0.5' calculates the cutoff frequency necessary
             * for the transition band to fully wrap on itself around the
             * nyquist frequency. If this is larger than max_cutoff, the
             * transition band is not fully wrapped at this scale and the
             * cutoff doesn't need adjustment.
             */
            const auto max_cutoff = (0.5 - hdr.scaleBase)*scale;
            const auto width = hdr.scaleBase * std::max(hdr.scaleLimit, scale);
            const auto cutoff2 = std::min(max_cutoff, (scale - width)*0.5) * 2.0;

            for(const auto pi : std::views::iota(0u, BSincPhaseCount))
            {
                const auto phase = l + gsl::narrow_cast<double>(pi)/BSincPhaseCount;

                std::ranges::transform(std::views::iota(0u, m),
                    std::span{filter[si][pi]}.subspan(o).begin(),
                    [phase, cutoff2, a](std::uint32_t const i) noexcept -> double
                {
                    auto const x = i - phase;
                    return Kaiser(hdr.beta, x/a, besseli_0_beta) * cutoff2 * Sinc(cutoff2*x);
                });
            }
        }

        auto idx = 0_uz;
        for(const auto si : std::views::iota(0_uz, BSincScaleCount))
        {
            const auto m = (hdr.m[si]+3_uz) & ~3_uz;
            const auto o = std::size_t{BSincPointsMax-m} / 2u;

            /* Write out each phase index's filter and phase delta for this
             * quality scale.
             */
            for(const auto pi : std::views::iota(0_uz, BSincPhaseCount))
            {
                for(const auto i : std::views::iota(0_uz, m))
                    mTable[idx++] = gsl::narrow_cast<float>(filter[si][pi][o+i]);

                /* Linear interpolation between phases is simplified by pre-
                 * calculating the delta (b - a) in: x = a + f (b - a)
                 */
                if(pi < BSincPhaseCount-1)
                {
                    for(const auto i : std::views::iota(0_uz, m))
                    {
                        const auto phDelta = filter[si][pi+1][o+i] - filter[si][pi][o+i];
                        mTable[idx++] = gsl::narrow_cast<float>(phDelta);
                    }
                }
                else
                {
                    /* The delta target for the last phase index is the first
                     * phase index with the coefficients offset by one. The
                     * first delta targets 0, as it represents a coefficient
                     * for a sample that won't be part of the filter.
                     */
                    mTable[idx++] = gsl::narrow_cast<float>(0.0 - filter[si][pi][o]);
                    for(const auto i : std::views::iota(1_uz, m))
                    {
                        const auto phDelta = filter[si][0][o+i-1] - filter[si][pi][o+i];
                        mTable[idx++] = gsl::narrow_cast<float>(phDelta);
                    }
                }
            }

            /* Now write out each phase index's scale and phase+scale deltas,
             * to complete the bilinear equation for the combination of phase
             * and scale.
             */
            if(si < BSincScaleCount-1)
            {
                for(const auto pi : std::views::iota(0_uz, BSincPhaseCount))
                {
                    for(const auto i : std::views::iota(0_uz, m))
                    {
                        const auto scDelta = filter[si+1][pi][o+i] - filter[si][pi][o+i];
                        mTable[idx++] = gsl::narrow_cast<float>(scDelta);
                    }

                    if(pi < BSincPhaseCount-1)
                    {
                        for(const auto i : std::views::iota(0_uz, m))
                        {
                            const auto spDelta = (filter[si+1][pi+1][o+i]-filter[si+1][pi][o+i]) -
                                (filter[si][pi+1][o+i]-filter[si][pi][o+i]);
                            mTable[idx++] = gsl::narrow_cast<float>(spDelta);
                        }
                    }
                    else
                    {
                        mTable[idx++] = gsl::narrow_cast<float>((0.0 - filter[si+1][pi][o])
                            - (0.0 - filter[si][pi][o]));
                        for(const auto i : std::views::iota(1_uz, m))
                        {
                            const auto spDelta = (filter[si+1][0][o+i-1] - filter[si+1][pi][o+i]) -
                                (filter[si][0][o+i-1] - filter[si][pi][o+i]);
                            mTable[idx++] = gsl::narrow_cast<float>(spDelta);
                        }
                    }
                }
            }
            else
            {
                /* The last scale index doesn't have scale-related deltas. */
                auto const count = BSincPhaseCount * m * 2;
                std::ranges::fill(std::span{mTable}.subspan(idx, count), 0.0f);
                idx += count;
            }
        }
        Ensures(idx == hdr.total_size);
    }

    [[nodiscard]] static constexpr auto getHeader() noexcept -> BSincHeader const& { return hdr; }
    [[nodiscard]] constexpr auto getTable() const noexcept { return std::span{mTable}; }
};

inline auto const bsinc12_filter = BSincFilterArray<bsinc12_hdr>{};
inline auto const bsinc24_filter = BSincFilterArray<bsinc24_hdr>{};
inline auto const bsinc48_filter = BSincFilterArray<bsinc48_hdr>{};


struct BSincTable {
    float scaleBase, scaleRange;
    std::array<std::uint32_t, BSincScaleCount> m;
    std::array<std::uint32_t, BSincScaleCount> filterOffset;
    std::span<float const> Tab;
};

template<typename T>
constexpr auto GenerateBSincTable(T const& filter) noexcept -> BSincTable
{
    auto ret = BSincTable{};
    const BSincHeader &hdr = filter.getHeader();
    ret.scaleBase = gsl::narrow_cast<float>(hdr.scaleBase);
    ret.scaleRange = gsl::narrow_cast<float>(1.0 / (1.0 - hdr.scaleBase));
    for(const auto i : std::views::iota(0_uz, BSincScaleCount))
        ret.m[i] = (hdr.m[i]+3u) & ~3u;
    ret.filterOffset[0] = 0;
    for(const auto i : std::views::iota(1_uz, BSincScaleCount))
        ret.filterOffset[i] = ret.filterOffset[i-1] + ret.m[i-1]*4u*BSincPhaseCount;
    ret.Tab = filter.getTable();
    return ret;
}

inline constexpr auto gBSinc12 = BSincTable{GenerateBSincTable(bsinc12_filter)};
inline constexpr auto gBSinc24 = BSincTable{GenerateBSincTable(bsinc24_filter)};
inline constexpr auto gBSinc48 = BSincTable{GenerateBSincTable(bsinc48_filter)};

#endif /* CORE_BSINC_TABLES_HPP */
