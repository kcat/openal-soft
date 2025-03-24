
#include "bsinc_tables.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "alnumbers.h"
#include "alnumeric.h"
#include "alspan.h"
#include "bsinc_defs.h"
#include "opthelpers.h"
#include "resampler_limits.h"


namespace {

using uint = unsigned int;


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
template<typename T, typename U>
constexpr auto cyl_bessel_i(T nu, U x) -> U
{
    if(nu != T{0})
        throw std::runtime_error{"cyl_bessel_i: nu != 0"};

    /* Start at k=1 since k=0 is trivial. */
    const double x2{x/2.0};
    double term{1.0};
    double sum{1.0};
    int k{1};

    /* Let the integration converge until the term of the sum is no longer
     * significant.
     */
    double last_sum{};
    do {
        const double y{x2 / k};
        ++k;
        last_sum = sum;
        term *= y * y;
        sum += term;
    } while(sum != last_sum);
    return static_cast<U>(sum);
}

/* This is the normalized cardinal sine (sinc) function.
 *
 *   sinc(x) = { 1,                   x = 0
 *             { sin(pi x) / (pi x),  otherwise.
 */
constexpr double Sinc(const double x)
{
    constexpr double epsilon{std::numeric_limits<double>::epsilon()};
    if(!(x > epsilon || x < -epsilon))
        return 1.0;
    return std::sin(al::numbers::pi*x) / (al::numbers::pi*x);
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
constexpr double Kaiser(const double beta, const double k, const double besseli_0_beta)
{
    if(!(k >= -1.0 && k <= 1.0))
        return 0.0;
    return ::cyl_bessel_i(0, beta * std::sqrt(1.0 - k*k)) / besseli_0_beta;
}

/* Calculates the (normalized frequency) transition width of the Kaiser window.
 * Rejection is in dB.
 */
constexpr double CalcKaiserWidth(const double rejection, const uint order) noexcept
{
    if(rejection > 21.19)
        return (rejection - 7.95) / (2.285 * al::numbers::pi*2.0 * order);
    /* This enforces a minimum rejection of just above 21.18dB */
    return 5.79 / (al::numbers::pi*2.0 * order);
}

/* Calculates the beta value of the Kaiser window. Rejection is in dB. */
constexpr double CalcKaiserBeta(const double rejection)
{
    if(rejection > 50.0)
        return 0.1102 * (rejection-8.7);
    if(rejection >= 21.0)
        return (0.5842 * std::pow(rejection-21.0, 0.4)) + (0.07886 * (rejection-21.0));
    return 0.0;
}


struct BSincHeader {
    double beta{};
    double scaleBase{};
    double scaleLimit{};

    std::array<double,BSincScaleCount> a{};
    std::array<uint,BSincScaleCount> m{};
    uint total_size{};

    constexpr BSincHeader(uint rejection, uint order, uint maxScale) noexcept
        : beta{CalcKaiserBeta(rejection)}, scaleBase{CalcKaiserWidth(rejection, order) / 2.0}
        , scaleLimit{1.0 / maxScale}
    {
        const auto base_a = (order+1.0) / 2.0;
        for(uint si{0};si < BSincScaleCount;++si)
        {
            const auto scale = lerpd(scaleBase, 1.0, (si+1u) / double{BSincScaleCount});
            a[si] = std::min(base_a/scale, base_a*maxScale);
            /* std::ceil() isn't constexpr until C++23, this should behave the
             * same.
             */
            auto a_ = static_cast<uint>(a[si]);
            a_ += (static_cast<double>(a_) != a[si]);
            m[si] = a_ * 2u;

            total_size += 4u * BSincPhaseCount * ((m[si]+3u) & ~3u);
        }
    }
};

/* 11th and 23rd order filters (12 and 24-point respectively) with a 60dB drop
 * at nyquist. Each filter will scale up to double size when downsampling, to
 * 23rd and 47th order respectively.
 */
constexpr auto bsinc12_hdr = BSincHeader{60, 11, 2};
constexpr auto bsinc24_hdr = BSincHeader{60, 23, 2};
/* 47th order filter (48-point) with an 80dB drop at nyquist. The filter order
 * doesn't increase when downsampling.
 */
constexpr auto bsinc48_hdr = BSincHeader{80, 47, 1};


template<const BSincHeader &hdr>
struct SIMDALIGN BSincFilterArray {
    alignas(16) std::array<float, hdr.total_size> mTable{};

    BSincFilterArray()
    {
        static constexpr auto BSincPointsMax = (hdr.m[0]+3u) & ~3u;
        static_assert(BSincPointsMax <= MaxResamplerPadding, "MaxResamplerPadding is too small");

        using filter_type = std::array<std::array<double,BSincPointsMax>,BSincPhaseCount>;
        auto filter = std::vector<filter_type>(BSincScaleCount);

        static constexpr auto besseli_0_beta = ::cyl_bessel_i(0, hdr.beta);

        /* Calculate the Kaiser-windowed Sinc filter coefficients for each
         * scale and phase index.
         */
        for(uint si{0};si < BSincScaleCount;++si)
        {
            const auto a = hdr.a[si];
            const auto m = hdr.m[si];
            const auto l = std::floor(m*0.5) - 1.0;
            const auto o = size_t{BSincPointsMax-m} / 2u;
            const auto scale = lerpd(hdr.scaleBase, 1.0, (si+1u) / double{BSincScaleCount});

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
             * normally be compensated for by reducing the cutoff frequency,
             * to keep the transition band under the nyquist frequency and
             * avoid aliasing. However, this has the side-effect of attenuating
             * more of the original high frequency content, which can be
             * significant with more extreme down-sampling scales.
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

            for(uint pi{0};pi < BSincPhaseCount;++pi)
            {
                const auto phase = l + (pi/double{BSincPhaseCount});

                for(uint i{0};i < m;++i)
                {
                    const auto x = static_cast<double>(i) - phase;
                    filter[si][pi][o+i] = Kaiser(hdr.beta, x/a, besseli_0_beta) * cutoff2 *
                        Sinc(cutoff2*x);
                }
            }
        }

        size_t idx{0};
        for(size_t si{0};si < BSincScaleCount;++si)
        {
            const auto m = (hdr.m[si]+3_uz) & ~3_uz;
            const auto o = size_t{BSincPointsMax-m} / 2u;

            /* Write out each phase index's filter and phase delta for this
             * quality scale.
             */
            for(size_t pi{0};pi < BSincPhaseCount;++pi)
            {
                for(size_t i{0};i < m;++i)
                    mTable[idx++] = static_cast<float>(filter[si][pi][o+i]);

                /* Linear interpolation between phases is simplified by pre-
                 * calculating the delta (b - a) in: x = a + f (b - a)
                 */
                if(pi < BSincPhaseCount-1)
                {
                    for(size_t i{0};i < m;++i)
                    {
                        const double phDelta{filter[si][pi+1][o+i] - filter[si][pi][o+i]};
                        mTable[idx++] = static_cast<float>(phDelta);
                    }
                }
                else
                {
                    /* The delta target for the last phase index is the first
                     * phase index with the coefficients offset by one. The
                     * first delta targets 0, as it represents a coefficient
                     * for a sample that won't be part of the filter.
                     */
                    mTable[idx++] = static_cast<float>(0.0 - filter[si][pi][o]);
                    for(size_t i{1};i < m;++i)
                    {
                        const double phDelta{filter[si][0][o+i-1] - filter[si][pi][o+i]};
                        mTable[idx++] = static_cast<float>(phDelta);
                    }
                }
            }

            /* Now write out each phase index's scale and phase+scale deltas,
             * to complete the bilinear equation for the combination of phase
             * and scale.
             */
            if(si < BSincScaleCount-1)
            {
                for(size_t pi{0};pi < BSincPhaseCount;++pi)
                {
                    for(size_t i{0};i < m;++i)
                    {
                        const double scDelta{filter[si+1][pi][o+i] - filter[si][pi][o+i]};
                        mTable[idx++] = static_cast<float>(scDelta);
                    }

                    if(pi < BSincPhaseCount-1)
                    {
                        for(size_t i{0};i < m;++i)
                        {
                            const double spDelta{(filter[si+1][pi+1][o+i]-filter[si+1][pi][o+i]) -
                                (filter[si][pi+1][o+i]-filter[si][pi][o+i])};
                            mTable[idx++] = static_cast<float>(spDelta);
                        }
                    }
                    else
                    {
                        mTable[idx++] = static_cast<float>((0.0 - filter[si+1][pi][o]) -
                            (0.0 - filter[si][pi][o]));
                        for(size_t i{1};i < m;++i)
                        {
                            const double spDelta{(filter[si+1][0][o+i-1] - filter[si+1][pi][o+i]) -
                                (filter[si][0][o+i-1] - filter[si][pi][o+i])};
                            mTable[idx++] = static_cast<float>(spDelta);
                        }
                    }
                }
            }
            else
            {
                /* The last scale index doesn't have scale-related deltas. */
                for(size_t i{0};i < BSincPhaseCount*m*2;++i)
                    mTable[idx++] = 0.0f;
            }
        }
        assert(idx == hdr.total_size);
    }

    [[nodiscard]] constexpr auto getHeader() const noexcept -> const BSincHeader& { return hdr; }
    [[nodiscard]] constexpr auto getTable() const noexcept { return al::span{mTable}; }
};

const auto bsinc12_filter = BSincFilterArray<bsinc12_hdr>{};
const auto bsinc24_filter = BSincFilterArray<bsinc24_hdr>{};
const auto bsinc48_filter = BSincFilterArray<bsinc48_hdr>{};

template<typename T>
constexpr BSincTable GenerateBSincTable(const T &filter)
{
    BSincTable ret{};
    const BSincHeader &hdr = filter.getHeader();
    ret.scaleBase = static_cast<float>(hdr.scaleBase);
    ret.scaleRange = static_cast<float>(1.0 / (1.0 - hdr.scaleBase));
    for(size_t i{0};i < BSincScaleCount;++i)
        ret.m[i] = (hdr.m[i]+3u) & ~3u;
    ret.filterOffset[0] = 0;
    for(size_t i{1};i < BSincScaleCount;++i)
        ret.filterOffset[i] = ret.filterOffset[i-1] + ret.m[i-1]*4*BSincPhaseCount;
    ret.Tab = filter.getTable();
    return ret;
}

} // namespace

const BSincTable gBSinc12{GenerateBSincTable(bsinc12_filter)};
const BSincTable gBSinc24{GenerateBSincTable(bsinc24_filter)};
const BSincTable gBSinc48{GenerateBSincTable(bsinc48_filter)};
