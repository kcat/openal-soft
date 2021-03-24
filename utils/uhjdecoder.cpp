/*
 * 2-channel UHJ Decoder
 *
 * Copyright (c) Chris Robinson <chris.kcat@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "config.h"

#ifdef HAVE_SSE_INTRINSICS
#include <xmmintrin.h>
#elif defined(HAVE_NEON)
#include <arm_neon.h>
#endif

#include <array>
#include <complex>
#include <cstring>
#include <memory>
#include <stddef.h>
#include <string>
#include <utility>
#include <vector>

#include "albit.h"
#include "albyte.h"
#include "alcomplex.h"
#include "almalloc.h"
#include "alspan.h"
#include "vector.h"
#include "opthelpers.h"

#include "sndfile.h"

#include "win_main_utf8.h"


struct FileDeleter {
    void operator()(FILE *file) { fclose(file); }
};
using FilePtr = std::unique_ptr<FILE,FileDeleter>;

struct SndFileDeleter {
    void operator()(SNDFILE *sndfile) { sf_close(sndfile); }
};
using SndFilePtr = std::unique_ptr<SNDFILE,SndFileDeleter>;


using ubyte = unsigned char;
using ushort = unsigned short;
using uint = unsigned int;
using complex_d = std::complex<double>;

using byte4 = std::array<al::byte,4>;


constexpr ubyte SUBTYPE_BFORMAT_FLOAT[]{
    0x03, 0x00, 0x00, 0x00, 0x21, 0x07, 0xd3, 0x11, 0x86, 0x44, 0xc8, 0xc1,
    0xca, 0x00, 0x00, 0x00
};

void fwrite16le(ushort val, FILE *f)
{
    ubyte data[2]{ static_cast<ubyte>(val&0xff), static_cast<ubyte>((val>>8)&0xff) };
    fwrite(data, 1, 2, f);
}

void fwrite32le(uint val, FILE *f)
{
    ubyte data[4]{ static_cast<ubyte>(val&0xff), static_cast<ubyte>((val>>8)&0xff),
        static_cast<ubyte>((val>>16)&0xff), static_cast<ubyte>((val>>24)&0xff) };
    fwrite(data, 1, 4, f);
}

template<al::endian = al::endian::native>
byte4 f32AsLEBytes(const float &value) = delete;

template<>
byte4 f32AsLEBytes<al::endian::little>(const float &value)
{
    byte4 ret{};
    std::memcpy(ret.data(), &value, 4);
    return ret;
}
template<>
byte4 f32AsLEBytes<al::endian::big>(const float &value)
{
    byte4 ret{};
    std::memcpy(ret.data(), &value, 4);
    std::swap(ret[0], ret[3]);
    std::swap(ret[1], ret[2]);
    return ret;
}


constexpr uint BufferLineSize{1024};

using FloatBufferLine = std::array<float,BufferLineSize>;
using FloatBufferSpan = al::span<float,BufferLineSize>;


struct UhjDecoder {
    constexpr static size_t sFilterSize{128};

    alignas(16) std::array<float,BufferLineSize+sFilterSize> mS{};
    alignas(16) std::array<float,BufferLineSize+sFilterSize> mD{};
    alignas(16) std::array<float,BufferLineSize+sFilterSize> mT{};
    alignas(16) std::array<float,BufferLineSize+sFilterSize> mQ{};

    /* History for the FIR filter. */
    alignas(16) std::array<float,sFilterSize-1> mDTHistory{};
    alignas(16) std::array<float,sFilterSize-1> mSHistory{};

    alignas(16) std::array<float,BufferLineSize + sFilterSize*2> mTemp{};

    void decode(const float *RESTRICT InSamples, const al::span<FloatBufferLine> OutSamples,
        const size_t SamplesToDo);
    void decode2(const float *RESTRICT InSamples, const al::span<FloatBufferLine,3> OutSamples,
        const size_t SamplesToDo);

    DEF_NEWDEL(UhjDecoder)
};

/* Same basic filter design as in core/uhjfilter.cpp. */
template<size_t FilterSize>
struct PhaseShifterT {
    static_assert((FilterSize&(FilterSize-1)) == 0, "FilterSize needs to be power-of-two");

