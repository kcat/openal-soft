
#include "config.h"

#include "alcomplex.h"

#include <algorithm>
#include <array>
#include <bit>
#include <numbers>
#include <ranges>

#include "alnumeric.h"
#include "gsl/gsl"


namespace {

using u16x2 = std::array<u16, 2>;
using complex_d = std::complex<f64>;

[[nodiscard]]
constexpr auto BitReverseCounter(usize const log2_size) noexcept -> usize
{
    /* Some magic math that calculates the number of swaps needed for a
     * sequence of bit-reversed indices when index < reversed_index.
     */
    return (1_zu<<(log2_size-1)) - (1_zu<<((log2_size-1_zu)/2_zu));
}


template<usize N>
struct BitReverser {
    static_assert(N <= sizeof(u16)*8, "Too many bits for the bit-reversal table.");

    std::array<u16x2, BitReverseCounter(N)> mData{};

    constexpr BitReverser()
    {
        auto const fftsize = 1_uz << N;
        auto ret_i = 0_uz;

        /* Bit-reversal permutation applied to a sequence of fftsize items. */
        for(auto const idx : std::views::iota(1_uz, fftsize-1_uz))
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
                mData[ret_i][0] = u16{idx};
                mData[ret_i][1] = u16{revidx};
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
constexpr auto gBitReverses = std::array<std::span<u16x2 const>, 12>{{
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
constexpr auto gArgAngle = std::array<std::complex<T>, gBitReverses.size()-1>{{
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

void complex_fft(std::span<std::complex<f64>> const buffer, f64 const sign)
{
    auto const fftsize = buffer.size();
    /* Get the number of bits used for indexing. Simplifies bit-reversal and
     * the main loop count.
     */
    if(auto const log2_size = gsl::narrow_cast<usize>(std::countr_zero(fftsize));
        log2_size < gBitReverses.size()) [[likely]]
    {
        for(auto &rev : gBitReverses[log2_size])
            std::swap(buffer[rev[0].c_val], buffer[rev[1].c_val]);

        /* Iterative form of Danielson-Lanczos lemma */
        for(auto const i : std::views::iota(0_uz, log2_size))
        {
            auto const step2 = 1_uz << i;
            auto const step = 2_uz << i;
            /* The first iteration of the inner loop would have u=1, which we
             * can simplify to remove a number of complex multiplies.
             */
            for(auto k = 0_uz;k < fftsize;k+=step)
            {
                auto const temp = buffer[k+step2];
                buffer[k+step2] = buffer[k] - temp;
                buffer[k] += temp;
            }

            auto const w = complex_d{gArgAngle<f64>[i].real(),gArgAngle<f64>[i].imag()*sign};
            auto u = w;
            for(auto const j : std::views::iota(1_uz, step2))
            {
                for(auto k = j;k < fftsize;k+=step)
                {
                    auto const temp = buffer[k+step2] * u;
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

        for(auto const idx : std::views::iota(1_uz, fftsize-1))
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

        auto const pi = std::numbers::pi * sign;
        for(auto const i : std::views::iota(0_uz, log2_size))
        {
            auto const step2 = 1_uz << i;
            auto const step = 2_uz << i;
            for(auto k = 0_uz;k < fftsize;k+=step)
            {
                auto const temp = buffer[k+step2];
                buffer[k+step2] = buffer[k] - temp;
                buffer[k] += temp;
            }

            auto const arg = pi / gsl::narrow_cast<f64>(step2);
            auto const w = std::polar(1.0, arg);
            auto u = w;
            for(auto const j : std::views::iota(1_uz, step2))
            {
                for(auto k = j;k < fftsize;k+=step)
                {
                    auto const temp = buffer[k+step2] * u;
                    buffer[k+step2] = buffer[k] - temp;
                    buffer[k] += temp;
                }
                u *= w;
            }
        }
    }
}

void complex_hilbert(std::span<std::complex<f64>> const buffer)
{
    inverse_fft(buffer);

    auto const inverse_size = 1.0 / gsl::narrow_cast<f64>(buffer.size());
    auto bufiter = buffer.begin();
    auto const halfiter = std::next(bufiter, gsl::narrow_cast<ptrdiff_t>(buffer.size()>>1));

    *bufiter *= inverse_size; ++bufiter;
    bufiter = std::transform(bufiter, halfiter, bufiter,
        [scale=inverse_size*2.0](complex_d const d){ return d * scale; });
    *bufiter *= inverse_size; ++bufiter;

    std::fill(bufiter, buffer.end(), complex_d{});

    forward_fft(buffer);
}
