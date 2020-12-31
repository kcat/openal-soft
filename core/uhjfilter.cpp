
#include "config.h"

#include "uhjfilter.h"

#ifdef HAVE_SSE_INTRINSICS
#include <xmmintrin.h>
#elif defined(HAVE_NEON)
#include <arm_neon.h>
#endif

#include <algorithm>
#include <iterator>

#include "alcomplex.h"
#include "alnumeric.h"
#include "opthelpers.h"


namespace {

using complex_d = std::complex<double>;

struct PhaseShifterT {
    alignas(16) std::array<float,Uhj2Encoder::sFilterSize> Coeffs;

    /* Some notes on this filter construction.
     *
     * A wide-band phase-shift filter needs a delay to maintain linearity. A
     * dirac impulse in the center of a time-domain buffer represents a filter
     * passing all frequencies through as-is with a pure delay. Converting that
     * to the frequency domain, adjusting the phase of each frequency bin by
     * +90 degrees, then converting back to the time domain, results in a FIR
     * filter that applies a +90 degree wide-band phase-shift.
     *
     * A particularly notable aspect of the time-domain filter response is that
     * every other coefficient is 0. This allows doubling the effective size of
     * the filter, by storing only the non-0 coefficients and double-stepping
     * over the input to apply it.
     *
     * Additionally, the resulting filter is independent of the sample rate.
     * The same filter can be applied regardless of the device's sample rate
     * and achieve the same effect.
     */
    PhaseShifterT()
    {
        constexpr size_t fft_size{Uhj2Encoder::sFilterSize * 2};
        constexpr size_t half_size{fft_size / 2};

        /* Generate a frequency domain impulse with a +90 degree phase offset.
         * Reconstruct the mirrored frequencies to convert to the time domain.
         */
        auto fftBuffer = std::make_unique<complex_d[]>(fft_size);
        std::fill_n(fftBuffer.get(), fft_size, complex_d{});
        fftBuffer[half_size] = 1.0;

        forward_fft({fftBuffer.get(), fft_size});
        for(size_t i{0};i < half_size+1;++i)
            fftBuffer[i] = complex_d{-fftBuffer[i].imag(), fftBuffer[i].real()};
        for(size_t i{half_size+1};i < fft_size;++i)
            fftBuffer[i] = std::conj(fftBuffer[fft_size - i]);
        inverse_fft({fftBuffer.get(), fft_size});

        /* Reverse the filter for simpler processing, and store only the non-0
         * coefficients.
         */
        auto fftiter = fftBuffer.get() + half_size + (Uhj2Encoder::sFilterSize-1);
        for(float &coeff : Coeffs)
        {
            coeff = static_cast<float>(fftiter->real() / double{fft_size});
            fftiter -= 2;
        }
    }
};
const PhaseShifterT PShift{};

void allpass_process(al::span<float> dst, const float *RESTRICT src)
{
#ifdef HAVE_SSE_INTRINSICS
    size_t pos{0};
    if(size_t todo{dst.size()>>1})
    {
        do {
            __m128 r04{_mm_setzero_ps()};
            __m128 r14{_mm_setzero_ps()};
            for(size_t j{0};j < PShift.Coeffs.size();j+=4)
            {
                const __m128 coeffs{_mm_load_ps(&PShift.Coeffs[j])};
                const __m128 s0{_mm_loadu_ps(&src[j*2])};
                const __m128 s1{_mm_loadu_ps(&src[j*2 + 4])};

                __m128 s{_mm_shuffle_ps(s0, s1, _MM_SHUFFLE(2, 0, 2, 0))};
                r04 = _mm_add_ps(r04, _mm_mul_ps(s, coeffs));

                s = _mm_shuffle_ps(s0, s1, _MM_SHUFFLE(3, 1, 3, 1));
                r14 = _mm_add_ps(r14, _mm_mul_ps(s, coeffs));
            }
            r04 = _mm_add_ps(r04, _mm_shuffle_ps(r04, r04, _MM_SHUFFLE(0, 1, 2, 3)));
            r04 = _mm_add_ps(r04, _mm_movehl_ps(r04, r04));
            dst[pos++] += _mm_cvtss_f32(r04);

            r14 = _mm_add_ps(r14, _mm_shuffle_ps(r14, r14, _MM_SHUFFLE(0, 1, 2, 3)));
            r14 = _mm_add_ps(r14, _mm_movehl_ps(r14, r14));
            dst[pos++] += _mm_cvtss_f32(r14);

            src += 2;
        } while(--todo);
    }
    if((dst.size()&1))
    {
        __m128 r4{_mm_setzero_ps()};
        for(size_t j{0};j < PShift.Coeffs.size();j+=4)
        {
            const __m128 coeffs{_mm_load_ps(&PShift.Coeffs[j])};
            /* NOTE: This could alternatively be done with two unaligned loads
             * and a shuffle. Which would be better?
             */
            const __m128 s{_mm_setr_ps(src[j*2], src[j*2 + 2], src[j*2 + 4], src[j*2 + 6])};
            r4 = _mm_add_ps(r4, _mm_mul_ps(s, coeffs));
        }
        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));

