
#include "config.h"

#include "uhjfilter.h"

#include <algorithm>
#include <iterator>

#include "alcomplex.h"
#include "alnumeric.h"
#include "opthelpers.h"
#include "phase_shifter.h"


UhjQualityType UhjDecodeQuality{UhjQualityType::Default};
UhjQualityType UhjEncodeQuality{UhjQualityType::Default};


namespace {

const PhaseShifterT<UhjLength256> PShiftLq{};
const PhaseShifterT<UhjLength512> PShiftHq{};

template<size_t N>
struct GetPhaseShifter;
template<>
struct GetPhaseShifter<UhjLength256> { static auto& Get() noexcept { return PShiftLq; } };
template<>
struct GetPhaseShifter<UhjLength512> { static auto& Get() noexcept { return PShiftHq; } };


constexpr float square(float x) noexcept
{ return x*x; }

/* Filter coefficients for the 'base' all-pass IIR, which applies a frequency-
 * dependent phase-shift of N degrees. The output of the filter requires a 1-
 * sample delay.
 */
constexpr std::array<float,4> Filter1Coeff{{
    square(0.6923878f), square(0.9360654322959f), square(0.9882295226860f),
    square(0.9987488452737f)
}};
/* Filter coefficients for the shifted all-pass IIR, which applies a frequency-
 * dependent phase-shift of N+90 degrees.
 */
constexpr std::array<float,4> Filter2Coeff{{
    square(0.4021921162426f), square(0.8561710882420f), square(0.9722909545651f),
    square(0.9952884791278f)
}};

/* This applies the base all-pass filter in reverse, resulting in a phase-shift
 * of -N degrees. Extra samples are provided at the back of the input to reduce
 * the amount of error at the back of the output.
 */
void allpass1_process_rev(const al::span<const float> src, float *RESTRICT dst)
{
    float z[4][2]{};

    auto proc_sample = [&z](float x) noexcept -> float
    {
        for(size_t i{0};i < 4;++i)
        {
            const float y{x*Filter1Coeff[i] + z[i][0]};
            z[i][0] = z[i][1];
            z[i][1] = y*Filter1Coeff[i] - x;
            x = y;
        }
        return x;
    };
    std::transform(src.rbegin(), src.rend(), std::make_reverse_iterator(dst+src.size()),
        proc_sample);
}

/* This applies the shifted all-pass filter to the output of the base filter,
 * resulting in a phase shift of -N + N + 90 degrees, or just 90 degrees.
 */
void allpass2_process(const al::span<UhjAllPassState,4> state, const al::span<const float> src,
    const size_t forwardSamples, float *RESTRICT dst)
{
    float z[4][2]{{state[0].z[0], state[0].z[1]}, {state[1].z[0], state[1].z[1]},
        {state[2].z[0], state[2].z[1]}, {state[3].z[0], state[3].z[1]}};

    auto proc_sample = [&z](float x) noexcept -> float
    {
        for(size_t i{0};i < 4;++i)
        {
            const float y{x*Filter2Coeff[i] + z[i][0]};
            z[i][0] = z[i][1];
            z[i][1] = y*Filter2Coeff[i] - x;
            x = y;
        }
        return x;
    };
    auto dstiter = std::transform(src.begin(), src.begin()+forwardSamples, dst, proc_sample);
    for(size_t i{0};i < 4;++i)
    {
        state[i].z[0] = z[i][0];
        state[i].z[1] = z[i][1];
    }

    std::transform(src.begin()+forwardSamples, src.end(), dstiter, proc_sample);
}

} // namespace


/* Encoding UHJ from B-Format is done as:
 *
 * S = 0.9396926*W + 0.1855740*X
 * D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y
 *
 * Left = (S + D)/2.0
 * Right = (S - D)/2.0
 * T = j(-0.1432*W + 0.6512*X) - 0.7071068*Y
 * Q = 0.9772*Z
 *
 * where j is a wide-band +90 degree phase shift. 3-channel UHJ excludes Q,
 * while 2-channel excludes Q and T.
 *
 * The phase shift is done using a linear FIR filter derived from an FFT'd
 * impulse with the desired shift.
 */

