
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
#include "phase_shifter.h"


namespace {

using complex_d = std::complex<double>;

const PhaseShifterT<Uhj2Encoder::sFilterDelay*2> PShift{};

} // namespace


/* Encoding UHJ from B-Format is done as:
 *
 * S = 0.9396926*W + 0.1855740*X
 * D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y
 *
 * Left = (S + D)/2.0
 * Right = (S - D)/2.0
 * T = j(-0.1432*W + 0.6511746*X) - 0.7071068*Y
 * Q = 0.9772*Z
 *
 * where j is a wide-band +90 degree phase shift. T is excluded from 2-channel
 * output, and Q is excluded from 2- and 3-channel output.
 *
 * The phase shift is done using a FIR filter derived from an FFT'd impulse
 * with the desired shift.
 */

void Uhj2Encoder::encode(const FloatBufferSpan LeftOut, const FloatBufferSpan RightOut,
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
    auto miditer = mMid.begin() + sFilterDelay;
    std::transform(winput, winput+SamplesToDo, xinput, miditer,
        [](const float w, const float x) noexcept -> float
        { return 0.9396926f*w + 0.1855740f*x; });

    /* D = 0.6554516*Y */
    auto sideiter = mSide.begin() + sFilterDelay;
    std::transform(yinput, yinput+SamplesToDo, sideiter,
        [](const float y) noexcept -> float { return 0.6554516f*y; });

    /* Include any existing direct signal in the mid/side buffers. */
    for(size_t i{0};i < SamplesToDo;++i,++miditer)
        *miditer += left[i] + right[i];
    for(size_t i{0};i < SamplesToDo;++i,++sideiter)
        *sideiter += left[i] - right[i];

    /* Now add the all-passed signal into the side signal. */

    /* D += j(-0.3420201*W + 0.5098604*X) */
    auto tmpiter = std::copy(mSideHistory.cbegin(), mSideHistory.cend(), mTemp.begin());
    std::transform(winput, winput+SamplesToDo, xinput, tmpiter,
        [](const float w, const float x) noexcept -> float
        { return -0.3420201f*w + 0.5098604f*x; });
    std::copy_n(mTemp.cbegin()+SamplesToDo, mSideHistory.size(), mSideHistory.begin());
    PShift.processAccum({mSide.data(), SamplesToDo}, mTemp.data());

    /* Left = (S + D)/2.0 */
    for(size_t i{0};i < SamplesToDo;i++)
        left[i] = (mMid[i] + mSide[i]) * 0.5f;
    /* Right = (S - D)/2.0 */
    for(size_t i{0};i < SamplesToDo;i++)
        right[i] = (mMid[i] - mSide[i]) * 0.5f;

    /* Copy the future samples to the front for next time. */
    std::copy(mMid.cbegin()+SamplesToDo, mMid.cbegin()+SamplesToDo+sFilterDelay, mMid.begin());
    std::copy(mSide.cbegin()+SamplesToDo, mSide.cbegin()+SamplesToDo+sFilterDelay, mSide.begin());
}
