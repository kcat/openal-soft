
#include "config.h"

#include "alcomplex.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <numbers>
#include <ranges>
#include <utility>

#include "alnumeric.h"
#include "gsl/gsl"


namespace {

using ushort = unsigned short;
using ushort2 = std::array<ushort,2>;
using complex_d = std::complex<double>;

constexpr auto BitReverseCounter(std::size_t log2_size) noexcept -> std::size_t
{
    /* Some magic math that calculates the number of swaps needed for a
     * sequence of bit-reversed indices when index < reversed_index.
     */
    return (1_zu<<(log2_size-1)) - (1_zu<<((log2_size-1_zu)/2_zu));
}


template<std::size_t N>
struct BitReverser {
    static_assert(N <= sizeof(ushort)*8, "Too many bits for the bit-reversal table.");

    std::array<ushort2,BitReverseCounter(N)> mData{};

    constexpr BitReverser()
    {
        const auto fftsize = 1_uz << N;
        auto ret_i = 0_uz;

        /* Bit-reversal permutation applied to a sequence of fftsize items. */
        for(const auto idx : std::views::iota(1_uz, fftsize-1_uz))
        {
            auto revidx = idx;
            revidx = ((revidx&0xaaaaaaaa) >> 1) | ((revidx&0x55555555) << 1);
            revidx = ((revidx&0xcccccccc) >> 2) | ((revidx&0x33333333) << 2);
            revidx = ((revidx&0xf0f0f0f0) >> 4) | ((revidx&0x0f0f0f0f) << 4);
            revidx = ((revidx&0xff00ff00) >> 8) | ((revidx&0x00ff00ff) << 8);
            revidx = (revidx >> 16) | ((revidx&0x0000ffff) << 16);
            revidx >>= 32-N;

            if(idx < revidx)
            {
                mData[ret_i][0] = gsl::narrow<ushort>(idx);
                mData[ret_i][1] = gsl::narrow<ushort>(revidx);
                ++ret_i;
            }
        }
        Ensures(ret_i == std::size(mData));
    }
};

/* These bit-reversal swap tables support up to 11-bit indices (2048 elements),
 * which is large enough for the filters and effects in OpenAL Soft. Larger FFT
 * requests will use a slower table-less path.
 */
constexpr auto BitReverser2 = BitReverser<2>{};
constexpr auto BitReverser3 = BitReverser<3>{};
constexpr auto BitReverser4 = BitReverser<4>{};
constexpr auto BitReverser5 = BitReverser<5>{};
constexpr auto BitReverser6 = BitReverser<6>{};
constexpr auto BitReverser7 = BitReverser<7>{};
constexpr auto BitReverser8 = BitReverser<8>{};
constexpr auto BitReverser9 = BitReverser<9>{};
constexpr auto BitReverser10 = BitReverser<10>{};
constexpr auto BitReverser11 = BitReverser<11>{};
constexpr auto gBitReverses = std::array<std::span<const ushort2>,12>{{
    {}, {},
    BitReverser2.mData,
    BitReverser3.mData,
    BitReverser4.mData,
    BitReverser5.mData,
    BitReverser6.mData,
    BitReverser7.mData,
    BitReverser8.mData,
    BitReverser9.mData,
    BitReverser10.mData,
    BitReverser11.mData
}};

/* Lookup table for std::polar(1, pi / (1<<index)); */
template<typename T>
constexpr auto gArgAngle = std::array<std::complex<T>,gBitReverses.size()-1>{{
    {gsl::narrow_cast<T>(-1.00000000000000000e+00), gsl::narrow_cast<T>(0.00000000000000000e+00)},
    {gsl::narrow_cast<T>( 0.00000000000000000e+00), gsl::narrow_cast<T>(1.00000000000000000e+00)},
    {gsl::narrow_cast<T>( 7.07106781186547524e-01), gsl::narrow_cast<T>(7.07106781186547524e-01)},
    {gsl::narrow_cast<T>( 9.23879532511286756e-01), gsl::narrow_cast<T>(3.82683432365089772e-01)},
    {gsl::narrow_cast<T>( 9.80785280403230449e-01), gsl::narrow_cast<T>(1.95090322016128268e-01)},
    {gsl::narrow_cast<T>( 9.95184726672196886e-01), gsl::narrow_cast<T>(9.80171403295606020e-02)},
    {gsl::narrow_cast<T>( 9.98795456205172393e-01), gsl::narrow_cast<T>(4.90676743274180143e-02)},
    {gsl::narrow_cast<T>( 9.99698818696204220e-01), gsl::narrow_cast<T>(2.45412285229122880e-02)},
    {gsl::narrow_cast<T>( 9.99924701839144541e-01), gsl::narrow_cast<T>(1.22715382857199261e-02)},
    {gsl::narrow_cast<T>( 9.99981175282601143e-01), gsl::narrow_cast<T>(6.13588464915447536e-03)},
    {gsl::narrow_cast<T>( 9.99995293809576172e-01), gsl::narrow_cast<T>(3.06795676296597627e-03)}
}};

} // namespace