template<size_t N>
void UhjEncoder<N>::encode(float *LeftOut, float *RightOut,
    const al::span<const float*const,3> InSamples, const size_t SamplesToDo)
{
    const auto &PShift = GetPhaseShifter<N>::Get();

    ASSUME(SamplesToDo > 0);

    const float *RESTRICT winput{al::assume_aligned<16>(InSamples[0])};
    const float *RESTRICT xinput{al::assume_aligned<16>(InSamples[1])};
    const float *RESTRICT yinput{al::assume_aligned<16>(InSamples[2])};

    std::copy_n(winput, SamplesToDo, mW.begin()+sFilterDelay);
    std::copy_n(xinput, SamplesToDo, mX.begin()+sFilterDelay);
    std::copy_n(yinput, SamplesToDo, mY.begin()+sFilterDelay);

    /* S = 0.9396926*W + 0.1855740*X */
    for(size_t i{0};i < SamplesToDo;++i)
        mS[i] = 0.9396926f*mW[i] + 0.1855740f*mX[i];

    /* Precompute j(-0.3420201*W + 0.5098604*X) and store in mD. */
    std::transform(winput, winput+SamplesToDo, xinput, mWX.begin() + sWXInOffset,
        [](const float w, const float x) noexcept -> float
        { return -0.3420201f*w + 0.5098604f*x; });
    PShift.process({mD.data(), SamplesToDo}, mWX.data());

    /* D = 0.6554516*Y + j(-0.3420201*W + 0.5098604*X) */
    for(size_t i{0};i < SamplesToDo;++i)
        mD[i] = 0.6554516f*mY[i] + mD[i];

    /* Copy the future samples to the front for next time. */
    std::copy(mW.cbegin()+SamplesToDo, mW.cbegin()+SamplesToDo+sFilterDelay, mW.begin());
    std::copy(mX.cbegin()+SamplesToDo, mX.cbegin()+SamplesToDo+sFilterDelay, mX.begin());
    std::copy(mY.cbegin()+SamplesToDo, mY.cbegin()+SamplesToDo+sFilterDelay, mY.begin());
    std::copy(mWX.cbegin()+SamplesToDo, mWX.cbegin()+SamplesToDo+sWXInOffset, mWX.begin());

    /* Apply a delay to the existing output to align with the input delay. */
    auto *delayBuffer = mDirectDelay.data();
    for(float *buffer : {LeftOut, RightOut})
    {
        float *distbuf{al::assume_aligned<16>(delayBuffer->data())};
        ++delayBuffer;

        float *inout{al::assume_aligned<16>(buffer)};
        auto inout_end = inout + SamplesToDo;
        if(likely(SamplesToDo >= sFilterDelay))
        {
            auto delay_end = std::rotate(inout, inout_end - sFilterDelay, inout_end);
            std::swap_ranges(inout, delay_end, distbuf);
        }
        else
        {
            auto delay_start = std::swap_ranges(inout, inout_end, distbuf);
            std::rotate(distbuf, delay_start, distbuf + sFilterDelay);
        }
    }

    /* Combine the direct signal with the produced output. */

    /* Left = (S + D)/2.0 */
    float *RESTRICT left{al::assume_aligned<16>(LeftOut)};
    for(size_t i{0};i < SamplesToDo;i++)
        left[i] += (mS[i] + mD[i]) * 0.5f;
    /* Right = (S - D)/2.0 */
    float *RESTRICT right{al::assume_aligned<16>(RightOut)};
    for(size_t i{0};i < SamplesToDo;i++)
        right[i] += (mS[i] - mD[i]) * 0.5f;
}