    alignas(16) std::array<float,FilterSize> Coeffs{};

    PhaseShifterT()
    {
        constexpr size_t fft_size{FilterSize * 2};
        constexpr size_t half_size{fft_size / 2};

        auto fftBuffer = std::make_unique<complex_d[]>(fft_size);
        std::fill_n(fftBuffer.get(), fft_size, complex_d{});
        fftBuffer[half_size] = 1.0;

        forward_fft({fftBuffer.get(), fft_size});
        for(size_t i{0};i < half_size+1;++i)
            fftBuffer[i] = complex_d{-fftBuffer[i].imag(), fftBuffer[i].real()};
        for(size_t i{half_size+1};i < fft_size;++i)
            fftBuffer[i] = std::conj(fftBuffer[fft_size - i]);
        inverse_fft({fftBuffer.get(), fft_size});

        auto fftiter = fftBuffer.get() + half_size + (FilterSize-1);
        for(float &coeff : Coeffs)
        {
            coeff = static_cast<float>(fftiter->real() / double{fft_size});
            fftiter -= 2;
        }
    }
};
const PhaseShifterT<UhjDecoder::sFilterSize> PShift{};

/* Mostly the same as in core/uhjfilter.cpp, except this overwrites the output
 * instead of adding to it.
 */
void allpass_process(al::span<float> dst, const float *RESTRICT src)
{
#ifdef HAVE_SSE_INTRINSICS
    if(size_t todo{dst.size()>>1})
    {
        auto *out = reinterpret_cast<__m64*>(dst.data());
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
            src += 2;

            __m128 r4{_mm_add_ps(_mm_unpackhi_ps(r04, r14), _mm_unpacklo_ps(r04, r14))};
            r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));

            _mm_storel_pi(out, r4);
            ++out;
        } while(--todo);
    }
    if((dst.size()&1))
    {
        __m128 r4{_mm_setzero_ps()};
        for(size_t j{0};j < PShift.Coeffs.size();j+=4)
        {
            const __m128 coeffs{_mm_load_ps(&PShift.Coeffs[j])};
            const __m128 s{_mm_setr_ps(src[j*2], src[j*2 + 2], src[j*2 + 4], src[j*2 + 6])};
            r4 = _mm_add_ps(r4, _mm_mul_ps(s, coeffs));
        }
        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));

        dst.back() = _mm_cvtss_f32(r4);
    }

#elif defined(HAVE_NEON)

    size_t pos{0};
    if(size_t todo{dst.size()>>1})
    {
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
        auto unpacklo = [](float32x4_t a, float32x4_t b)
        {
            float32x2x2_t result{vzip_f32(vget_low_f32(a), vget_low_f32(b))};
            return vcombine_f32(result.val[0], result.val[1]);
        };
        auto unpackhi = [](float32x4_t a, float32x4_t b)
        {
            float32x2x2_t result{vzip_f32(vget_high_f32(a), vget_high_f32(b))};
            return vcombine_f32(result.val[0], result.val[1]);
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
            src += 2;

            float32x4_t r4{vaddq_f32(unpackhi(r04, r14), unpacklo(r04, r14))};
            float32x2_t r2{vadd_f32(vget_low_f32(r4), vget_high_f32(r4))};

            vst1_f32(&dst[pos], r2);
            pos += 2;
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

        output = ret;
        ++src;
    }
#endif
}


