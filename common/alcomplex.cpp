
#include "config.h"

#include "alcomplex.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>
#include <utility>

#include "albit.h"
#include "alnumbers.h"
#include "alnumeric.h"
#include "opthelpers.h"


namespace {

using ushort = unsigned short;
using ushort2 = std::pair<ushort,ushort>;
using complex_d = std::complex<double>;

constexpr size_t BitReverseCounter(size_t log2_size) noexcept
{
    /* Some magic math that calculates the number of swaps needed for a
     * sequence of bit-reversed indices when index < reversed_index.
     */
    return (1u<<(log2_size-1)) - (1u<<((log2_size-1u)/2u));
}


template<size_t N>
struct BitReverser {
    static_assert(N <= sizeof(ushort)*8, "Too many bits for the bit-reversal table.");

    std::array<ushort2,BitReverseCounter(N)> mData{};

    constexpr BitReverser()
    {
        const size_t fftsize{1u << N};
        size_t ret_i{0};

        /* Bit-reversal permutation applied to a sequence of fftsize items. */
        for(size_t idx{1u};idx < fftsize-1;++idx)
        {
            size_t revidx{idx};
            revidx = ((revidx&0xaaaaaaaa) >> 1) | ((revidx&0x55555555) << 1);
            revidx = ((revidx&0xcccccccc) >> 2) | ((revidx&0x33333333) << 2);
            revidx = ((revidx&0xf0f0f0f0) >> 4) | ((revidx&0x0f0f0f0f) << 4);
            revidx = ((revidx&0xff00ff00) >> 8) | ((revidx&0x00ff00ff) << 8);
            revidx = (revidx >> 16) | ((revidx&0x0000ffff) << 16);
            revidx >>= 32-N;

            if(idx < revidx)
            {
                mData[ret_i].first  = static_cast<ushort>(idx);
                mData[ret_i].second = static_cast<ushort>(revidx);
                ++ret_i;
            }
        }
        assert(ret_i == std::size(mData));
    }
};

/* These bit-reversal swap tables support up to 11-bit indices (2048 elements),
 * which is large enough for the filters and effects in OpenAL Soft. Larger FFT
 * requests will use a slower table-less path.
 */
constexpr BitReverser<2> BitReverser2{};
constexpr BitReverser<3> BitReverser3{};
constexpr BitReverser<4> BitReverser4{};
constexpr BitReverser<5> BitReverser5{};
constexpr BitReverser<6> BitReverser6{};
constexpr BitReverser<7> BitReverser7{};
constexpr BitReverser<8> BitReverser8{};
constexpr BitReverser<9> BitReverser9{};
constexpr BitReverser<10> BitReverser10{};
constexpr BitReverser<11> BitReverser11{};
constexpr std::array<al::span<const ushort2>,12> gBitReverses{{
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
constexpr std::array<std::complex<T>,gBitReverses.size()-1> gArgAngle{{
    {static_cast<T>(-1.00000000000000000e+00), static_cast<T>(0.00000000000000000e+00)},
    {static_cast<T>( 0.00000000000000000e+00), static_cast<T>(1.00000000000000000e+00)},
    {static_cast<T>( 7.07106781186547524e-01), static_cast<T>(7.07106781186547524e-01)},
    {static_cast<T>( 9.23879532511286756e-01), static_cast<T>(3.82683432365089772e-01)},
    {static_cast<T>( 9.80785280403230449e-01), static_cast<T>(1.95090322016128268e-01)},
    {static_cast<T>( 9.95184726672196886e-01), static_cast<T>(9.80171403295606020e-02)},
    {static_cast<T>( 9.98795456205172393e-01), static_cast<T>(4.90676743274180143e-02)},
    {static_cast<T>( 9.99698818696204220e-01), static_cast<T>(2.45412285229122880e-02)},
    {static_cast<T>( 9.99924701839144541e-01), static_cast<T>(1.22715382857199261e-02)},
    {static_cast<T>( 9.99981175282601143e-01), static_cast<T>(6.13588464915447536e-03)},
    {static_cast<T>( 9.99995293809576172e-01), static_cast<T>(3.06795676296597627e-03)}
}};

} // namespace

void complex_fft(const al::span<std::complex<double>> buffer, const double sign)
{
    const size_t fftsize{buffer.size()};
    /* Get the number of bits used for indexing. Simplifies bit-reversal and
     * the main loop count.
     */
    const size_t log2_size{static_cast<size_t>(al::countr_zero(fftsize))};

    if(log2_size < gBitReverses.size()) LIKELY
    {
        for(auto &rev : gBitReverses[log2_size])
            std::swap(buffer[rev.first], buffer[rev.second]);

        /* Iterative form of Danielson-Lanczos lemma */
        for(size_t i{0};i < log2_size;++i)
        {
            const size_t step2{1_uz << i};
            const size_t step{2_uz << i};
            /* The first iteration of the inner loop would have u=1, which we
             * can simplify to remove a number of complex multiplies.
             */
            for(size_t k{0};k < fftsize;k+=step)
            {
                const complex_d temp{buffer[k+step2]};
                buffer[k+step2] = buffer[k] - temp;
                buffer[k] += temp;
            }

            const complex_d w{gArgAngle<double>[i].real(), gArgAngle<double>[i].imag()*sign};
            complex_d u{w};
            for(size_t j{1};j < step2;j++)
            {
                for(size_t k{j};k < fftsize;k+=step)
                {
                    const complex_d temp{buffer[k+step2] * u};
                    buffer[k+step2] = buffer[k] - temp;
                    buffer[k] += temp;
                }
                u *= w;
            }
        }
    }
    else
    {
        for(size_t idx{1u};idx < fftsize-1;++idx)
        {
            size_t revidx{idx};
            revidx = ((revidx&0xaaaaaaaa) >> 1) | ((revidx&0x55555555) << 1);
            revidx = ((revidx&0xcccccccc) >> 2) | ((revidx&0x33333333) << 2);
            revidx = ((revidx&0xf0f0f0f0) >> 4) | ((revidx&0x0f0f0f0f) << 4);
            revidx = ((revidx&0xff00ff00) >> 8) | ((revidx&0x00ff00ff) << 8);
            revidx = (revidx >> 16) | ((revidx&0x0000ffff) << 16);
            revidx >>= 32-log2_size;

            if(idx < revidx)
                std::swap(buffer[idx], buffer[revidx]);
        }

        const double pi{al::numbers::pi * sign};
        for(size_t i{0};i < log2_size;++i)
        {
            const size_t step2{1_uz << i};
            const size_t step{2_uz << i};
            for(size_t k{0};k < fftsize;k+=step)
            {
                const complex_d temp{buffer[k+step2]};
                buffer[k+step2] = buffer[k] - temp;
                buffer[k] += temp;
            }

            const double arg{pi / static_cast<double>(step2)};
            const complex_d w{std::polar(1.0, arg)};
            complex_d u{w};
            for(size_t j{1};j < step2;j++)
            {
                for(size_t k{j};k < fftsize;k+=step)
                {
                    const complex_d temp{buffer[k+step2] * u};
                    buffer[k+step2] = buffer[k] - temp;
                    buffer[k] += temp;
                }
                u *= w;
            }
        }
    }
}

void complex_hilbert(const al::span<std::complex<double>> buffer)
{
    using namespace std::placeholders;

    inverse_fft(buffer);

    const double inverse_size = 1.0/static_cast<double>(buffer.size());
    auto bufiter = buffer.begin();
    const auto halfiter = bufiter + (buffer.size()>>1);

    *bufiter *= inverse_size; ++bufiter;
    bufiter = std::transform(bufiter, halfiter, bufiter,
        [scale=inverse_size*2.0](std::complex<double> d){ return d * scale; });
    *bufiter *= inverse_size; ++bufiter;

    std::fill(bufiter, buffer.end(), std::complex<double>{});

    forward_fft(buffer);
}