        dst[pos] += _mm_cvtss_f32(r4);
    }

#elif defined(HAVE_NEON)

    size_t pos{0};
    if(size_t todo{dst.size()>>1})
    {
        /* There doesn't seem to be NEON intrinsics to do this kind of stipple
         * shuffling, so there's two custom methods for it.
         */
        auto shuffle_2020 = [](float32x4_t a, float32x4_t b)
        {
            float32x4_t ret{vmovq_n_f32(vgetq_lane_f32(a, 0))};
            ret = vsetq_lane_f32(vgetq_lane_f32(a, 2), ret, 1);
            ret = vsetq_lane_f32(vgetq_lane_f32(b, 0), ret, 2);
            ret = vsetq_lane_f32(vgetq_lane_f32(b, 2), ret, 3);
            return ret;
        };
        auto shuffle_3131 = [](float32x4_t a, float32x4_t b)
        {
            float32x4_t ret{vmovq_n_f32(vgetq_lane_f32(a, 1))};
            ret = vsetq_lane_f32(vgetq_lane_f32(a, 3), ret, 1);
            ret = vsetq_lane_f32(vgetq_lane_f32(b, 1), ret, 2);
            ret = vsetq_lane_f32(vgetq_lane_f32(b, 3), ret, 3);
            return ret;
        };
        do {
            float32x4_t r04{vdupq_n_f32(0.0f)};
            float32x4_t r14{vdupq_n_f32(0.0f)};
            for(size_t j{0};j < PShift.Coeffs.size();j+=4)
            {
                const float32x4_t coeffs{vld1q_f32(&PShift.Coeffs[j])};
                const float32x4_t s0{vld1q_f32(&src[j*2])};
                const float32x4_t s1{vld1q_f32(&src[j*2 + 4])};

                r04 = vmlaq_f32(r04, shuffle_2020(s0, s1), coeffs);
                r14 = vmlaq_f32(r14, shuffle_3131(s0, s1), coeffs);
            }
            r04 = vaddq_f32(r04, vrev64q_f32(r04));
            dst[pos++] = vget_lane_f32(vadd_f32(vget_low_f32(r04), vget_high_f32(r04)), 0);

            r14 = vaddq_f32(r14, vrev64q_f32(r14));
            dst[pos++] = vget_lane_f32(vadd_f32(vget_low_f32(r14), vget_high_f32(r14)), 0);

            src += 2;
        } while(--todo);
    }
    if((dst.size()&1))
    {
        auto load4 = [](float32_t a, float32_t b, float32_t c, float32_t d)
        {
            float32x4_t ret{vmovq_n_f32(a)};
            ret = vsetq_lane_f32(b, ret, 1);
            ret = vsetq_lane_f32(c, ret, 2);
            ret = vsetq_lane_f32(d, ret, 3);
            return ret;
        };
        float32x4_t r4{vdupq_n_f32(0.0f)};
        for(size_t j{0};j < PShift.Coeffs.size();j+=4)
        {
            const float32x4_t coeffs{vld1q_f32(&PShift.Coeffs[j])};
            const float32x4_t s{load4(src[j*2], src[j*2 + 2], src[j*2 + 4], src[j*2 + 6])};
            r4 = vmlaq_f32(r4, s, coeffs);
        }
        r4 = vaddq_f32(r4, vrev64q_f32(r4));
        dst[pos] = vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0);
    }