/* Decoding 3- and 4-channel UHJ is done as:
 *
 * S = Left + Right
 * D = Left - Right
 *
 * W = 0.981530*S + 0.197484*j(0.828347*D + 0.767835*T)
 * X = 0.418504*S - j(0.828347*D + 0.767835*T)
 * Y = 0.795954*D - 0.676406*T + j(0.186626*S)
 * Z = 1.023332*Q
 *
 * where j is a +90 degree phase shift. 3-channel UHJ excludes Q/Z.
 *
 * NOTE: Some sources specify
 *
 * S = (Left + Right)/2
 * D = (Left - Right)/2
 *
 * However, this is incorrect. It's halving Left and Right even though they
 * were already halved during encoding, causing S and D to be half what they
 * initially were at the encoding stage. This division is not present in
 * Gerzon's original paper for deriving Sigma (S) or Delta (D) from the L and R
 * signals. As proof, taking Y for example:
 *
 * Y = 0.795954*D - 0.676406*T + j(0.186626*S)
 *
 * * Plug in the encoding parameters, using ? as a placeholder for whether S
 *   and D should receive an extra 0.5 factor
 * Y = 0.795954*(j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y)*? -
 *     0.676406*(j(-0.1432*W + 0.6511746*X) - 0.7071068*Y) +
 *     0.186626*j(0.9396926*W + 0.1855740*X)*?
 *
 * * Move common factors in
 * Y = (j(-0.3420201*0.795954*?*W + 0.5098604*0.795954*?*X) + 0.6554516*0.795954*?*Y) -
 *     (j(-0.1432*0.676406*W + 0.6511746*0.676406*X) - 0.7071068*0.676406*Y) +
 *     j(0.9396926*0.186626*?*W + 0.1855740*0.186626*?*X)
 *
 * * Clean up extraneous groupings
 * Y = j(-0.3420201*0.795954*?*W + 0.5098604*0.795954*?*X) + 0.6554516*0.795954*?*Y -
 *     j(-0.1432*0.676406*W + 0.6511746*0.676406*X) + 0.7071068*0.676406*Y +
 *     j*(0.9396926*0.186626*?*W + 0.1855740*0.186626*?*X)
 *
 * * Move phase shifts together and combine them
 * Y = j(-0.3420201*0.795954*?*W + 0.5098604*0.795954*?*X - -0.1432*0.676406*W -
 *        0.6511746*0.676406*X + 0.9396926*0.186626*?*W + 0.1855740*0.186626*?*X) +
 *     0.6554516*0.795954*?*Y + 0.7071068*0.676406*Y
 *
 * * Reorder terms
 * Y = j(-0.3420201*0.795954*?*W +  0.1432*0.676406*W + 0.9396926*0.186626*?*W +
 *        0.5098604*0.795954*?*X + -0.6511746*0.676406*X + 0.1855740*0.186626*?*X) +
 *     0.7071068*0.676406*Y + 0.6554516*0.795954*?*Y
 *
 * * Move common factors out
 * Y = j((-0.3420201*0.795954*? +  0.1432*0.676406 + 0.9396926*0.186626*?)*W +
 *       ( 0.5098604*0.795954*? + -0.6511746*0.676406 + 0.1855740*0.186626*?)*X) +
 *     (0.7071068*0.676406 + 0.6554516*0.795954*?)*Y
 *
 * * Result w/ 0.5 factor:
 * -0.3420201*0.795954*0.5 + 0.1432*0.676406 + 0.9396926*0.186626*0.5 = 0.04843*W
 * 0.5098604*0.795954*0.5 + -0.6511746*0.676406 + 0.1855740*0.186626*0.5 = -0.22023*X
 * 0.7071068*0.676406 + 0.6554516*0.795954*0.5 = 0.73915*Y
 * -> Y = j(0.04843*W + -0.22023*X) + 0.73915*Y
 *
 * * Result w/o 0.5 factor:
 * -0.3420201*0.795954 + 0.1432*0.676406 + 0.9396926*0.186626 = 0.00000*W
 * 0.5098604*0.795954 + -0.6511746*0.676406 + 0.1855740*0.186626 = 0.00000*X
 * 0.7071068*0.676406 + 0.6554516*0.795954 = 1.00000*Y
 * -> Y = j(0.00000*W + 0.00000*X) + 1.00000*Y
 *
 * Not halving produces a result matching the original input.
 */