void UhjEncoderIIR::encode(float *LeftOut, float *RightOut,
    const al::span<const float *const, 3> InSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    const float *RESTRICT winput{al::assume_aligned<16>(InSamples[0])};
    const float *RESTRICT xinput{al::assume_aligned<16>(InSamples[1])};
    const float *RESTRICT yinput{al::assume_aligned<16>(InSamples[2])};

    std::copy_n(winput, SamplesToDo, mW.begin()+sFilterDelay);
    std::copy_n(xinput, SamplesToDo, mX.begin()+sFilterDelay);
    std::copy_n(yinput, SamplesToDo, mY.begin()+sFilterDelay);

    /* S = 0.9396926*W + 0.1855740*X */
    for(size_t i{0};i < SamplesToDo;++i)
        mS[i] = 0.9396926f*mW[i] + 0.1855740f*mX[i];

    /* Precompute j(-0.3420201*W + 0.5098604*X) and store in mD. */
    std::transform(winput, winput+SamplesToDo, mX.cbegin(), mWX.begin()+sFilterDelay,
        [](const float w, const float x) noexcept { return -0.3420201f*w + 0.5098604f*x; });
    allpass1_process_rev({mWX.data()+1, SamplesToDo+sFilterDelay-1}, mRevTemp.data());
    allpass2_process(mFilterWX, {mRevTemp.data(), SamplesToDo}, SamplesToDo, mD.data());

    /* D = 0.6554516*Y + j(-0.3420201*W + 0.5098604*X) */
    for(size_t i{0};i < SamplesToDo;++i)
        mD[i] = 0.6554516f*mY[i] + mD[i];

    /* Copy the future samples to the front for next time. */
    std::copy(mW.cbegin()+SamplesToDo, mW.cbegin()+SamplesToDo+sFilterDelay, mW.begin());
    std::copy(mX.cbegin()+SamplesToDo, mX.cbegin()+SamplesToDo+sFilterDelay, mX.begin());
    std::copy(mY.cbegin()+SamplesToDo, mY.cbegin()+SamplesToDo+sFilterDelay, mY.begin());
    std::copy(mWX.cbegin()+SamplesToDo, mWX.cbegin()+SamplesToDo+sFilterDelay, mWX.begin());

    /* Apply a delay to the existing output to align with the input delay. */
    auto *delayBuffer = mDirectDelay.data();
    for(float *buffer : {LeftOut, RightOut})
    {
        float *RESTRICT distbuf{al::assume_aligned<16>(delayBuffer->data())};
        ++delayBuffer;

        float *inout{al::assume_aligned<16>(buffer)};
        auto inout_end = inout + SamplesToDo;
        if(likely(SamplesToDo >= sFilterDelay))
        {
            auto delay_end = std::rotate(inout, inout_end - sFilterDelay, inout_end);
            std::swap_ranges(inout, delay_end, distbuf);
        }
        else
        {
            auto delay_start = std::swap_ranges(inout, inout_end, distbuf);
            std::rotate(distbuf, delay_start, distbuf + sFilterDelay);
        }
    }

    /* Combine the direct signal with the produced output. */

    /* Left = (S + D)/2.0 */
    float *RESTRICT left{al::assume_aligned<16>(LeftOut)};
    for(size_t i{0};i < SamplesToDo;i++)
        left[i] += (mS[i] + mD[i]) * 0.5f;
    /* Right = (S - D)/2.0 */
    float *RESTRICT right{al::assume_aligned<16>(RightOut)};
    for(size_t i{0};i < SamplesToDo;i++)
        right[i] += (mS[i] - mD[i]) * 0.5f;
}


/* Decoding UHJ is done as:
 *
 * S = Left + Right
 * D = Left - Right
 *
 * W = 0.981532*S + 0.197484*j(0.828331*D + 0.767820*T)
 * X = 0.418496*S - j(0.828331*D + 0.767820*T)
 * Y = 0.795968*D - 0.676392*T + j(0.186633*S)
 * Z = 1.023332*Q
 *
 * where j is a +90 degree phase shift. 3-channel UHJ excludes Q, while 2-
 * channel excludes Q and T.
 */
