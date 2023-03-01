
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
/* Filter coefficients for the offset all-pass IIR, which applies a frequency-
 * dependent phase-shift of N+90 degrees.
 */
constexpr std::array<float,4> Filter2Coeff{{
    square(0.4021921162426f), square(0.8561710882420f), square(0.9722909545651f),
    square(0.9952884791278f)
}};

} // namespace

void UhjAllPassFilter::process(const al::span<const float,4> coeffs,
    const al::span<const float> src, const bool updateState, float *RESTRICT dst)
{
    auto state = mState;

    auto proc_sample = [&state,coeffs](float x) noexcept -> float
    {
        for(size_t i{0};i < 4;++i)
        {
            const float y{x*coeffs[i] + state[i].z[0]};
            state[i].z[0] = state[i].z[1];
            state[i].z[1] = y*coeffs[i] - x;
            x = y;
        }
        return x;
    };
    std::transform(src.begin(), src.end(), dst, proc_sample);
    if(updateState) LIKELY mState = state;
}


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

    /* D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y */
    for(size_t i{0};i < SamplesToDo;++i)
        mD[i] = mD[i] + 0.6554516f*mY[i];

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
        if(SamplesToDo >= sFilterDelay) LIKELY
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

/* This encoding implementation uses two sets of four chained IIR filters to
 * produce the desired relative phase shift. The first filter chain produces a
 * phase shift of varying degrees over a wide range of frequencies, while the
 * second filter chain produces a phase shift 90 degrees ahead of the first
 * over the same range. Further details are described here:
 *
 * https://web.archive.org/web/20060708031958/http://www.biochem.oulu.fi/~oniemita/dsp/hilbert/
 *
 * 2-channel UHJ output requires the use of three filter chains. The S channel
 * output uses a Filter1 chain on the W and X channel mix, while the D channel
 * output uses a Filter1 chain on the Y channel plus a Filter2 chain on the W
 * and X channel mix. This results in the W and X input mix on the D channel
 * output having the required +90 degree phase shift relative to the other
 * inputs.
 */