void UhjDecoder::decode(const float *RESTRICT InSamples,
    const al::span<FloatBufferLine> OutSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    const size_t Channels{OutSamples.size()};

    float *woutput{OutSamples[0].data()};
    float *xoutput{OutSamples[1].data()};
    float *youtput{OutSamples[2].data()};

    /* Add a delay to the input channels, to align it with the all-passed
     * signal.
     */

    /* S = Left + Right */
    for(size_t i{0};i < SamplesToDo;++i)
        mS[sFilterSize+i] = InSamples[i*Channels + 0] + InSamples[i*Channels + 1];

    /* D = Left - Right */
    for(size_t i{0};i < SamplesToDo;++i)
        mD[sFilterSize+i] = InSamples[i*Channels + 0] - InSamples[i*Channels + 1];

    /* T */
    for(size_t i{0};i < SamplesToDo;++i)
        mT[sFilterSize+i] = InSamples[i*Channels + 2];

    if(Channels > 3)
    {
        /* Q */
        for(size_t i{0};i < SamplesToDo;++i)
            mQ[sFilterSize+i] = InSamples[i*Channels + 3];
    }

    /* Precompute j(0.828347*D + 0.767835*T) and store in xoutput. */
    auto tmpiter = std::copy(mDTHistory.cbegin(), mDTHistory.cend(), mTemp.begin());
    std::transform(mD.cbegin(), mD.cbegin()+SamplesToDo+sFilterSize, mT.cbegin(), tmpiter,
        [](const float D, const float T) noexcept { return 0.828347f*D + 0.767835f*T; });
    std::copy_n(mTemp.cbegin()+SamplesToDo, mDTHistory.size(), mDTHistory.begin());
    allpass_process({xoutput, SamplesToDo}, mTemp.data());

    for(size_t i{0};i < SamplesToDo;++i)
    {
        /* W = 0.981530*S + 0.197484*j(0.828347*D + 0.767835*T) */
        woutput[i] = 0.981530f*mS[i] + 0.197484f*xoutput[i];
        /* X = 0.418504*S - j(0.828347*D + 0.767835*T) */
        xoutput[i] = 0.418504f*mS[i] - xoutput[i];
    }

    /* Precompute j*S and store in youtput. */
    tmpiter = std::copy(mSHistory.cbegin(), mSHistory.cend(), mTemp.begin());
    std::copy_n(mS.cbegin(), SamplesToDo+sFilterSize, tmpiter);
    std::copy_n(mTemp.cbegin()+SamplesToDo, mSHistory.size(), mSHistory.begin());
    allpass_process({youtput, SamplesToDo}, mTemp.data());

    for(size_t i{0};i < SamplesToDo;++i)
    {
        /* Y = 0.795954*D - 0.676406*T + j(0.186626*S) */
        youtput[i] = 0.795954f*mD[i] - 0.676406f*mT[i] + 0.186626f*youtput[i];
    }

    if(Channels > 3)
    {
        float *zoutput{OutSamples[3].data()};
        /* Z = 1.023332*Q */
        for(size_t i{0};i < SamplesToDo;++i)
            zoutput[i] = 1.023332f*mQ[i];
    }

    std::copy(mS.begin()+SamplesToDo, mS.begin()+SamplesToDo+sFilterSize, mS.begin());
    std::copy(mD.begin()+SamplesToDo, mD.begin()+SamplesToDo+sFilterSize, mD.begin());
    std::copy(mT.begin()+SamplesToDo, mT.begin()+SamplesToDo+sFilterSize, mT.begin());
    std::copy(mQ.begin()+SamplesToDo, mQ.begin()+SamplesToDo+sFilterSize, mQ.begin());
}

/* There is a difference with decoding 2-channel UHJ compared to 3-channel, due
 * to 2-channel having lost some of the original signal. The B-Format signal
 * reconstructed from 2-channel UHJ should not be run through a normal B-Format
 * decoder, as it needs different shelf filters.
 *
 * 2-channel UHJ decoding is done as:
 *
 * S = Left + Right
 * D = Left - Right
 *
 * W = 0.981530*S + j*0.163585*D
 * X = 0.418504*S - j*0.828347*D
 * Y = 0.762956*D + j*0.384230*S
 *
 * where j is a +90 degree phase shift.
 *
 * NOTE: As above, S and D should not be halved. The only consequence of
 * halving here is merely a -6dB reduction in output, but it's still incorrect.
 */
