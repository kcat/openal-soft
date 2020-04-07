
#include "bsinc_tables.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

#include "math_defs.h"
#include "vector.h"


namespace {

/* The max points includes the doubling for downsampling, so the maximum number
 * of base sample points is 24, which is 23rd order.
 */
constexpr int BSincPointsMax{BSINC_POINTS_MAX};
constexpr int BSincPointsHalf{BSincPointsMax / 2};

constexpr int BSincPhaseCount{BSINC_PHASE_COUNT};
constexpr int BSincScaleCount{BSINC_SCALE_COUNT};


template<typename T>
constexpr std::enable_if_t<std::is_floating_point<T>::value,T> sqrt(T x)
{
    if(!(x >= 0 && x < std::numeric_limits<double>::infinity()))
        throw std::domain_error{"Invalid sqrt value"};

    T cur{x}, prev{0};
    while(cur != prev)
    {
        prev = cur;
        cur = 0.5f*(cur + x/cur);
    }
    return cur;
}

template<typename T>
constexpr std::enable_if_t<std::is_floating_point<T>::value,T> sin(T x)
{
    if(x >= al::MathDefs<T>::Tau())
    {
        if(!(x < 65536))
            throw std::domain_error{"Invalid sin value"};
        do {
            x -= al::MathDefs<T>::Tau();
        } while(x >= al::MathDefs<T>::Tau());
    }
    else if(x < 0)
    {
        if(!(x > -65536))
            throw std::domain_error{"Invalid sin value"};
        do {
            x += al::MathDefs<T>::Tau();
        } while(x < 0);
    }

    T prev{x}, n{6};
    int i{4}, s{-1};
    const T xx{x*x};
    T t{xx*x};

    T cur{prev + t*s/n};
    while(prev != cur)
    {
        prev = cur;
        n *= i*(i+1);
        i += 2;
        s = -s;
        t *= xx;

        cur += t*s/n;
    }
    return cur;
}


/* This is the normalized cardinal sine (sinc) function.
 *
 *   sinc(x) = { 1,                   x = 0
 *             { sin(pi x) / (pi x),  otherwise.
 */
constexpr double Sinc(const double x)
{
    if(!(x > 1e-15 || x < -1e-15))
        return 1.0;
    return sin(al::MathDefs<double>::Pi()*x) / (al::MathDefs<double>::Pi()*x);
}

/* The zero-order modified Bessel function of the first kind, used for the
 * Kaiser window.
 *
 *   I_0(x) = sum_{k=0}^inf (1 / k!)^2 (x / 2)^(2 k)
 *          = sum_{k=0}^inf ((x / 2)^k / k!)^2
 */
constexpr double BesselI_0(const double x)
{
    /* Start at k=1 since k=0 is trivial. */
    const double x2{x / 2.0};
    double term{1.0};
    double sum{1.0};
    double last_sum{};
    int k{1};

    /* Let the integration converge until the term of the sum is no longer
     * significant.
     */
    do {
        const double y{x2 / k};
        ++k;
        last_sum = sum;
        term *= y * y;
        sum += term;
    } while(sum != last_sum);

    return sum;
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
    return BesselI_0(beta * sqrt(1.0 - k*k)) / besseli_0_beta;
}

/* Calculates the (normalized frequency) transition width of the Kaiser window.
 * Rejection is in dB.
 */
constexpr double CalcKaiserWidth(const double rejection, const int order)
{
    if(rejection > 21.19)
       return (rejection - 7.95) / (order * 2.285 * al::MathDefs<double>::Tau());
    /* This enforces a minimum rejection of just above 21.18dB */
    return 5.79 / (order * al::MathDefs<double>::Tau());
}

/* Calculates the beta value of the Kaiser window. Rejection is in dB. */
constexpr double CalcKaiserBeta(const double rejection)
{
    if(rejection > 50.0)
       return 0.1102 * (rejection-8.7);
    else if(rejection >= 21.0)
       return (0.5842 * std::pow(rejection-21.0, 0.4)) + (0.07886 * (rejection-21.0));
    return 0.0;
}


struct BSincHeader {
    double width;
    double beta;
    double scaleBase;
    double scaleRange;
    double besseli_0_beta;