void complex_fft(const std::span<std::complex<double>> buffer, const double sign)
{
    const auto fftsize = buffer.size();
    /* Get the number of bits used for indexing. Simplifies bit-reversal and
     * the main loop count.
     */
    const auto log2_size = gsl::narrow_cast<std::size_t>(std::countr_zero(fftsize));

    if(log2_size < gBitReverses.size()) [[likely]]
    {
        for(auto &rev : gBitReverses[log2_size])
            std::swap(buffer[rev[0]], buffer[rev[1]]);

        /* Iterative form of Danielson-Lanczos lemma */
        for(const auto i : std::views::iota(0_uz, log2_size))
        {
            const auto step2 = 1_uz << i;
            const auto step = 2_uz << i;
            /* The first iteration of the inner loop would have u=1, which we
             * can simplify to remove a number of complex multiplies.
             */
            for(auto k = 0_uz;k < fftsize;k+=step)
            {
                const auto temp = buffer[k+step2];
                buffer[k+step2] = buffer[k] - temp;
                buffer[k] += temp;
            }

            const auto w = complex_d{gArgAngle<double>[i].real(),gArgAngle<double>[i].imag()*sign};
            auto u = w;
            for(const auto j : std::views::iota(1_uz, step2))
            {
                for(auto k = j;k < fftsize;k+=step)
                {
                    const auto temp = buffer[k+step2] * u;
                    buffer[k+step2] = buffer[k] - temp;
                    buffer[k] += temp;
                }
                u *= w;
            }
        }
    }
    else
    {
        Expects(log2_size < 32);

        for(const auto idx : std::views::iota(1_uz, fftsize-1))
        {
            auto revidx = idx;
            revidx = ((revidx&0xaaaaaaaa) >> 1) | ((revidx&0x55555555) << 1);
            revidx = ((revidx&0xcccccccc) >> 2) | ((revidx&0x33333333) << 2);
            revidx = ((revidx&0xf0f0f0f0) >> 4) | ((revidx&0x0f0f0f0f) << 4);
            revidx = ((revidx&0xff00ff00) >> 8) | ((revidx&0x00ff00ff) << 8);
            revidx = (revidx >> 16) | ((revidx&0x0000ffff) << 16);
            revidx >>= 32-log2_size;

            if(idx < revidx)
                std::swap(buffer[idx], buffer[revidx]);
        }

        const auto pi = std::numbers::pi * sign;
        for(const auto i : std::views::iota(0_uz, log2_size))
        {
            const auto step2 = 1_uz << i;
            const auto step = 2_uz << i;
            for(auto k = 0_uz;k < fftsize;k+=step)
            {
                const auto temp = buffer[k+step2];
                buffer[k+step2] = buffer[k] - temp;
                buffer[k] += temp;
            }

            const auto arg = pi / gsl::narrow_cast<double>(step2);
            const auto w = std::polar(1.0, arg);
            auto u = w;
            for(const auto j : std::views::iota(1_uz, step2))
            {
                for(auto k = j;k < fftsize;k+=step)
                {
                    const auto temp = buffer[k+step2] * u;
                    buffer[k+step2] = buffer[k] - temp;
                    buffer[k] += temp;
                }
                u *= w;
            }
        }
    }
}

void complex_hilbert(const std::span<std::complex<double>> buffer)
{
    inverse_fft(buffer);

    const double inverse_size = 1.0 / gsl::narrow_cast<double>(buffer.size());
    auto bufiter = buffer.begin();
    const auto halfiter = std::next(bufiter, gsl::narrow_cast<ptrdiff_t>(buffer.size()>>1));

    *bufiter *= inverse_size; ++bufiter;
    bufiter = std::transform(bufiter, halfiter, bufiter,
        [scale=inverse_size*2.0](std::complex<double> d){ return d * scale; });
    *bufiter *= inverse_size; ++bufiter;

    std::fill(bufiter, buffer.end(), std::complex<double>{});

    forward_fft(buffer);
}