void UhjDecoder::decode2(const float *RESTRICT InSamples,
    const al::span<FloatBufferLine,3> OutSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    float *woutput{OutSamples[0].data()};
    float *xoutput{OutSamples[1].data()};
    float *youtput{OutSamples[2].data()};

    /* S = Left + Right */
    for(size_t i{0};i < SamplesToDo;++i)
        mS[sFilterSize+i] = InSamples[i*2 + 0] + InSamples[i*2 + 1];

    /* D = Left - Right */
    for(size_t i{0};i < SamplesToDo;++i)
        mD[sFilterSize+i] = InSamples[i*2 + 0] - InSamples[i*2 + 1];

    /* Precompute j*D and store in xoutput. */
    auto tmpiter = std::copy(mDTHistory.cbegin(), mDTHistory.cend(), mTemp.begin());
    std::copy_n(mD.cbegin(), SamplesToDo+sFilterSize, tmpiter);
    std::copy_n(mTemp.cbegin()+SamplesToDo, mDTHistory.size(), mDTHistory.begin());
    allpass_process({xoutput, SamplesToDo}, mTemp.data());

    for(size_t i{0};i < SamplesToDo;++i)
    {
        /* W = 0.981530*S + j*0.163585*D */
        woutput[i] = 0.981530f*mS[i] + 0.163585f*xoutput[i];
        /* X = 0.418504*S - j*0.828347*D */
        xoutput[i] = 0.418504f*mS[i] - 0.828347f*xoutput[i];
    }

    /* Precompute j*S and store in youtput. */
    tmpiter = std::copy(mSHistory.cbegin(), mSHistory.cend(), mTemp.begin());
    std::copy_n(mS.cbegin(), SamplesToDo+sFilterSize, tmpiter);
    std::copy_n(mTemp.cbegin()+SamplesToDo, mSHistory.size(), mSHistory.begin());
    allpass_process({youtput, SamplesToDo}, mTemp.data());

    for(size_t i{0};i < SamplesToDo;++i)
    {
        /* Y = 0.762956*D + j*0.384230*S */
        youtput[i] = 0.762956f*mD[i] + 0.384230f*youtput[i];
    }

    std::copy(mS.begin()+SamplesToDo, mS.begin()+SamplesToDo+sFilterSize, mS.begin());
    std::copy(mD.begin()+SamplesToDo, mD.begin()+SamplesToDo+sFilterSize, mD.begin());
}