template<size_t N>
void UhjDecoder<N>::decode(const al::span<float*> samples, const size_t samplesToDo,
    const size_t forwardSamples)
{
    static_assert(sFilterDelay <= sMaxDelay, "Filter delay is too large");

    const auto &PShift = GetPhaseShifter<N>::Get();

    ASSUME(samplesToDo > 0);

    {
        const float *RESTRICT left{al::assume_aligned<16>(samples[0])};
        const float *RESTRICT right{al::assume_aligned<16>(samples[1])};
        const float *RESTRICT t{al::assume_aligned<16>(samples[2])};

        /* S = Left + Right */
        for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
            mS[i] = left[i] + right[i];

        /* D = Left - Right */
        for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
            mD[i] = left[i] - right[i];

        /* T */
        for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
            mT[i] = t[i];
    }

    float *RESTRICT woutput{al::assume_aligned<16>(samples[0])};
    float *RESTRICT xoutput{al::assume_aligned<16>(samples[1])};
    float *RESTRICT youtput{al::assume_aligned<16>(samples[2])};

    /* Precompute j(0.828331*D + 0.767820*T) and store in xoutput. */
    auto tmpiter = std::copy(mDTHistory.cbegin(), mDTHistory.cend(), mTemp.begin());
    std::transform(mD.cbegin(), mD.cbegin()+samplesToDo+sFilterDelay, mT.cbegin(), tmpiter,
        [](const float d, const float t) noexcept { return 0.828331f*d + 0.767820f*t; });
    std::copy_n(mTemp.cbegin()+forwardSamples, mDTHistory.size(), mDTHistory.begin());
    PShift.process({xoutput, samplesToDo}, mTemp.data());

    /* W = 0.981532*S + 0.197484*j(0.828331*D + 0.767820*T) */
    for(size_t i{0};i < samplesToDo;++i)
        woutput[i] = 0.981532f*mS[i] + 0.197484f*xoutput[i];
    /* X = 0.418496*S - j(0.828331*D + 0.767820*T) */
    for(size_t i{0};i < samplesToDo;++i)
        xoutput[i] = 0.418496f*mS[i] - xoutput[i];

    /* Precompute j*S and store in youtput. */
    tmpiter = std::copy(mSHistory.cbegin(), mSHistory.cend(), mTemp.begin());
    std::copy_n(mS.cbegin(), samplesToDo+sFilterDelay, tmpiter);
    std::copy_n(mTemp.cbegin()+forwardSamples, mSHistory.size(), mSHistory.begin());
    PShift.process({youtput, samplesToDo}, mTemp.data());

    /* Y = 0.795968*D - 0.676392*T + j(0.186633*S) */
    for(size_t i{0};i < samplesToDo;++i)
        youtput[i] = 0.795968f*mD[i] - 0.676392f*mT[i] + 0.186633f*youtput[i];

    if(samples.size() > 3)
    {
        float *RESTRICT zoutput{al::assume_aligned<16>(samples[3])};
        /* Z = 1.023332*Q */
        for(size_t i{0};i < samplesToDo;++i)
            zoutput[i] = 1.023332f*zoutput[i];
    }
}

void UhjDecoderIIR::decode(const al::span<float*> samples, const size_t samplesToDo,
    const size_t forwardSamples)
{
    static_assert(sFilterDelay <= sMaxDelay, "Filter delay is too large");

    ASSUME(samplesToDo > 0);

    {
        const float *RESTRICT left{al::assume_aligned<16>(samples[0])};
        const float *RESTRICT right{al::assume_aligned<16>(samples[1])};
        const float *RESTRICT t{al::assume_aligned<16>(samples[2])};

        /* S = Left + Right */
        for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
            mS[i] = left[i] + right[i];

        /* D = Left - Right */
        for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
            mD[i] = left[i] - right[i];

        /* T */
        for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
            mT[i] = t[i];
    }

    float *RESTRICT woutput{al::assume_aligned<16>(samples[0])};
    float *RESTRICT xoutput{al::assume_aligned<16>(samples[1])};
    float *RESTRICT youtput{al::assume_aligned<16>(samples[2])};

    /* Precompute j(0.828331*D + 0.767820*T) and store in xoutput. */
    std::transform(mD.cbegin(), mD.cbegin()+samplesToDo+sFilterDelay, mT.cbegin(), mTemp.begin(),
        [](const float d, const float t) noexcept { return 0.828331f*d + 0.767820f*t; });
    allpass1_process_rev({mTemp.data()+1, samplesToDo+sFilterDelay-1}, mRevTemp.data());
    allpass2_process(mFilterDT, {mRevTemp.data(), samplesToDo}, forwardSamples, xoutput);

    /* W = 0.981532*S + 0.197484*j(0.828331*D + 0.767820*T) */
    for(size_t i{0};i < samplesToDo;++i)
        woutput[i] = 0.981532f*mS[i] + 0.197484f*xoutput[i];
    /* X = 0.418496*S - j(0.828331*D + 0.767820*T) */
    for(size_t i{0};i < samplesToDo;++i)
        xoutput[i] = 0.418496f*mS[i] - xoutput[i];

    /* Precompute j*S and store in youtput. */
    allpass1_process_rev({mS.data()+1, samplesToDo+sFilterDelay-1}, mRevTemp.data());
    allpass2_process(mFilterS, {mRevTemp.data(), samplesToDo}, forwardSamples, youtput);

    /* Y = 0.795968*D - 0.676392*T + j(0.186633*S) */
    for(size_t i{0};i < samplesToDo;++i)
        youtput[i] = 0.795968f*mD[i] - 0.676392f*mT[i] + 0.186633f*youtput[i];

    if(samples.size() > 3)
    {
        float *RESTRICT zoutput{al::assume_aligned<16>(samples[3])};
        /* Z = 1.023332*Q */
        for(size_t i{0};i < samplesToDo;++i)
            zoutput[i] = 1.023332f*zoutput[i];
    }
}


