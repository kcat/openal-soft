
#include "polyphase_resampler.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <tuple>

#include "gsl/gsl"


using uint = unsigned int;

namespace {

constexpr auto Epsilon = 1e-9;


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
    const auto x2 = x/2.0;
    auto term = 1.0;
    auto sum = 1.0;
    auto k = 1;

    /* Let the integration converge until the term of the sum is no longer
     * significant.
     */
    auto last_sum = 0.0;
    do {
        const auto y = x2 / k;
        ++k;
        last_sum = sum;
        term *= y * y;
        sum += term;
    } while(sum != last_sum);
    return gsl::narrow_cast<U>(sum);
}

/* This is the normalized cardinal sine (sinc) function.
 *
 *   sinc(x) = { 1,                   x = 0
 *             { sin(pi x) / (pi x),  otherwise.
 */
auto Sinc(const double x) -> double
{
    if(std::abs(x) < Epsilon) [[unlikely]]
        return 1.0;
    return std::sin(std::numbers::pi*x) / (std::numbers::pi*x);
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
auto Kaiser(const double beta, const double k, const double besseli_0_beta) -> double
{
    if(!(k >= -1.0 && k <= 1.0))
        return 0.0;
    return ::cyl_bessel_i(0, beta * std::sqrt(1.0 - k*k)) / besseli_0_beta;
}

/* Calculates the size (order) of the Kaiser window.  Rejection is in dB and
 * the transition width is normalized frequency (0.5 is nyquist).
 *
 *   M = { ceil((r - 7.95) / (2.285 2 pi f_t)),  r > 21
 *       { ceil(5.79 / 2 pi f_t),                r <= 21.
 *
 */
constexpr auto CalcKaiserOrder(const double rejection, const double transition) -> uint
{
    const auto w_t = 2.0 * std::numbers::pi * transition;
    if(rejection > 21.0) [[likely]]
        return gsl::narrow_cast<uint>(std::ceil((rejection - 7.95) / (2.285 * w_t)));
    return gsl::narrow_cast<uint>(std::ceil(5.79 / w_t));
}

// Calculates the beta value of the Kaiser window.  Rejection is in dB.
constexpr auto CalcKaiserBeta(const double rejection) -> double
{
    if(rejection > 50.0) [[likely]]
        return 0.1102 * (rejection - 8.7);
    if(rejection >= 21.0)
        return (0.5842 * std::pow(rejection - 21.0, 0.4)) +
            (0.07886 * (rejection - 21.0));
    return 0.0;
}

/* Calculates a point on the Kaiser-windowed sinc filter for the given half-
 * width, beta, gain, and cutoff.  The point is specified in non-normalized
 * samples, from 0 to M, where M = (2 l + 1).
 *
 *   w(k) 2 p f_t sinc(2 f_t x)
 *
 *   x    -- centered sample index (i - l)
 *   k    -- normalized and centered window index (x / l)
 *   w(k) -- window function (Kaiser)
 *   p    -- gain compensation factor when sampling
 *   f_t  -- normalized center frequency (or cutoff; 0.5 is nyquist)
 */
auto SincFilter(const uint l, const double beta, const double besseli_0_beta, const double gain,
    const double cutoff, const uint i) -> double
{
    const auto x = gsl::narrow_cast<double>(i) - l;
    return Kaiser(beta, x/l, besseli_0_beta) * 2.0 * gain * cutoff * Sinc(2.0 * cutoff * x);
}

} // namespace

// Calculate the resampling metrics and build the Kaiser-windowed sinc filter
// that's used to cut frequencies above the destination nyquist.
void PPhaseResampler::init(const uint srcRate, const uint dstRate)
{
    const auto gcd = std::gcd(srcRate, dstRate);
    mP = dstRate / gcd;
    mQ = srcRate / gcd;

    /* The cutoff is adjusted by the transition width, so the transition ends
     * at nyquist (0.5). Both are scaled by the downsampling factor.
     */
    const auto [cutoff, width] = (mP > mQ) ? std::make_tuple(0.47 / mP, 0.03 / mP)
        : std::make_tuple(0.47 / mQ, 0.03 / mQ);

    // A rejection of -180 dB is used for the stop band. Round up when
    // calculating the left offset to avoid increasing the transition width.
    static constexpr auto rejection = 180.0;
    const auto l = (CalcKaiserOrder(rejection, width)+1u) / 2u;
    static constexpr auto beta = CalcKaiserBeta(rejection);
    static constexpr auto besseli_0_beta = ::cyl_bessel_i(0, beta);
    mM = l*2u + 1u;
    mL = l;
    mF.resize(mM);
    std::ranges::transform(std::views::iota(0u, mM), mF.begin(), [this,cutoff](const uint i)
    { return SincFilter(mL, beta, besseli_0_beta, mP, cutoff, i); });
}

// Perform the upsample-filter-downsample resampling operation using a
// polyphase filter implementation.
void PPhaseResampler::process(const std::span<const double> in, const std::span<double> out) const
{
    if(out.empty()) [[unlikely]]
        return;

    // Handle in-place operation.
    auto workspace = std::vector<double>{};
    auto work = std::span{out};
    if(work.data() == in.data()) [[unlikely]]
    {
        workspace.resize(out.size());
        work = workspace;
    }

    const auto f = std::span{mF};
    const auto p = size_t{mP};
    const auto q = size_t{mQ};
    const auto m = size_t{mM};
    /* Input starts at l to compensate for the filter delay. This will drop any
     * build-up from the first half of the filter.
     */
    auto l = size_t{mL};
    std::ranges::generate(work, [in,f,p,q,m,&l]
    {
        auto j_s = l / p;
        auto j_f = l % p;
        l += q;

        // Only take input when 0 <= j_s < in.size().
        if(j_f >= m) [[unlikely]]
            return 0.0;

        auto filt_len = (m - j_f - 1)/p + 1;
        if(j_s+1 > in.size()) [[likely]]
        {
            const auto skip = std::min(j_s+1-in.size(), filt_len);
            j_f += p*skip;
            j_s -= skip;
            filt_len -= skip;
        }
        /* Get the range of input samples being used for this output sample.
         * j_s is the first sample and iterates backwards toward 0.
         */
        const auto src = in.first(j_s+1).last(std::min(j_s+1, filt_len));
        return std::accumulate(src.rbegin(), src.rend(), 0.0, [p,f,&j_f](const double cur,
            const double smp) -> double
        {
            const auto ret = cur + f[j_f]*smp;
            j_f += p;
            return ret;
        });
    });
    // Clean up after in-place operation.
    if(work.data() != out.data())
        std::ranges::copy(work, out.begin());
}