    int a[BSINC_SCALE_COUNT];
    int total_size;
};

constexpr BSincHeader GenerateBSincHeader(int Rejection, int Order)
{
    BSincHeader ret{};
    ret.width = CalcKaiserWidth(Rejection, Order);
    ret.beta = CalcKaiserBeta(Rejection);
    ret.scaleBase = ret.width / 2.0;
    ret.scaleRange = 1.0 - ret.scaleBase;
    ret.besseli_0_beta = BesselI_0(ret.beta);

    int num_points{Order+1};
    for(int si{0};si < BSincScaleCount;++si)
    {
        const double scale{ret.scaleBase + (ret.scaleRange * si / (BSincScaleCount - 1))};
        const int a{std::min(static_cast<int>(num_points / 2.0 / scale), num_points)};
        const int m{2 * a};

        ret.a[si] = a;
        ret.total_size += 4 * BSincPhaseCount * ((m+3) & ~3);
    }

    return ret;
}

/* 11th and 23rd order filters (12 and 24-point respectively) with a 60dB drop
 * at nyquist. Each filter will scale up the order when downsampling, to 23rd
 * and 47th order respectively.
 */
constexpr BSincHeader bsinc12_hdr{GenerateBSincHeader(60, 11)};
constexpr BSincHeader bsinc24_hdr{GenerateBSincHeader(60, 23)};


/* FIXME: This should be constexpr, but the temporary filter arrays are too
 * big. This requires using heap space, which is not allowed in a constexpr
 * function (maybe in C++20).
 */
template<size_t total_size>
std::array<float,total_size> GenerateBSincCoeffs(const BSincHeader &hdr)
{
    auto filter = std::make_unique<double[][BSincPhaseCount+1][BSincPointsMax]>(BSincScaleCount);

    /* Calculate the Kaiser-windowed Sinc filter coefficients for each scale
     * and phase index.
     */
    for(unsigned int si{0};si < BSincScaleCount;++si)
    {
        const int m{hdr.a[si] * 2};
        const int o{BSincPointsHalf - (m/2)};
        const int l{hdr.a[si] - 1};
        const int a{hdr.a[si]};
        const double scale{hdr.scaleBase + (hdr.scaleRange * si / (BSincScaleCount - 1))};
        const double cutoff{scale - (hdr.scaleBase * std::max(0.5, scale) * 2.0)};

        /* Do one extra phase index so that the phase delta has a proper target
         * for its last index.
         */
        for(int pi{0};pi <= BSincPhaseCount;++pi)
        {
            const double phase{l + (pi/double{BSincPhaseCount})};

            for(int i{0};i < m;++i)
            {
                const double x{i - phase};
                filter[si][pi][o+i] = Kaiser(hdr.beta, x/a, hdr.besseli_0_beta) * cutoff *
                    Sinc(cutoff*x);
            }
        }
    }

    auto ret = std::make_unique<std::array<float,total_size>>();
    size_t idx{0};

    for(unsigned int si{0};si < BSincScaleCount-1;++si)
    {
        const int m{((hdr.a[si]*2) + 3) & ~3};
        const int o{BSincPointsHalf - (m/2)};

        for(int pi{0};pi < BSincPhaseCount;++pi)
        {
            /* Write out the filter. Also calculate and write out the phase and
             * scale deltas.
             */
            for(int i{0};i < m;++i)
                (*ret)[idx++] = static_cast<float>(filter[si][pi][o+i]);

            /* Linear interpolation between phases is simplified by pre-
             * calculating the delta (b - a) in: x = a + f (b - a)
             */
            for(int i{0};i < m;++i)
            {
                const double phDelta{filter[si][pi+1][o+i] - filter[si][pi][o+i]};
                (*ret)[idx++] = static_cast<float>(phDelta);
            }

            /* Linear interpolation between scales is also simplified.
             *
             * Given a difference in points between scales, the destination
             * points will be 0, thus: x = a + f (-a)
             */
            for(int i{0};i < m;++i)
            {
                const double scDelta{filter[si+1][pi][o+i] - filter[si][pi][o+i]};
                (*ret)[idx++] = static_cast<float>(scDelta);
            }

            /* This last simplification is done to complete the bilinear
             * equation for the combination of phase and scale.
             */
            for(int i{0};i < m;++i)
            {
                const double spDelta{(filter[si+1][pi+1][o+i] - filter[si+1][pi][o+i]) -
                    (filter[si][pi+1][o+i] - filter[si][pi][o+i])};
                (*ret)[idx++] = static_cast<float>(spDelta);
            }
        }
    }
    {
        /* The last scale index doesn't have any scale or scale-phase deltas. */
        const unsigned int si{BSincScaleCount - 1};
        const int m{((hdr.a[si]*2) + 3) & ~3};
        const int o{BSincPointsHalf - (m/2)};

        for(int pi{0};pi < BSincPhaseCount;++pi)
        {
            for(int i{0};i < m;++i)
                (*ret)[idx++] = static_cast<float>(filter[si][pi][o+i]);
            for(int i{0};i < m;++i)
            {
                const double phDelta{filter[si][pi+1][o+i] - filter[si][pi][o+i]};
                (*ret)[idx++] = static_cast<float>(phDelta);
            }
            for(int i{0};i < m;++i)
                (*ret)[idx++] = 0.0f;
            for(int i{0};i < m;++i)
                (*ret)[idx++] = 0.0f;
        }
    }
    assert(idx == total_size);

    return *ret;
}

/* FIXME: These can't be constexpr due to the calls reaching the compiler's
 * step limit.
 */
alignas(16) const auto bsinc12_table = GenerateBSincCoeffs<bsinc12_hdr.total_size>(bsinc12_hdr);
alignas(16) const auto bsinc24_table = GenerateBSincCoeffs<bsinc24_hdr.total_size>(bsinc24_hdr);


constexpr BSincTable GenerateBSincTable(const BSincHeader &hdr, const float *tab)
{
    BSincTable ret{};
    ret.scaleBase = static_cast<float>(hdr.scaleBase);
    ret.scaleRange = static_cast<float>(1.0 / hdr.scaleRange);
    for(int i{0};i < BSincScaleCount;++i)
        ret.m[i] = static_cast<unsigned int>(((hdr.a[i]*2) + 3) & ~3);
    ret.filterOffset[0] = 0;
    for(int i{1};i < BSincScaleCount;++i)
        ret.filterOffset[i] = ret.filterOffset[i-1] + ret.m[i-1]*4*BSincPhaseCount;
    ret.Tab = tab;
    return ret;
}

} // namespace

const BSincTable bsinc12{GenerateBSincTable(bsinc12_hdr, &bsinc12_table.front())};
const BSincTable bsinc24{GenerateBSincTable(bsinc24_hdr, &bsinc24_table.front())};
