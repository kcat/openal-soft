
#include "config.h"

#include "uhjfilter.h"

#ifdef HAVE_SSE_INTRINSICS
#include <xmmintrin.h>
#endif

#include <algorithm>
#include <iterator>

#include "AL/al.h"

#include "alcomplex.h"
#include "alnumeric.h"
#include "opthelpers.h"


namespace {

using complex_d = std::complex<double>;

std::array<float,Uhj2Encoder::sFilterSize> GenerateFilter()
{
    /* Some notes on this filter construction.
     *
     * An impulse in the frequency domain is represented by a continuous series
     * of +1,-1 values, with a 0 imaginary term. Consequently, that impulse
     * with a +90 degree phase offset would be represented by 0s with imaginary
     * terms that alternate between +1,-1. Converting that to the time domain
     * results in a FIR filter that can be convolved with the incoming signal
     * to apply a wide-band 90-degree phase shift.
     *
     * A particularly notable aspect of the time-domain filter response is that
     * every other coefficient is 0. This allows doubling the effective size of
     * the filter, by only storing the non-0 coefficients and double-stepping
     * over the input to apply it.
     *
     * Additionally, the resulting filter is independent of the sample rate.
     * The same filter can be applied regardless of the device's sample rate
     * and achieve the same effect, although a lower rate allows the filter to
     * cover more time and improve the results.
     */
    constexpr complex_d c0{0.0,  1.0};
    constexpr complex_d c1{0.0, -1.0};
    constexpr size_t half_size{32768};

    /* Generate a frequency domain impulse with a +90 degree phase offset. Keep
     * the mirrored frequencies clear for converting to the time domain.
     */
    auto fftBuffer = std::vector<complex_d>(half_size*2, complex_d{});
    for(size_t i{0};i < half_size;i += 2)
    {
        fftBuffer[i  ] = c0;
        fftBuffer[i+1] = c1;
    }
    fftBuffer[half_size] = c0;
    complex_fft(fftBuffer, 1.0);

    /* Reverse and truncate the filter to a usable size, and store only the
     * non-0 terms. Should this be windowed?
     */
    std::array<float,Uhj2Encoder::sFilterSize> ret;
    auto fftiter = fftBuffer.data() + half_size + (Uhj2Encoder::sFilterSize-1);
    for(float &coeff : ret)
    {
        coeff = static_cast<float>(fftiter->real() / (half_size+1));
        fftiter -= 2;
    }
    return ret;
}
alignas(16) const auto PShiftCoeffs = GenerateFilter();


void allpass_process(al::span<float> dst, const float *RESTRICT src)
{
#ifdef HAVE_SSE_INTRINSICS
    size_t pos{0};
    if(size_t todo{dst.size()>>1})
    {
        do {
            __m128 r04{_mm_setzero_ps()};
            __m128 r14{_mm_setzero_ps()};
            for(size_t j{0};j < PShiftCoeffs.size();j+=4)
            {
                const __m128 coeffs{_mm_load_ps(&PShiftCoeffs[j])};
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
        for(size_t j{0};j < PShiftCoeffs.size();j+=4)
        {
            const __m128 coeffs{_mm_load_ps(&PShiftCoeffs[j])};
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

#else

    for(float &output : dst)
    {
        float ret{0.0f};
        for(size_t j{0};j < PShiftCoeffs.size();++j)
            ret += src[j*2] * PShiftCoeffs[j];

        output += ret;
        ++src;
    }
#endif
}

} // namespace


/* NOTE: There seems to be a bit of an inconsistency in how this encoding is
 * supposed to work. Some references, such as
 *
 * http://members.tripod.com/martin_leese/Ambisonic/UHJ_file_format.html
 *
 * specify a pre-scaling of sqrt(2) on the W channel input, while other
 * references, such as
 *
 * https://en.wikipedia.org/wiki/Ambisonic_UHJ_format#Encoding.5B1.5D
 * and
 * https://wiki.xiph.org/Ambisonics#UHJ_format
 *
 * do not. The sqrt(2) scaling is in line with B-Format decoder coefficients
 * which include such a scaling for the W channel input, however the original
 * source for this equation is a 1985 paper by Michael Gerzon, which does not
 * apparently include the scaling. Applying the extra scaling creates a louder
 * result with a narrower stereo image compared to not scaling, and I don't
 * know which is the intended result.
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

    /* S = 0.9396926*W + 0.1855740*X */
    std::transform(winput, winput+SamplesToDo, xinput, mMid.begin(),
        [](const float w, const float x) noexcept -> float
        { return 0.9396926f*w + 0.1855740f*x; });

    /* D = 0.6554516*Y */
    std::transform(yinput, yinput+SamplesToDo, mSide.begin(),
        [](const float y) noexcept -> float { return 0.6554516f*y; });

    /* Include any existing direct signal in the mid/side buffers. */
    for(size_t i{0};i < SamplesToDo;++i)
        mMid[i] += left[i] + right[i];
    for(size_t i{0};i < SamplesToDo;++i)
        mSide[i] += left[i] - right[i];

    /* Apply a delay to the non-filtered signal to align with the filter delay. */
    if LIKELY(SamplesToDo >= sFilterSize)
    {
        auto buffer_end = mMid.begin() + SamplesToDo;
        auto delay_end = std::rotate(mMid.begin(), buffer_end - sFilterSize, buffer_end);
        std::swap_ranges(mMid.begin(), delay_end, mMidDelay.begin());

        buffer_end = mSide.begin() + SamplesToDo;
        delay_end = std::rotate(mSide.begin(), buffer_end - sFilterSize, buffer_end);
        std::swap_ranges(mSide.begin(), delay_end, mSideDelay.begin());
    }
    else
    {
        auto buffer_end = mMid.begin() + SamplesToDo;
        auto delay_start = std::swap_ranges(mMid.begin(), buffer_end, mMidDelay.begin());
        std::rotate(mMidDelay.begin(), delay_start, mMidDelay.end());

        buffer_end = mSide.begin() + SamplesToDo;
        delay_start = std::swap_ranges(mSide.begin(), buffer_end, mSideDelay.begin());
        std::rotate(mSideDelay.begin(), delay_start, mSideDelay.end());
    }

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