int main(int argc, char **argv)
{
    if(argc < 2 || std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)
    {
        printf("Usage: %s <filename.wav...>\n", argv[0]);
        return 1;
    }

    size_t num_files{0}, num_decoded{0};
    for(int fidx{1};fidx < argc;++fidx)
    {
        ++num_files;
        SF_INFO ininfo{};
        SndFilePtr infile{sf_open(argv[fidx], SFM_READ, &ininfo)};
        if(!infile)
        {
            fprintf(stderr, "Failed to open %s\n", argv[fidx]);
            continue;
        }
        if(sf_command(infile.get(), SFC_WAVEX_GET_AMBISONIC, NULL, 0) == SF_AMBISONIC_B_FORMAT)
        {
            fprintf(stderr, "%s is already B-Format\n", argv[fidx]);
            continue;
        }
        uint outchans{};
        if(ininfo.channels == 2)
            outchans = 3;
        else if(ininfo.channels == 3 || ininfo.channels == 4)
            outchans = static_cast<uint>(ininfo.channels);
        else
        {
            fprintf(stderr, "%s is not a 2-, 3-, or 4-channel file\n", argv[fidx]);
            continue;
        }
        printf("Converting %s from %d-channel UHJ...\n", argv[fidx], ininfo.channels);

        std::string outname{argv[fidx]};
        auto lastslash = outname.find_last_of('/');
        if(lastslash != std::string::npos)
            outname.erase(0, lastslash+1);
        auto lastdot = outname.find_last_of('.');
        if(lastdot != std::string::npos)
            outname.resize(lastdot+1);
        outname += "amb";

        FilePtr outfile{fopen(outname.c_str(), "wb")};
        if(!outfile)
        {
            fprintf(stderr, "Failed to create %s\n", outname.c_str());
            continue;
        }

        fputs("RIFF", outfile.get());
        fwrite32le(0xFFFFFFFF, outfile.get()); // 'RIFF' header len; filled in at close

        fputs("WAVE", outfile.get());

        fputs("fmt ", outfile.get());
        fwrite32le(40, outfile.get()); // 'fmt ' header len; 40 bytes for EXTENSIBLE

        // 16-bit val, format type id (extensible: 0xFFFE)
        fwrite16le(0xFFFE, outfile.get());
        // 16-bit val, channel count
        fwrite16le(static_cast<ushort>(outchans), outfile.get());
        // 32-bit val, frequency
        fwrite32le(static_cast<uint>(ininfo.samplerate), outfile.get());
        // 32-bit val, bytes per second
        fwrite32le(static_cast<uint>(ininfo.samplerate)*sizeof(float)*outchans, outfile.get());
        // 16-bit val, frame size
        fwrite16le(static_cast<ushort>(sizeof(float)*outchans), outfile.get());
        // 16-bit val, bits per sample
        fwrite16le(static_cast<ushort>(sizeof(float)*8), outfile.get());
        // 16-bit val, extra byte count
        fwrite16le(22, outfile.get());
        // 16-bit val, valid bits per sample
        fwrite16le(static_cast<ushort>(sizeof(float)*8), outfile.get());
        // 32-bit val, channel mask
        fwrite32le(0, outfile.get());
        // 16 byte GUID, sub-type format
        fwrite(SUBTYPE_BFORMAT_FLOAT, 1, 16, outfile.get());

        fputs("data", outfile.get());
        fwrite32le(0xFFFFFFFF, outfile.get()); // 'data' header len; filled in at close
        if(ferror(outfile.get()))
        {
            fprintf(stderr, "Error writing wave file header: %s (%d)\n", strerror(errno), errno);
            continue;
        }

        auto DataStart = ftell(outfile.get());

        auto decoder = std::make_unique<UhjDecoder>();
        auto inmem = std::make_unique<float[]>(BufferLineSize*static_cast<uint>(ininfo.channels));
        auto decmem = al::vector<std::array<float,BufferLineSize>, 16>(outchans);
        auto outmem = std::make_unique<byte4[]>(BufferLineSize*outchans);

        /* The all-pass filter has a lead-in of 127 samples, and a lead-out of
         * 128 samples. So after reading the last samples from the input, an
         * additional 255 samples of silence need to be fed through the decoder
         * for it to finish.
         */
        sf_count_t LeadOut{UhjDecoder::sFilterSize*2 - 1};
        while(LeadOut > 0)
        {
            sf_count_t sgot{sf_readf_float(infile.get(), inmem.get(), BufferLineSize)};
            sgot = std::max<sf_count_t>(sgot, 0);
            if(sgot < BufferLineSize)
            {
                const sf_count_t remaining{std::min(BufferLineSize - sgot, LeadOut)};
                std::fill_n(inmem.get() + sgot*ininfo.channels, remaining*ininfo.channels, 0.0f);
                sgot += remaining;
                LeadOut -= remaining;
            }

            auto got = static_cast<size_t>(sgot);
            if(ininfo.channels == 2)
                decoder->decode2(inmem.get(), decmem, got);
            else if(ininfo.channels == 3 || ininfo.channels == 4)
                decoder->decode(inmem.get(), decmem, got);
            for(size_t i{0};i < got;++i)
            {
                for(size_t j{0};j < outchans;++j)
                    outmem[i*outchans + j] = f32AsLEBytes(decmem[j][i]);
            }

            size_t wrote{fwrite(outmem.get(), sizeof(byte4)*outchans, got, outfile.get())};
            if(wrote < got)
            {
                fprintf(stderr, "Error writing wave data: %s (%d)\n", strerror(errno), errno);
                break;
            }
        }

        auto DataEnd = ftell(outfile.get());
        if(DataEnd > DataStart)
        {
            long dataLen{DataEnd - DataStart};
            if(fseek(outfile.get(), 4, SEEK_SET) == 0)
                fwrite32le(static_cast<uint>(DataEnd-8), outfile.get()); // 'WAVE' header len
            if(fseek(outfile.get(), DataStart-4, SEEK_SET) == 0)
                fwrite32le(static_cast<uint>(dataLen), outfile.get()); // 'data' header len
        }
        fflush(outfile.get());
        ++num_decoded;
    }
    if(num_decoded == 0)
        fprintf(stderr, "Failed to decode any input files\n");
    else if(num_decoded < num_files)
        fprintf(stderr, "Decoded %zu of %zu files\n", num_decoded, num_files);
    else
        printf("Decoded %zu file%s\n", num_decoded, (num_decoded==1)?"":"s");
    return 0;
}
