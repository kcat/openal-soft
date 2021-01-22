
#include "config.h"

#include "alcomplex.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "albit.h"
#include "alnumeric.h"
#include "math_defs.h"


void complex_fft(const al::span<std::complex<double>> buffer, const double sign)
{
    const size_t fftsize{buffer.size()};
    /* Get the number of bits used for indexing. Simplifies bit-reversal and
     * the main loop count.
     */
    const size_t log2_size{static_cast<size_t>(al::countr_zero(fftsize))};

    /* Bit-reversal permutation applied to a sequence of fftsize items. */
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

    /* Iterative form of Danielson-Lanczos lemma */
    size_t step2{1u};
    for(size_t i{0};i < log2_size;++i)
    {
        const double arg{al::MathDefs<double>::Pi() / static_cast<double>(step2)};

        const std::complex<double> w{std::cos(arg), std::sin(arg)*sign};
        std::complex<double> u{1.0, 0.0};
        const size_t step{step2 << 1};
        for(size_t j{0};j < step2;j++)
        {
            for(size_t k{j};k < fftsize;k+=step)
            {
                std::complex<double> temp{buffer[k+step2] * u};
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
    inverse_fft(buffer);

    const double inverse_size = 1.0/static_cast<double>(buffer.size());
    auto bufiter = buffer.begin();
    const auto halfiter = bufiter + (buffer.size()>>1);

    *bufiter *= inverse_size; ++bufiter;
    bufiter = std::transform(bufiter, halfiter, bufiter,
        [inverse_size](const std::complex<double> &c) -> std::complex<double>
        { return c * (2.0*inverse_size); });
    *bufiter *= inverse_size; ++bufiter;

    std::fill(bufiter, buffer.end(), std::complex<double>{});

    forward_fft(buffer);
}
