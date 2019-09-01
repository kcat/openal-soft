
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


constexpr ALfloat Filter1CoeffSqr[4] = {
    0.479400865589f, 0.876218493539f, 0.976597589508f, 0.997499255936f
};
constexpr ALfloat Filter2CoeffSqr[4] = {
    0.161758498368f, 0.733028932341f, 0.945349700329f, 0.990599156685f
};

void allpass_process(AllPassState *state, ALfloat *dst, const ALfloat *src, const ALfloat aa,
    const size_t todo)
{
    ALfloat z1{state->z[0]};
    ALfloat z2{state->z[1]};
    auto proc_sample = [aa,&z1,&z2](const ALfloat input) noexcept -> ALfloat
    {
        const ALfloat output{input*aa + z1};
        z1 = z2; z2 = output*aa - input;
        return output;
    };
    std::transform(src, src+todo, dst, proc_sample);
    state->z[0] = z1;
    state->z[1] = z2;
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
    alignas(16) ALfloat D[MAX_UPDATE_SAMPLES], S[MAX_UPDATE_SAMPLES];
    alignas(16) ALfloat temp[MAX_UPDATE_SAMPLES];

    ASSUME(SamplesToDo > 0);

    auto winput = InSamples[0].cbegin();
    auto xinput = InSamples[1].cbegin();
    auto yinput = InSamples[2].cbegin();
    for(size_t base{0};base < SamplesToDo;)
    {
        const size_t todo{minz(SamplesToDo - base, MAX_UPDATE_SAMPLES)};
        ASSUME(todo > 0);

        /* D = 0.6554516*Y */
        std::transform(yinput, yinput+todo, std::begin(temp),
            [](const float y) noexcept -> float { return 0.6554516f*y; });
        allpass_process(&mFilter1_Y[0], temp, temp, Filter1CoeffSqr[0], todo);
        allpass_process(&mFilter1_Y[1], temp, temp, Filter1CoeffSqr[1], todo);
        allpass_process(&mFilter1_Y[2], temp, temp, Filter1CoeffSqr[2], todo);
        allpass_process(&mFilter1_Y[3], temp, temp, Filter1CoeffSqr[3], todo);
        /* NOTE: Filter1 requires a 1 sample delay for the final output, so
         * take the last processed sample from the previous run as the first
         * output sample.
         */
        D[0] = mLastY;
        for(size_t i{1};i < todo;i++)
            D[i] = temp[i-1];
        mLastY = temp[todo-1];

        /* D += j(-0.3420201*W + 0.5098604*X) */
        std::transform(winput, winput+todo, xinput, std::begin(temp),
            [](const float w, const float x) noexcept -> float
            { return -0.3420201f*w + 0.5098604f*x; });
        allpass_process(&mFilter2_WX[0], temp, temp, Filter2CoeffSqr[0], todo);
        allpass_process(&mFilter2_WX[1], temp, temp, Filter2CoeffSqr[1], todo);
        allpass_process(&mFilter2_WX[2], temp, temp, Filter2CoeffSqr[2], todo);
        allpass_process(&mFilter2_WX[3], temp, temp, Filter2CoeffSqr[3], todo);
        for(size_t i{0};i < todo;i++)
            D[i] += temp[i];

        /* S = 0.9396926*W + 0.1855740*X */
        std::transform(winput, winput+todo, xinput, std::begin(temp),
            [](const float w, const float x) noexcept -> float
            { return 0.9396926f*w + 0.1855740f*x; });
        allpass_process(&mFilter1_WX[0], temp, temp, Filter1CoeffSqr[0], todo);
        allpass_process(&mFilter1_WX[1], temp, temp, Filter1CoeffSqr[1], todo);
        allpass_process(&mFilter1_WX[2], temp, temp, Filter1CoeffSqr[2], todo);
        allpass_process(&mFilter1_WX[3], temp, temp, Filter1CoeffSqr[3], todo);
        S[0] = mLastWX;
        for(size_t i{1};i < todo;i++)
            S[i] = temp[i-1];
        mLastWX = temp[todo-1];

        /* Left = (S + D)/2.0 */
        ALfloat *RESTRICT left = al::assume_aligned<16>(LeftOut.data()+base);
        for(size_t i{0};i < todo;i++)
            left[i] += (S[i] + D[i]) * 0.5f;
        /* Right = (S - D)/2.0 */
        ALfloat *RESTRICT right = al::assume_aligned<16>(RightOut.data()+base);
        for(size_t i{0};i < todo;i++)
            right[i] += (S[i] - D[i]) * 0.5f;

        winput += todo;
        xinput += todo;
        yinput += todo;
        base += todo;
    }
}