/* Super Stereo processing is done as:
 *
 * S = Left + Right
 * D = Left - Right
 *
 * W = 0.6098637*S - 0.6896511*j*w*D
 * X = 0.8624776*S + 0.7626955*j*w*D
 * Y = 1.6822415*w*D - 0.2156194*j*S
 *
 * where j is a +90 degree phase shift. w is a variable control for the
 * resulting stereo width, with the range 0 <= w <= 0.7.
 */
template<size_t N>
void UhjStereoDecoder<N>::decode(const al::span<float*> samples, const size_t samplesToDo,
    const size_t forwardSamples)
{
    static_assert(sFilterDelay <= sMaxDelay, "Filter delay is too large");

    const auto &PShift = GetPhaseShifter<N>::Get();

    ASSUME(samplesToDo > 0);

    {
        const float *RESTRICT left{al::assume_aligned<16>(samples[0])};
        const float *RESTRICT right{al::assume_aligned<16>(samples[1])};

        for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
            mS[i] = left[i] + right[i];

        /* Pre-apply the width factor to the difference signal D. Smoothly
         * interpolate when it changes.
         */
        const float wtarget{mWidthControl};
        const float wcurrent{unlikely(mCurrentWidth < 0.0f) ? wtarget : mCurrentWidth};
        if(likely(wtarget == wcurrent) || unlikely(forwardSamples == 0))
        {
            for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
                mD[i] = (left[i] - right[i]) * wcurrent;
            mCurrentWidth = wcurrent;
        }
        else
        {
            const float wstep{(wtarget - wcurrent) / static_cast<float>(forwardSamples)};
            float fi{0.0f};
            size_t i{0};
            for(;i < forwardSamples;++i)
            {
                mD[i] = (left[i] - right[i]) * (wcurrent + wstep*fi);
                fi += 1.0f;
            }
            for(;i < samplesToDo+sFilterDelay;++i)
                mD[i] = (left[i] - right[i]) * wtarget;
            mCurrentWidth = wtarget;
        }
    }

    float *RESTRICT woutput{al::assume_aligned<16>(samples[0])};
    float *RESTRICT xoutput{al::assume_aligned<16>(samples[1])};
    float *RESTRICT youtput{al::assume_aligned<16>(samples[2])};

    /* Precompute j*D and store in xoutput. */
    auto tmpiter = std::copy(mDTHistory.cbegin(), mDTHistory.cend(), mTemp.begin());
    std::copy_n(mD.cbegin(), samplesToDo+sFilterDelay, tmpiter);
    std::copy_n(mTemp.cbegin()+forwardSamples, mDTHistory.size(), mDTHistory.begin());
    PShift.process({xoutput, samplesToDo}, mTemp.data());

    /* W = 0.6098637*S - 0.6896511*j*w*D */
    for(size_t i{0};i < samplesToDo;++i)
        woutput[i] = 0.6098637f*mS[i] - 0.6896511f*xoutput[i];
    /* X = 0.8624776*S + 0.7626955*j*w*D */
    for(size_t i{0};i < samplesToDo;++i)
        xoutput[i] = 0.8624776f*mS[i] + 0.7626955f*xoutput[i];

    /* Precompute j*S and store in youtput. */
    tmpiter = std::copy(mSHistory.cbegin(), mSHistory.cend(), mTemp.begin());
    std::copy_n(mS.cbegin(), samplesToDo+sFilterDelay, tmpiter);
    std::copy_n(mTemp.cbegin()+forwardSamples, mSHistory.size(), mSHistory.begin());
    PShift.process({youtput, samplesToDo}, mTemp.data());

    /* Y = 1.6822415*w*D - 0.2156194*j*S */
    for(size_t i{0};i < samplesToDo;++i)
        youtput[i] = 1.6822415f*mD[i] - 0.2156194f*youtput[i];
}