#else

    for(float &output : dst)
    {
        float ret{0.0f};
        for(size_t j{0};j < PShift.Coeffs.size();++j)
            ret += src[j*2] * PShift.Coeffs[j];

        output += ret;
        ++src;
    }
#endif
}

} // namespace


/* Encoding 2-channel UHJ from B-Format is done as:
 *
 * S = 0.9396926*W + 0.1855740*X
 * D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y
 *
 * Left = (S + D)/2.0
 * Right = (S - D)/2.0
 *
 * where j is a wide-band +90 degree phase shift.
 *
 * The phase shift is done using a FIR filter derived from an FFT'd impulse
 * with the desired shift.
 */

void Uhj2Encoder::encode(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const FloatBufferLine *InSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    float *RESTRICT left{al::assume_aligned<16>(LeftOut.data())};
    float *RESTRICT right{al::assume_aligned<16>(RightOut.data())};

    const float *RESTRICT winput{al::assume_aligned<16>(InSamples[0].data())};
    const float *RESTRICT xinput{al::assume_aligned<16>(InSamples[1].data())};
    const float *RESTRICT yinput{al::assume_aligned<16>(InSamples[2].data())};

    /* Combine the previously delayed mid/side signal with the input. */

    /* S = 0.9396926*W + 0.1855740*X */
    auto miditer = std::copy(mMidDelay.cbegin(), mMidDelay.cend(), mMid.begin());
    std::transform(winput, winput+SamplesToDo, xinput, miditer,
        [](const float w, const float x) noexcept -> float
        { return 0.9396926f*w + 0.1855740f*x; });

    /* D = 0.6554516*Y */
    auto sideiter = std::copy(mSideDelay.cbegin(), mSideDelay.cend(), mSide.begin());
    std::transform(yinput, yinput+SamplesToDo, sideiter,
        [](const float y) noexcept -> float { return 0.6554516f*y; });

    /* Include any existing direct signal in the mid/side buffers. */
    for(size_t i{0};i < SamplesToDo;++i,++miditer)
        *miditer += left[i] + right[i];
    for(size_t i{0};i < SamplesToDo;++i,++sideiter)
        *sideiter += left[i] - right[i];

    /* Copy the future samples back to the delay buffers for next time. */
    std::copy_n(mMid.cbegin()+SamplesToDo, mMidDelay.size(), mMidDelay.begin());
    std::copy_n(mSide.cbegin()+SamplesToDo, mSideDelay.size(), mSideDelay.begin());

    /* Now add the all-passed signal into the side signal. */

    /* D += j(-0.3420201*W + 0.5098604*X) */
    auto tmpiter = std::copy(mSideHistory.cbegin(), mSideHistory.cend(), mTemp.begin());
    std::transform(winput, winput+SamplesToDo, xinput, tmpiter,
        [](const float w, const float x) noexcept -> float
        { return -0.3420201f*w + 0.5098604f*x; });
    std::copy_n(mTemp.cbegin()+SamplesToDo, mSideHistory.size(), mSideHistory.begin());
    allpass_process({mSide.data(), SamplesToDo}, mTemp.data());

    /* Left = (S + D)/2.0 */
    for(size_t i{0};i < SamplesToDo;i++)
        left[i] = (mMid[i] + mSide[i]) * 0.5f;
    /* Right = (S - D)/2.0 */
    for(size_t i{0};i < SamplesToDo;i++)
        right[i] = (mMid[i] - mSide[i]) * 0.5f;
}
