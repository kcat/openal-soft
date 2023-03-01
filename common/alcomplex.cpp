
#include "config.h"

#include "alcomplex.h"

#include <algorithm>
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

    ushort2 mData[BitReverseCounter(N)]{};

    constexpr BitReverser()
    {
        const size_t fftsize{1u << N};
        size_t ret_i{0};

        /* Bit-reversal permutation applied to a sequence of fftsize items. */
        for(size_t idx{1u};idx < fftsize-1;++idx)
        {
            size_t revidx{0u}, imask{idx};
            for(size_t i{0};i < N;++i)
            {
                revidx = (revidx<<1) | (imask&1);
                imask >>= 1;
            }

            if(idx < revidx)
            {
                mData[ret_i].first  = static_cast<ushort>(idx);
                mData[ret_i].second = static_cast<ushort>(revidx);
                ++ret_i;
            }
        }
        assert(ret_i == al::size(mData));
    }
};

/* These bit-reversal swap tables support up to 10-bit indices (1024 elements),
 * which is the largest used by OpenAL Soft's filters and effects. Larger FFT
 * requests, used by some utilities where performance is less important, will
 * use a slower table-less path.
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
constexpr std::array<al::span<const ushort2>,11> gBitReverses{{
    {}, {},
    BitReverser2.mData,
    BitReverser3.mData,
    BitReverser4.mData,
    BitReverser5.mData,
    BitReverser6.mData,
    BitReverser7.mData,
    BitReverser8.mData,
    BitReverser9.mData,
    BitReverser10.mData
}};

} // namespace

template<typename Real>
std::enable_if_t<std::is_floating_point<Real>::value>
complex_fft(const al::span<std::complex<Real>> buffer, const al::type_identity_t<Real> sign)
{
    const size_t fftsize{buffer.size()};
    /* Get the number of bits used for indexing. Simplifies bit-reversal and
     * the main loop count.
     */
    const size_t log2_size{static_cast<size_t>(al::countr_zero(fftsize))};

    if(log2_size >= gBitReverses.size()) UNLIKELY
    {
        for(size_t idx{1u};idx < fftsize-1;++idx)
        {
            size_t revidx{0u}, imask{idx};
            for(size_t i{0};i < log2_size;++i)
            {
                revidx = (revidx<<1) | (imask&1);
                imask >>= 1;
            }

            if(idx < revidx)
                std::swap(buffer[idx], buffer[revidx]);
        }
    }
    else for(auto &rev : gBitReverses[log2_size])
        std::swap(buffer[rev.first], buffer[rev.second]);

    /* Iterative form of Danielson-Lanczos lemma */
    const Real pi{al::numbers::pi_v<Real> * sign};
    size_t step2{1u};
    for(size_t i{0};i < log2_size;++i)
    {
        const Real arg{pi / static_cast<Real>(step2)};

        /* TODO: Would std::polar(1.0, arg) be any better? */
        const std::complex<Real> w{std::cos(arg), std::sin(arg)};
        std::complex<Real> u{1.0, 0.0};
        const size_t step{step2 << 1};
        for(size_t j{0};j < step2;j++)
        {
            for(size_t k{j};k < fftsize;k+=step)
            {
                std::complex<Real> temp{buffer[k+step2] * u};
                buffer[k+step2] = buffer[k] - temp;
                buffer[k] += temp;
            }

            u *= w;
        }

        step2 <<= 1;
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


template void complex_fft<>(const al::span<std::complex<float>> buffer, const float sign);
template void complex_fft<>(const al::span<std::complex<double>> buffer, const double sign);