void UhjStereoDecoderIIR::decode(const al::span<float*> samples, const size_t samplesToDo,
    const size_t forwardSamples)
{
    static_assert(sFilterDelay <= sMaxDelay, "Filter delay is too large");

    ASSUME(samplesToDo > 0);

    {
        const float *RESTRICT left{al::assume_aligned<16>(samples[0])};
        const float *RESTRICT right{al::assume_aligned<16>(samples[1])};

        for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
            mS[i] = left[i] + right[i];

        /* Pre-apply the width factor to the difference signal D. Smoothly
         * interpolate when it changes.
         */
        const float wtarget{mWidthControl};
        const float wcurrent{unlikely(mCurrentWidth < 0.0f) ? wtarget : mCurrentWidth};
        if(likely(wtarget == wcurrent) || unlikely(forwardSamples == 0))
        {
            for(size_t i{0};i < samplesToDo+sFilterDelay;++i)
                mD[i] = (left[i] - right[i]) * wcurrent;
            mCurrentWidth = wcurrent;
        }
        else
        {
            const float wstep{(wtarget - wcurrent) / static_cast<float>(forwardSamples)};
            float fi{0.0f};
            size_t i{0};
            for(;i < forwardSamples;++i)
            {
                mD[i] = (left[i] - right[i]) * (wcurrent + wstep*fi);
                fi += 1.0f;
            }
            for(;i < samplesToDo+sFilterDelay;++i)
                mD[i] = (left[i] - right[i]) * wtarget;
            mCurrentWidth = wtarget;
        }
    }

    float *RESTRICT woutput{al::assume_aligned<16>(samples[0])};
    float *RESTRICT xoutput{al::assume_aligned<16>(samples[1])};
    float *RESTRICT youtput{al::assume_aligned<16>(samples[2])};

    /* Precompute j*D and store in xoutput. */
    allpass1_process_rev({mD.data()+1, samplesToDo+sFilterDelay-1}, mRevTemp.data());
    allpass2_process(mFilterD, {mRevTemp.data(), samplesToDo}, forwardSamples, xoutput);

    /* W = 0.6098637*S - 0.6896511*j*w*D */
    for(size_t i{0};i < samplesToDo;++i)
        woutput[i] = 0.6098637f*mS[i] - 0.6896511f*xoutput[i];
    /* X = 0.8624776*S + 0.7626955*j*w*D */
    for(size_t i{0};i < samplesToDo;++i)
        xoutput[i] = 0.8624776f*mS[i] + 0.7626955f*xoutput[i];

    /* Precompute j*S and store in youtput. */
    allpass1_process_rev({mS.data()+1, samplesToDo+sFilterDelay-1}, mRevTemp.data());
    allpass2_process(mFilterS, {mRevTemp.data(), samplesToDo}, forwardSamples, xoutput);

    /* Y = 1.6822415*w*D - 0.2156194*j*S */
    for(size_t i{0};i < samplesToDo;++i)
        youtput[i] = 1.6822415f*mD[i] - 0.2156194f*youtput[i];
}


template struct UhjEncoder<UhjLength256>;
template struct UhjDecoder<UhjLength256>;
template struct UhjStereoDecoder<UhjLength256>;

template struct UhjEncoder<UhjLength512>;
template struct UhjDecoder<UhjLength512>;
template struct UhjStereoDecoder<UhjLength512>;