void UhjEncoderIIR::encode(float *LeftOut, float *RightOut,
    const al::span<const float *const, 3> InSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    const float *RESTRICT winput{al::assume_aligned<16>(InSamples[0])};
    const float *RESTRICT xinput{al::assume_aligned<16>(InSamples[1])};
    const float *RESTRICT yinput{al::assume_aligned<16>(InSamples[2])};

    /* S = 0.9396926*W + 0.1855740*X */
    std::transform(winput, winput+SamplesToDo, xinput, mTemp.begin(),
        [](const float w, const float x) noexcept { return 0.9396926f*w + 0.1855740f*x; });
    mFilter1WX.process(Filter1Coeff, {mTemp.data(), SamplesToDo}, true, mS.data()+1);
    mS[0] = mDelayWX; mDelayWX = mS[SamplesToDo];

    /* Precompute j(-0.3420201*W + 0.5098604*X) and store in mWX. */
    std::transform(winput, winput+SamplesToDo, xinput, mTemp.begin(),
        [](const float w, const float x) noexcept { return -0.3420201f*w + 0.5098604f*x; });
    mFilter2WX.process(Filter2Coeff, {mTemp.data(), SamplesToDo}, true, mWX.data());

    /* Apply filter1 to Y and store in mD. */
    mFilter1Y.process(Filter1Coeff, {yinput, SamplesToDo}, SamplesToDo, mD.data()+1);
    mD[0] = mDelayY; mDelayY = mD[SamplesToDo];

    /* D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y */
    for(size_t i{0};i < SamplesToDo;++i)
        mD[i] = mWX[i] + 0.6554516f*mD[i];

    /* Apply the base filter to the existing output to align with the processed
     * signal.
     */
    mFilter1Direct[0].process(Filter1Coeff, {LeftOut, SamplesToDo}, true, mTemp.data()+1);
    mTemp[0] = mDirectDelay[0]; mDirectDelay[0] = mTemp[SamplesToDo];

    /* Left = (S + D)/2.0 */
    float *RESTRICT left{al::assume_aligned<16>(LeftOut)};
    for(size_t i{0};i < SamplesToDo;i++)
        left[i] = (mS[i] + mD[i])*0.5f + mTemp[i];

    mFilter1Direct[1].process(Filter1Coeff, {RightOut, SamplesToDo}, true, mTemp.data()+1);
    mTemp[0] = mDirectDelay[1]; mDirectDelay[1] = mTemp[SamplesToDo];

    /* Right = (S - D)/2.0 */
    float *RESTRICT right{al::assume_aligned<16>(RightOut)};
    for(size_t i{0};i < SamplesToDo;i++)
        right[i] = (mS[i] - mD[i])*0.5f + mTemp[i];
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
    const bool updateState)
{
    static_assert(sInputPadding <= sMaxPadding, "Filter padding is too large");

    const auto &PShift = GetPhaseShifter<N>::Get();

    ASSUME(samplesToDo > 0);

    {
        const float *RESTRICT left{al::assume_aligned<16>(samples[0])};
        const float *RESTRICT right{al::assume_aligned<16>(samples[1])};
        const float *RESTRICT t{al::assume_aligned<16>(samples[2])};

        /* S = Left + Right */
        for(size_t i{0};i < samplesToDo+sInputPadding;++i)
            mS[i] = left[i] + right[i];

        /* D = Left - Right */
        for(size_t i{0};i < samplesToDo+sInputPadding;++i)
            mD[i] = left[i] - right[i];

        /* T */
        for(size_t i{0};i < samplesToDo+sInputPadding;++i)
            mT[i] = t[i];
    }

    float *RESTRICT woutput{al::assume_aligned<16>(samples[0])};
    float *RESTRICT xoutput{al::assume_aligned<16>(samples[1])};
    float *RESTRICT youtput{al::assume_aligned<16>(samples[2])};

    /* Precompute j(0.828331*D + 0.767820*T) and store in xoutput. */
    auto tmpiter = std::copy(mDTHistory.cbegin(), mDTHistory.cend(), mTemp.begin());
    std::transform(mD.cbegin(), mD.cbegin()+samplesToDo+sInputPadding, mT.cbegin(), tmpiter,
        [](const float d, const float t) noexcept { return 0.828331f*d + 0.767820f*t; });
    if(updateState) LIKELY
        std::copy_n(mTemp.cbegin()+samplesToDo, mDTHistory.size(), mDTHistory.begin());
    PShift.process({xoutput, samplesToDo}, mTemp.data());

    /* W = 0.981532*S + 0.197484*j(0.828331*D + 0.767820*T) */
    for(size_t i{0};i < samplesToDo;++i)
        woutput[i] = 0.981532f*mS[i] + 0.197484f*xoutput[i];
    /* X = 0.418496*S - j(0.828331*D + 0.767820*T) */
    for(size_t i{0};i < samplesToDo;++i)
        xoutput[i] = 0.418496f*mS[i] - xoutput[i];

    /* Precompute j*S and store in youtput. */
    tmpiter = std::copy(mSHistory.cbegin(), mSHistory.cend(), mTemp.begin());
    std::copy_n(mS.cbegin(), samplesToDo+sInputPadding, tmpiter);
    if(updateState) LIKELY
        std::copy_n(mTemp.cbegin()+samplesToDo, mSHistory.size(), mSHistory.begin());
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
    const bool updateState)
{
    static_assert(sInputPadding <= sMaxPadding, "Filter padding is too large");

    ASSUME(samplesToDo > 0);

    {
        const float *RESTRICT left{al::assume_aligned<16>(samples[0])};
        const float *RESTRICT right{al::assume_aligned<16>(samples[1])};

        /* S = Left + Right */
        for(size_t i{0};i < samplesToDo;++i)
            mS[i] = left[i] + right[i];

        /* D = Left - Right */
        for(size_t i{0};i < samplesToDo;++i)
            mD[i] = left[i] - right[i];
    }

    float *RESTRICT woutput{al::assume_aligned<16>(samples[0])};
    float *RESTRICT xoutput{al::assume_aligned<16>(samples[1])};
    float *RESTRICT youtput{al::assume_aligned<16>(samples[2])};

    /* Precompute j(0.828331*D + 0.767820*T) and store in xoutput. */
    std::transform(mD.cbegin(), mD.cbegin()+samplesToDo, youtput, mTemp.begin(),
        [](const float d, const float t) noexcept { return 0.828331f*d + 0.767820f*t; });
    mFilter2DT.process(Filter2Coeff, {mTemp.data(), samplesToDo}, updateState, xoutput);

    /* Apply filter1 to S and store in mTemp. */
    mTemp[0] = mDelayS;
    mFilter1S.process(Filter1Coeff, {mS.data(), samplesToDo}, updateState, mTemp.data()+1);
    if(updateState) LIKELY mDelayS = mTemp[samplesToDo];

    /* W = 0.981532*S + 0.197484*j(0.828331*D + 0.767820*T) */
    for(size_t i{0};i < samplesToDo;++i)
        woutput[i] = 0.981532f*mTemp[i] + 0.197484f*xoutput[i];
    /* X = 0.418496*S - j(0.828331*D + 0.767820*T) */
    for(size_t i{0};i < samplesToDo;++i)
        xoutput[i] = 0.418496f*mTemp[i] - xoutput[i];


    /* Apply filter1 to (0.795968*D - 0.676392*T) and store in mTemp. */
    std::transform(mD.cbegin(), mD.cbegin()+samplesToDo, youtput, youtput,
        [](const float d, const float t) noexcept { return 0.795968f*d - 0.676392f*t; });
    mTemp[0] = mDelayDT;
    mFilter1DT.process(Filter1Coeff, {youtput, samplesToDo}, updateState, mTemp.data()+1);
    if(updateState) LIKELY mDelayDT = mTemp[samplesToDo];

    /* Precompute j*S and store in youtput. */
    mFilter2S.process(Filter2Coeff, {mS.data(), samplesToDo}, updateState, youtput);

    /* Y = 0.795968*D - 0.676392*T + j(0.186633*S) */
    for(size_t i{0};i < samplesToDo;++i)
        youtput[i] = mTemp[i] + 0.186633f*youtput[i];


    if(samples.size() > 3)
    {
        float *RESTRICT zoutput{al::assume_aligned<16>(samples[3])};

        /* Apply filter1 to Q and store in mTemp. */
        mTemp[0] = mDelayQ;
        mFilter1Q.process(Filter1Coeff, {zoutput, samplesToDo}, updateState, mTemp.data()+1);
        if(updateState) LIKELY mDelayQ = mTemp[samplesToDo];

        /* Z = 1.023332*Q */
        for(size_t i{0};i < samplesToDo;++i)
            zoutput[i] = 1.023332f*mTemp[i];
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
    const bool updateState)
{
    static_assert(sInputPadding <= sMaxPadding, "Filter padding is too large");

    const auto &PShift = GetPhaseShifter<N>::Get();

    ASSUME(samplesToDo > 0);

    {
        const float *RESTRICT left{al::assume_aligned<16>(samples[0])};
        const float *RESTRICT right{al::assume_aligned<16>(samples[1])};

        for(size_t i{0};i < samplesToDo+sInputPadding;++i)
            mS[i] = left[i] + right[i];

        /* Pre-apply the width factor to the difference signal D. Smoothly
         * interpolate when it changes.
         */
        const float wtarget{mWidthControl};
        const float wcurrent{(mCurrentWidth < 0.0f) ? wtarget : mCurrentWidth};
        if(wtarget == wcurrent || !updateState)
        {
            for(size_t i{0};i < samplesToDo+sInputPadding;++i)
                mD[i] = (left[i] - right[i]) * wcurrent;
            mCurrentWidth = wcurrent;
        }
        else
        {
            const float wstep{(wtarget - wcurrent) / static_cast<float>(samplesToDo)};
            float fi{0.0f};
            for(size_t i{0};i < samplesToDo;++i)
            {
                mD[i] = (left[i] - right[i]) * (wcurrent + wstep*fi);
                fi += 1.0f;
            }
            for(size_t i{samplesToDo};i < samplesToDo+sInputPadding;++i)
                mD[i] = (left[i] - right[i]) * wtarget;
            mCurrentWidth = wtarget;
        }
    }

    float *RESTRICT woutput{al::assume_aligned<16>(samples[0])};
    float *RESTRICT xoutput{al::assume_aligned<16>(samples[1])};
    float *RESTRICT youtput{al::assume_aligned<16>(samples[2])};

    /* Precompute j*D and store in xoutput. */
    auto tmpiter = std::copy(mDTHistory.cbegin(), mDTHistory.cend(), mTemp.begin());
    std::copy_n(mD.cbegin(), samplesToDo+sInputPadding, tmpiter);
    if(updateState) LIKELY
        std::copy_n(mTemp.cbegin()+samplesToDo, mDTHistory.size(), mDTHistory.begin());
    PShift.process({xoutput, samplesToDo}, mTemp.data());

    /* W = 0.6098637*S - 0.6896511*j*w*D */
    for(size_t i{0};i < samplesToDo;++i)
        woutput[i] = 0.6098637f*mS[i] - 0.6896511f*xoutput[i];
    /* X = 0.8624776*S + 0.7626955*j*w*D */
    for(size_t i{0};i < samplesToDo;++i)
        xoutput[i] = 0.8624776f*mS[i] + 0.7626955f*xoutput[i];

    /* Precompute j*S and store in youtput. */
    tmpiter = std::copy(mSHistory.cbegin(), mSHistory.cend(), mTemp.begin());
    std::copy_n(mS.cbegin(), samplesToDo+sInputPadding, tmpiter);
    if(updateState) LIKELY
        std::copy_n(mTemp.cbegin()+samplesToDo, mSHistory.size(), mSHistory.begin());
    PShift.process({youtput, samplesToDo}, mTemp.data());

    /* Y = 1.6822415*w*D - 0.2156194*j*S */
    for(size_t i{0};i < samplesToDo;++i)
        youtput[i] = 1.6822415f*mD[i] - 0.2156194f*youtput[i];
}

void UhjStereoDecoderIIR::decode(const al::span<float*> samples, const size_t samplesToDo,
    const bool updateState)
{
    static_assert(sInputPadding <= sMaxPadding, "Filter padding is too large");

    ASSUME(samplesToDo > 0);

    {
        const float *RESTRICT left{al::assume_aligned<16>(samples[0])};
        const float *RESTRICT right{al::assume_aligned<16>(samples[1])};

        for(size_t i{0};i < samplesToDo;++i)
            mS[i] = left[i] + right[i];

        /* Pre-apply the width factor to the difference signal D. Smoothly
         * interpolate when it changes.
         */
        const float wtarget{mWidthControl};
        const float wcurrent{(mCurrentWidth < 0.0f) ? wtarget : mCurrentWidth};
        if(wtarget == wcurrent || !updateState)
        {
            for(size_t i{0};i < samplesToDo;++i)
                mD[i] = (left[i] - right[i]) * wcurrent;
            mCurrentWidth = wcurrent;
        }
        else
        {
            const float wstep{(wtarget - wcurrent) / static_cast<float>(samplesToDo)};
            float fi{0.0f};
            for(size_t i{0};i < samplesToDo;++i)
            {
                mD[i] = (left[i] - right[i]) * (wcurrent + wstep*fi);
                fi += 1.0f;
            }
            mCurrentWidth = wtarget;
        }
    }

    float *RESTRICT woutput{al::assume_aligned<16>(samples[0])};
    float *RESTRICT xoutput{al::assume_aligned<16>(samples[1])};
    float *RESTRICT youtput{al::assume_aligned<16>(samples[2])};

    /* Apply filter1 to S and store in mTemp. */
    mTemp[0] = mDelayS;
    mFilter1S.process(Filter1Coeff, {mS.data(), samplesToDo}, updateState, mTemp.data()+1);
    if(updateState) LIKELY mDelayS = mTemp[samplesToDo];

    /* Precompute j*D and store in xoutput. */
    mFilter2D.process(Filter2Coeff, {mD.data(), samplesToDo}, updateState, xoutput);

    /* W = 0.6098637*S - 0.6896511*j*w*D */
    for(size_t i{0};i < samplesToDo;++i)
        woutput[i] = 0.6098637f*mTemp[i] - 0.6896511f*xoutput[i];
    /* X = 0.8624776*S + 0.7626955*j*w*D */
    for(size_t i{0};i < samplesToDo;++i)
        xoutput[i] = 0.8624776f*mTemp[i] + 0.7626955f*xoutput[i];

    /* Precompute j*S and store in youtput. */
    mFilter2S.process(Filter2Coeff, {mS.data(), samplesToDo}, updateState, youtput);

    /* Apply filter1 to D and store in mTemp. */
    mTemp[0] = mDelayD;
    mFilter1D.process(Filter1Coeff, {mD.data(), samplesToDo}, updateState, mTemp.data()+1);
    if(updateState) LIKELY mDelayD = mTemp[samplesToDo];

    /* Y = 1.6822415*w*D - 0.2156194*j*S */
    for(size_t i{0};i < samplesToDo;++i)
        youtput[i] = 1.6822415f*mTemp[i] - 0.2156194f*youtput[i];
}


template struct UhjEncoder<UhjLength256>;
template struct UhjDecoder<UhjLength256>;
template struct UhjStereoDecoder<UhjLength256>;

template struct UhjEncoder<UhjLength512>;
template struct UhjDecoder<UhjLength512>;
template struct UhjStereoDecoder<UhjLength512>;
