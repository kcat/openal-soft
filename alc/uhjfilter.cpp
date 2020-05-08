
#include "config.h"

#include "uhjfilter.h"

#include <algorithm>
#include <iterator>

#include "AL/al.h"

#include "alnumeric.h"
#include "opthelpers.h"


namespace {

/* This is the maximum number of samples processed for each inner loop
 * iteration. */
#define MAX_UPDATE_SAMPLES  128


constexpr float Filter1CoeffSqr[4]{
    0.479400865589f, 0.876218493539f, 0.976597589508f, 0.997499255936f
};
constexpr float Filter2CoeffSqr[4]{
    0.161758498368f, 0.733028932341f, 0.945349700329f, 0.990599156685f
};

void allpass_process(AllPassState *state, float *dst, const float *src, const float *coeffs,
    const size_t todo)
{
    const float aa0{coeffs[0]};
    const float aa1{coeffs[1]};
    const float aa2{coeffs[2]};
    const float aa3{coeffs[3]};
    float z01{state[0].z[0]};
    float z02{state[0].z[1]};
    float z11{state[1].z[0]};
    float z12{state[1].z[1]};
    float z21{state[2].z[0]};
    float z22{state[2].z[1]};
    float z31{state[3].z[0]};
    float z32{state[3].z[1]};
    auto proc_sample = [aa0,aa1,aa2,aa3,&z01,&z02,&z11,&z12,&z21,&z22,&z31,&z32](
        float input) noexcept -> float
    {
        float output{input*aa0 + z01};
        z01 = z02; z02 = output*aa0 - input;
        input = output;

        output = input*aa1 + z11;
        z11 = z12; z12 = output*aa1 - input;
        input = output;

        output = input*aa2 + z21;
        z21 = z22; z22 = output*aa2 - input;
        input = output;

        output = input*aa3 + z31;
        z31 = z32; z32 = output*aa3 - input;
        return output;
    };
    std::transform(src, src+todo, dst, proc_sample);
    state[0].z[0] = z01;
    state[0].z[1] = z02;
    state[1].z[0] = z11;
    state[1].z[1] = z12;
    state[2].z[0] = z21;
    state[2].z[1] = z22;
    state[3].z[0] = z31;
    state[3].z[1] = z32;
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
    FloatBufferLine *InSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    const auto winput = al::assume_aligned<16>(InSamples[0].cbegin());
    const auto xinput = al::assume_aligned<16>(InSamples[1].cbegin());
    const auto yinput = al::assume_aligned<16>(InSamples[2].cbegin());

    /* D = 0.6554516*Y */
    std::transform(yinput, yinput+SamplesToDo, mTemp.begin(),
        [](const float y) noexcept -> float { return 0.6554516f*y; });
    /* NOTE: Filter1 requires a 1 sample delay for the final output, so take
     * the last processed sample from the previous run as the first output
     * sample.
     */
    mSide[0] = mLastY;
    allpass_process(mFilter1_Y, mSide.data()+1, mTemp.data(), Filter1CoeffSqr, SamplesToDo);
    mLastY = mSide[SamplesToDo];

    /* D += j(-0.3420201*W + 0.5098604*X) */
    std::transform(winput, winput+SamplesToDo, xinput, mTemp.begin(),
        [](const float w, const float x) noexcept -> float
        { return -0.3420201f*w + 0.5098604f*x; });
    allpass_process(mFilter2_WX, mTemp.data(), mTemp.data(), Filter2CoeffSqr, SamplesToDo);
    for(size_t i{0};i < SamplesToDo;++i)
        mSide[i] += mTemp[i];

    /* S = 0.9396926*W + 0.1855740*X */
    std::transform(winput, winput+SamplesToDo, xinput, mTemp.begin(),
        [](const float w, const float x) noexcept -> float
        { return 0.9396926f*w + 0.1855740f*x; });
    mMid[0] = mLastWX;
    allpass_process(mFilter1_WX, mMid.data()+1, mTemp.data(), Filter1CoeffSqr, SamplesToDo);
    mLastWX = mMid[SamplesToDo];

    /* Left = (S + D)/2.0 */
    float *RESTRICT left{al::assume_aligned<16>(LeftOut.data())};
    for(size_t i{0};i < SamplesToDo;i++)
        left[i] += (mMid[i] + mSide[i]) * 0.5f;
    /* Right = (S - D)/2.0 */
    float *RESTRICT right{al::assume_aligned<16>(RightOut.data())};
    for(size_t i{0};i < SamplesToDo;i++)
        right[i] += (mMid[i] - mSide[i]) * 0.5f;
}
