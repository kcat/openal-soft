#ifndef CORE_ALLPASS_CONV_HPP
#define CORE_ALLPASS_CONV_HPP

#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <ranges>
#include <vector>

#include "alcomplex.h"
#include "altypes.hpp"
#include "gsl/gsl"
#include "phase_shifter.h"
#include "pffft.h"
#include "vector.h"

/* Convolution is implemented using a segmented overlap-add method. The filter
 * response is broken up into multiple segments of 128 samples, and each
 * segment has an FFT applied with a 256-sample buffer (the latter half left
 * silent) to get its frequency-domain response.
 *
 * Input samples are similarly broken up into 128-sample segments, with a 256-
 * sample FFT applied to each new incoming segment to get its frequency-domain
 * response. A history of FFT'd input segments is maintained, equal to the
 * number of filter response segments.
 *
 * To apply the convolution, each filter response segment is convolved with its
 * paired input segment (using complex multiplies, far cheaper than time-domain
 * FIRs), accumulating into an FFT buffer. The input history is then shifted to
 * align with later filter response segments for the next input segment.
 *
 * An inverse FFT is then applied to the accumulated FFT buffer to get a 256-
 * sample time-domain response for output, which is split in two halves. The
 * first half is the 128-sample output, and the second half is a 128-sample
 * (really, 127) delayed extension, which gets added to the output next time.
 * Convolving two time-domain responses of length N results in a time-domain
 * signal of length N*2 - 1, and this holds true regardless of the convolution
 * being applied in the frequency domain, so these "overflow" samples need to
 * be accounted for.
 */
template<usize FilterSize>
struct SegmentedFilter {
    static constexpr auto sFftLength = 256_uz;
    static constexpr auto sSampleLength = sFftLength / 2_uz;
    static constexpr auto sNumSegments = FilterSize / sSampleLength;
    static_assert(FilterSize >= sFftLength);
    static_assert((FilterSize % sSampleLength) == 0);

    PFFFTSetup mFft;
    alignas(16) std::array<f32, sFftLength*sNumSegments> mFilterData;

    SegmentedFilter() noexcept : mFft{sFftLength, PFFFT_REAL}
    {
        /* To set up the filter, we first need to generate the desired
         * response (not reversed).
         */
        auto tmpBuffer = std::vector(FilterSize, 0.0);
        for(const auto i : std::views::iota(0_uz, FilterSize/2))
        {
            const auto k = int{FilterSize/2} - gsl::narrow_cast<int>(i*2 + 1);

            const auto w = 2.0*std::numbers::pi/f64{FilterSize}
                * gsl::narrow_cast<f64>(i*2 + 1);
            const auto window = 0.3635819 - 0.4891775*std::cos(w) + 0.1365995*std::cos(2.0*w)
                - 0.0106411*std::cos(3.0*w);

            const auto pk = std::numbers::pi * gsl::narrow_cast<f64>(k);
            tmpBuffer[i*2 + 1] = window * (1.0-std::cos(pk)) / pk;
        }

        /* The response is split into segments that are converted to the
         * frequency domain, each on their own (0 stuffed).
         */
        using complex_d = std::complex<f64>;
        auto fftBuffer = std::vector<complex_d>(sFftLength);
        auto fftTmp = al::vector<f32, 16>(sFftLength);
        auto filter = mFilterData.begin();
        for(const auto s : std::views::iota(0_uz, sNumSegments))
        {
            const auto tmpspan = std::span{tmpBuffer}.subspan(sSampleLength*s, sSampleLength);
            auto iter = std::ranges::copy(tmpspan, fftBuffer.begin()).out;
            std::ranges::fill(iter, fftBuffer.end(), complex_d{});
            forward_fft(fftBuffer);

            /* Convert to zdomain data for PFFFT, scaled by the FFT length so
             * the iFFT result will be normalized.
             */
            for(const auto i : std::views::iota(0_uz, sSampleLength))
            {
                fftTmp[i*2 + 0] = gsl::narrow_cast<f32>(fftBuffer[i].real()) / f32{sFftLength};
                fftTmp[i*2 + 1] = gsl::narrow_cast<f32>((i==0) ? fftBuffer[sSampleLength].real()
                    : fftBuffer[i].imag()) / f32{sFftLength};
            }
            mFft.zreorder(fftTmp.begin(), filter, PFFFT_BACKWARD);
            std::advance(filter, sFftLength);
        }
    }
};

template<usize N>
inline const SegmentedFilter<N> gSegmentedFilter;

template<usize N>
inline const PhaseShifterT<N> gPShifter;

#endif /* CORE_ALLPASS_CONV_HPP */
