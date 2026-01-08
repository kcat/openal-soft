
#include "config.h"

#include "uhjfilter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <iterator>
#include <memory>
#include <numbers>
#include <ranges>
#include <span>
#include <vector>

#include "alcomplex.h"
#include "alnumeric.h"
#include "allpass_conv.hpp"
#include "gsl/gsl"
#include "pffft.h"
#include "phase_shifter.h"
#include "vector.h"


namespace {

template<std::size_t A, typename T, std::size_t N>
constexpr auto assume_aligned_span(const std::span<T,N> s) noexcept -> std::span<T,N>
{ return std::span<T,N>{std::assume_aligned<A>(s.data()), s.size()}; }

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
 * The phase shift is done using a linear FIR filter implemented from a
 * segmented FFT'd response for the desired shift.
 */

template<usize N>
void UhjEncoder<N>::encode(const std::span<float> LeftOut, const std::span<float> RightOut,
    const std::span<const std::span<const float>> InSamples)
{
    static_assert(sFftLength == gSegmentedFilter<N>.sFftLength);
    static_assert(sSegmentSize == gSegmentedFilter<N>.sSampleLength);
    static_assert(sNumSegments == gSegmentedFilter<N>.sNumSegments);

    const auto samplesToDo = InSamples[0].size();
    const auto winput = assume_aligned_span<16>(InSamples[0]);
    const auto xinput = assume_aligned_span<16>(InSamples[1].first(samplesToDo));
    const auto yinput = assume_aligned_span<16>(InSamples[2].first(samplesToDo));

    std::ranges::copy(winput, std::next(mW.begin(), sFilterDelay));
    std::ranges::copy(xinput, std::next(mX.begin(), sFilterDelay));
    std::ranges::copy(yinput, std::next(mY.begin(), sFilterDelay));

    /* S = 0.9396926*W + 0.1855740*X */
    std::ranges::transform(mW | std::views::take(samplesToDo), mX, mS.begin(),
        [](const float w, const float x) noexcept { return 0.9396926f*w + 0.1855740f*x; });

    /* Precompute j(-0.3420201*W + 0.5098604*X) and store in mD. */
    auto dstore = mD.begin();
    auto curseg = mCurrentSegment;
    for(auto base = 0_uz;base < samplesToDo;)
    {
        const auto todo = std::min(sSegmentSize-mFifoPos, samplesToDo-base);
        const auto wseg = winput.subspan(base, todo);
        const auto xseg = xinput.subspan(base, todo);
        const auto wxio = std::span{mWXInOut}.subspan(mFifoPos, todo);

        /* Copy out the samples that were previously processed by the FFT. */
        dstore = std::ranges::copy(wxio, dstore).out;

        /* Transform the non-delayed input and store in the front half of the
         * filter input.
         */
        std::ranges::transform(wseg, xseg, wxio.begin(), [](const float w, const float x) noexcept
        { return -0.3420201f*w + 0.5098604f*x; });

        mFifoPos += todo;
        base += todo;

        /* Check whether the input buffer is filled with new samples. */
        if(mFifoPos < sSegmentSize) break;
        mFifoPos = 0;

        /* Copy the new input to the next history segment, clearing the back
         * half of the segment, and convert to the frequency domain.
         */
        auto input = mWXHistory.begin() + curseg*sFftLength;
        auto initer = std::ranges::copy(mWXInOut | std::views::take(sSegmentSize), input).out;
        std::ranges::fill(std::views::counted(initer, sSegmentSize), 0.0f);

        gSegmentedFilter<N>.mFft.transform(input, input, mWorkData.begin(), PFFFT_FORWARD);

        /* Convolve each input segment with its IR filter counterpart (aligned
         * in time, from newest to oldest).
         */
        mFftBuffer.fill(0.0f);
        auto filter = gSegmentedFilter<N>.mFilterData.begin();
        for(const auto s [[maybe_unused]] : std::views::iota(curseg, sNumSegments))
        {
            gSegmentedFilter<N>.mFft.zconvolve_accumulate(input, filter, mFftBuffer.begin());
            std::advance(input, sFftLength);
            std::advance(filter, sFftLength);
        }
        input = mWXHistory.begin();
        for(const auto s [[maybe_unused]] : std::views::iota(0_uz, curseg))
        {
            gSegmentedFilter<N>.mFft.zconvolve_accumulate(input, filter, mFftBuffer.begin());
            std::advance(input, sFftLength);
            std::advance(filter, sFftLength);
        }

        /* Convert back to samples, writing to the output and storing the extra
         * for next time.
         */
        gSegmentedFilter<N>.mFft.transform(mFftBuffer.begin(), mFftBuffer.begin(),
            mWorkData.begin(), PFFFT_BACKWARD);

        const auto wxiter = std::ranges::transform(mFftBuffer | std::views::take(sSegmentSize),
            mWXInOut | std::views::drop(sSegmentSize), mWXInOut.begin(), std::plus{}).out;
        std::ranges::copy(mFftBuffer | std::views::drop(sSegmentSize), wxiter);

        /* Shift the input history. */
        curseg = curseg ? (curseg-1) : (sNumSegments-1);
    }
    mCurrentSegment = curseg;

    /* D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y */
    std::ranges::transform(mD | std::views::take(samplesToDo), mY, mD.begin(),
        [](const float jwx, const float y) noexcept { return jwx + 0.6554516f*y; });

    /* Copy the future samples to the front for next time. */
    const auto take_end = std::views::drop(samplesToDo) | std::views::take(sFilterDelay);
    std::ranges::copy(mW | take_end, mW.begin());
    std::ranges::copy(mX | take_end, mX.begin());
    std::ranges::copy(mY | take_end, mY.begin());

    /* Apply a delay to the existing output to align with the input delay. */
    std::ignore = std::ranges::mismatch(mDirectDelay, std::array{LeftOut, RightOut},
        [](std::span<float,sFilterDelay> delayBuffer, const std::span<float> buffer)
    {
        const auto distbuf = assume_aligned_span<16>(delayBuffer);

        const auto inout = assume_aligned_span<16>(buffer);
        if(inout.size() >= sFilterDelay)
        {
            const auto inout_start = std::prev(inout.end(), sFilterDelay);
            const auto delay_end = std::ranges::rotate(inout, inout_start).begin();
            std::ranges::swap_ranges(std::span{inout.begin(), delay_end}, distbuf);
        }
        else
        {
            const auto delay_start = std::ranges::swap_ranges(inout, distbuf).in2;
            std::ranges::rotate(distbuf, delay_start);
        }
        return true;
    });

    /* Combine the direct signal with the produced output. */

    /* Left = (S + D)/2.0 */
    const auto left = assume_aligned_span<16>(LeftOut);
    for(auto i = 0_uz;i < samplesToDo;++i)
        left[i] += (mS[i] + mD[i]) * 0.5f;

    /* Right = (S - D)/2.0 */
    const auto right = assume_aligned_span<16>(RightOut);
    for(auto i = 0_uz;i < samplesToDo;++i)
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
void UhjEncoderIIR::encode(const std::span<float> LeftOut, const std::span<float> RightOut,
    const std::span<const std::span<const float>> InSamples)
{
    const auto samplesToDo = InSamples[0].size();
    const auto winput = assume_aligned_span<16>(InSamples[0]);
    const auto xinput = assume_aligned_span<16>(InSamples[1].first(samplesToDo));
    const auto yinput = assume_aligned_span<16>(InSamples[2].first(samplesToDo));

    /* S = 0.9396926*W + 0.1855740*X */
    std::ranges::transform(winput, xinput, mTemp.begin(),
        [](const float w, const float x) noexcept { return 0.9396926f*w + 0.1855740f*x; });
    process(mFilter1WX, Filter1Coeff, std::span{mTemp}.first(samplesToDo), true,
        std::span{mS}.subspan(1));
    mS[0] = mDelayWX; mDelayWX = mS[samplesToDo];

    /* Precompute j(-0.3420201*W + 0.5098604*X) and store in mWX. */
    std::ranges::transform(winput, xinput, mTemp.begin(),
        [](const float w, const float x) noexcept { return -0.3420201f*w + 0.5098604f*x; });
    process(mFilter2WX, Filter2Coeff, std::span{mTemp}.first(samplesToDo), true, mWX);

    /* Apply filter1 to Y and store in mD. */
    process(mFilter1Y, Filter1Coeff, yinput, true, std::span{mD}.subspan(1));
    mD[0] = mDelayY; mDelayY = mD[samplesToDo];

    /* D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y */
    std::ranges::transform(mWX | std::views::take(samplesToDo), mD, mD.begin(),
        [](const float jwx, const float y) noexcept { return jwx + 0.6554516f*y; });

    /* Apply the base filter to the existing output to align with the processed
     * signal.
     */
    const auto left = assume_aligned_span<16>(LeftOut.first(samplesToDo));
    process(mFilter1Direct[0], Filter1Coeff, left, true, std::span{mTemp}.subspan(1));
    mTemp[0] = mDirectDelay[0]; mDirectDelay[0] = mTemp[samplesToDo];

    /* Left = (S + D)/2.0 */
    for(auto i = 0_uz;i < samplesToDo;++i)
        left[i] = (mS[i] + mD[i])*0.5f + mTemp[i];

    const auto right = assume_aligned_span<16>(RightOut.first(samplesToDo));
    process(mFilter1Direct[1], Filter1Coeff, right, true, std::span{mTemp}.subspan(1));
    mTemp[0] = mDirectDelay[1]; mDirectDelay[1] = mTemp[samplesToDo];

    /* Right = (S - D)/2.0 */
    for(auto i = 0_uz;i < samplesToDo;++i)
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
void UhjDecoder<N>::decode(const std::span<std::span<float>> samples, const bool updateState)
{
    static_assert(sInputPadding <= sMaxPadding, "Filter padding is too large");

    {
        const auto left = assume_aligned_span<16>(samples[0]);
        const auto right = assume_aligned_span<16>(samples[1]);
        const auto t = assume_aligned_span<16>(samples[2]);

        /* S = Left + Right */
        std::ranges::transform(left, right, mS.begin(), std::plus{});

        /* D = Left - Right */
        std::ranges::transform(left, right, mD.begin(), std::minus{});

        /* T */
        std::ranges::copy(t, mT.begin());
    }

    const auto samplesToDo = samples[0].size() - sInputPadding;
    const auto woutput = assume_aligned_span<16>(samples[0].first(samplesToDo));
    const auto xoutput = assume_aligned_span<16>(samples[1].first(samplesToDo));
    const auto youtput = assume_aligned_span<16>(samples[2].first(samplesToDo));

    /* Precompute j(0.828331*D + 0.767820*T) and store in xoutput. */
    auto tmpiter = std::ranges::copy(mDTHistory, mTemp.begin()).out;
    std::ranges::transform(mD | std::views::take(samplesToDo+sInputPadding), mT, tmpiter,
        [](const float d, const float t) noexcept { return 0.828331f*d + 0.767820f*t; });
    if(updateState) [[likely]]
        std::ranges::copy(mTemp|std::views::drop(samplesToDo)|std::views::take(mDTHistory.size()),
            mDTHistory.begin());
    gPShifter<N>.process(xoutput, mTemp);

    /* W = 0.981532*S + 0.197484*j(0.828331*D + 0.767820*T) */
    std::ranges::transform(mS | std::views::take(samplesToDo), xoutput, woutput.begin(),
        [](const float s, const float jdt) noexcept { return 0.981532f*s + 0.197484f*jdt; });

    /* X = 0.418496*S - j(0.828331*D + 0.767820*T) */
    std::ranges::transform(mS | std::views::take(samplesToDo), xoutput, xoutput.begin(),
        [](const float s, const float jdt) noexcept { return 0.418496f*s - jdt; });

    /* Precompute j*S and store in youtput. */
    tmpiter = std::ranges::copy(mSHistory, mTemp.begin()).out;
    std::ranges::copy(mS | std::views::take(samplesToDo+sInputPadding), tmpiter);
    if(updateState) [[likely]]
        std::ranges::copy(mTemp|std::views::drop(samplesToDo)|std::views::take(mSHistory.size()),
            mSHistory.begin());
    gPShifter<N>.process(youtput, mTemp);

    /* Y = 0.795968*D - 0.676392*T + j(0.186633*S) */
    for(auto i = 0_uz;i < samplesToDo;++i)
        youtput[i] = 0.795968f*mD[i] - 0.676392f*mT[i] + 0.186633f*youtput[i];

    if(samples.size() > 3)
    {
        const auto zoutput = assume_aligned_span<16>(samples[3].first(samplesToDo));
        /* Z = 1.023332*Q */
        std::ranges::transform(zoutput, zoutput.begin(), [](float q) { return 1.023332f*q; });
    }
}

void UhjDecoderIIR::decode(const std::span<std::span<float>> samples, const bool updateState)
{
    static_assert(sInputPadding <= sMaxPadding, "Filter padding is too large");

    {
        const auto left = assume_aligned_span<16>(samples[0]);
        const auto right = assume_aligned_span<16>(samples[1]);

        /* S = Left + Right */
        std::ranges::transform(left, right, mS.begin(), std::plus{});

        /* D = Left - Right */
        std::ranges::transform(left, right, mD.begin(), std::minus{});
    }

    const auto samplesToDo = samples[0].size() - sInputPadding;
    const auto woutput = assume_aligned_span<16>(samples[0].first(samplesToDo));
    const auto xoutput = assume_aligned_span<16>(samples[1].first(samplesToDo));
    const auto youtput = assume_aligned_span<16>(samples[2].first(samplesToDo));

    /* Precompute j(0.828331*D + 0.767820*T) and store in xoutput. */
    std::ranges::transform(mD, assume_aligned_span<16>(samples[2]), mTemp.begin(),
        [](const float d, const float t) noexcept
    { return 0.828331f*d + 0.767820f*t; });
    if(mFirstRun) processOne(mFilter2DT, Filter2Coeff, mTemp[0]);
    process(mFilter2DT, Filter2Coeff, std::span{mTemp}.subspan(1, samplesToDo), updateState,
        xoutput);

    /* Apply filter1 to S and store in mTemp. */
    process(mFilter1S, Filter1Coeff, std::span{mS}.first(samplesToDo), updateState, mTemp);

    /* W = 0.981532*S + 0.197484*j(0.828331*D + 0.767820*T) */
    std::ranges::transform(mTemp, xoutput, woutput.begin(),
        [](const float s, const float jdt) noexcept { return 0.981532f*s + 0.197484f*jdt; });
    /* X = 0.418496*S - j(0.828331*D + 0.767820*T) */
    std::ranges::transform(mTemp, xoutput, xoutput.begin(),
        [](const float s, const float jdt) noexcept { return 0.418496f*s - jdt; });

    /* Apply filter1 to (0.795968*D - 0.676392*T) and store in mTemp. */
    std::ranges::transform(mD | std::views::take(samplesToDo), youtput, youtput.begin(),
        [](const float d, const float t) noexcept { return 0.795968f*d - 0.676392f*t; });
    process(mFilter1DT, Filter1Coeff, youtput, updateState, mTemp);

    /* Precompute j*S and store in youtput. */
    if(mFirstRun) processOne(mFilter2S, Filter2Coeff, mS[0]);
    process(mFilter2S, Filter2Coeff, std::span{mS}.subspan(1, samplesToDo), updateState, youtput);

    /* Y = 0.795968*D - 0.676392*T + j(0.186633*S) */
    std::ranges::transform(mTemp | std::views::take(samplesToDo), youtput, youtput.begin(),
        [](const float dt, const float js) noexcept { return dt + 0.186633f*js; });

    if(samples.size() > 3)
    {
        const auto zoutput = assume_aligned_span<16>(samples[3].first(samplesToDo));

        /* Apply filter1 to Q and store in mTemp. */
        process(mFilter1Q, Filter1Coeff, zoutput, updateState, mTemp);

        /* Z = 1.023332*Q */
        std::ranges::transform(mTemp | std::views::take(samplesToDo), zoutput.begin(),
            [](const float q) noexcept { return 1.023332f*q; });
    }

    mFirstRun = false;
}


/* Super Stereo processing is done as:
 *
 * S = Left + Right
 * D = Left - Right
 *
 * W = 0.6098637*S + 0.6896511*j*w*D
 * X = 0.8624776*S - 0.7626955*j*w*D
 * Y = 1.6822415*w*D + 0.2156194*j*S
 *
 * where j is a +90 degree phase shift. w is a variable control for the
 * resulting stereo width, with the range 0 <= w <= 0.7.
 */
template<size_t N>
void UhjStereoDecoder<N>::decode(const std::span<std::span<float>> samples, const bool updateState)
{
    static_assert(sInputPadding <= sMaxPadding, "Filter padding is too large");

    const auto samplesToDo = samples[0].size() - sInputPadding;

    {
        const auto left = assume_aligned_span<16>(samples[0]);
        const auto right = assume_aligned_span<16>(samples[1]);

        std::ranges::transform(left, right, mS.begin(), std::plus{});

        /* Pre-apply the width factor to the difference signal D. Smoothly
         * interpolate when it changes.
         */
        const auto wtarget = mWidthControl;
        const auto wcurrent = (mCurrentWidth < 0.0f) ? wtarget : mCurrentWidth;
        if(wtarget == wcurrent || !updateState)
        {
            std::ranges::transform(left, right, mD.begin(), [wcurrent](float l, float r) noexcept
            { return (l-r) * wcurrent; });
            mCurrentWidth = wcurrent;
        }
        else
        {
            const auto wstep = (wtarget - wcurrent) / gsl::narrow_cast<float>(samplesToDo);
            auto fi = 0.0f;

            const auto lfade = left.first(samplesToDo);
            auto dstore = std::ranges::transform(lfade, right, mD.begin(),
                [wcurrent,wstep,&fi](const float l, const float r) noexcept
            {
                const float ret{(l-r) * (wcurrent + wstep*fi)};
                fi += 1.0f;
                return ret;
            }).out;

            const auto lend = left.last(sInputPadding);
            const auto rend = right.last(sInputPadding);
            std::ranges::transform(lend, rend, dstore, [wtarget](float l, float r) noexcept
            { return (l-r) * wtarget; });
            mCurrentWidth = wtarget;
        }
    }

    const auto woutput = assume_aligned_span<16>(samples[0].first(samplesToDo));
    const auto xoutput = assume_aligned_span<16>(samples[1].first(samplesToDo));
    const auto youtput = assume_aligned_span<16>(samples[2].first(samplesToDo));

    /* Precompute j*D and store in xoutput. */
    auto tmpiter = std::ranges::copy(mDTHistory, mTemp.begin()).out;
    std::ranges::copy(mD | std::views::take(samplesToDo+sInputPadding), tmpiter);
    if(updateState) [[likely]]
        std::ranges::copy(mTemp|std::views::drop(samplesToDo)|std::views::take(mDTHistory.size()),
            mDTHistory.begin());
    gPShifter<N>.process(xoutput, mTemp);

    /* W = 0.6098637*S + 0.6896511*j*w*D */
    std::ranges::transform(mS, xoutput, woutput.begin(), [](const float s, const float jd) noexcept
    { return 0.6098637f*s + 0.6896511f*jd; });
    /* X = 0.8624776*S - 0.7626955*j*w*D */
    std::ranges::transform(mS, xoutput, xoutput.begin(), [](const float s, const float jd) noexcept
    { return 0.8624776f*s - 0.7626955f*jd; });

    /* Precompute j*S and store in youtput. */
    tmpiter = std::ranges::copy(mSHistory, mTemp.begin()).out;
    std::ranges::copy(mS | std::views::take(samplesToDo+sInputPadding), tmpiter);
    if(updateState) [[likely]]
        std::ranges::copy(mTemp|std::views::drop(samplesToDo)|std::views::take(mSHistory.size()),
            mSHistory.begin());
    gPShifter<N>.process(youtput, mTemp);

    /* Y = 1.6822415*w*D + 0.2156194*j*S */
    std::ranges::transform(mD, youtput, youtput.begin(), [](const float d, const float js) noexcept
    { return 1.6822415f*d + 0.2156194f*js; });
}

void UhjStereoDecoderIIR::decode(const std::span<std::span<float>> samples, const bool updateState)
{
    static_assert(sInputPadding <= sMaxPadding, "Filter padding is too large");

    const auto samplesToDo = samples[0].size() - sInputPadding;

    {
        const auto left = assume_aligned_span<16>(samples[0]);
        const auto right = assume_aligned_span<16>(samples[1]);

        std::ranges::transform(left, right, mS.begin(), std::plus{});

        /* Pre-apply the width factor to the difference signal D. Smoothly
         * interpolate when it changes.
         */
        const auto wtarget = mWidthControl;
        const auto wcurrent = (mCurrentWidth < 0.0f) ? wtarget : mCurrentWidth;
        if(wtarget == wcurrent || !updateState)
        {
            std::ranges::transform(left, right, mD.begin(), [wcurrent](float l, float r) noexcept
            { return (l-r) * wcurrent; });
            mCurrentWidth = wcurrent;
        }
        else
        {
            const auto wstep = (wtarget - wcurrent) / gsl::narrow_cast<float>(samplesToDo);
            auto fi = 0.0f;

            const auto lfade = left.first(samplesToDo);
            auto dstore = std::ranges::transform(lfade, right, mD.begin(),
                [wcurrent,wstep,&fi](const float l, const float r) noexcept
            {
                const float ret{(l-r) * (wcurrent + wstep*fi)};
                fi += 1.0f;
                return ret;
            }).out;

            const auto lend = left.last(sInputPadding);
            const auto rend = right.last(sInputPadding);
            std::ranges::transform(lend, rend, dstore, [wtarget](float l, float r) noexcept
            { return (l-r) * wtarget; });
            mCurrentWidth = wtarget;
        }
    }

    const auto woutput = assume_aligned_span<16>(samples[0].first(samplesToDo));
    const auto xoutput = assume_aligned_span<16>(samples[1].first(samplesToDo));
    const auto youtput = assume_aligned_span<16>(samples[2].first(samplesToDo));

    /* Apply filter1 to S and store in mTemp. */
    process(mFilter1S, Filter1Coeff, std::span{mS}.first(samplesToDo), updateState, mTemp);

    /* Precompute j*D and store in xoutput. */
    if(mFirstRun) processOne(mFilter2D, Filter2Coeff, mD[0]);
    process(mFilter2D, Filter2Coeff, std::span{mD}.subspan(1, samplesToDo), updateState, xoutput);

    /* W = 0.6098637*S + 0.6896511*j*w*D */
    std::ranges::transform(mTemp, xoutput, woutput.begin(), [](float s, float jd) noexcept
    { return 0.6098637f*s + 0.6896511f*jd; });
    /* X = 0.8624776*S - 0.7626955*j*w*D */
    std::ranges::transform(mTemp, xoutput, xoutput.begin(), [](float s, float jd) noexcept
    { return 0.8624776f*s - 0.7626955f*jd; });

    /* Precompute j*S and store in youtput. */
    if(mFirstRun) processOne(mFilter2S, Filter2Coeff, mS[0]);
    process(mFilter2S, Filter2Coeff, std::span{mS}.subspan(1, samplesToDo), updateState, youtput);

    /* Apply filter1 to D and store in mTemp. */
    process(mFilter1D, Filter1Coeff, std::span{mD}.first(samplesToDo), updateState, mTemp);

    /* Y = 1.6822415*w*D + 0.2156194*j*S */
    std::ranges::transform(mTemp, youtput, youtput.begin(), [](float d, float js) noexcept
    { return 1.6822415f*d + 0.2156194f*js; });

    mFirstRun = false;
}


template struct UhjEncoder<UhjLength256>;
template struct UhjDecoder<UhjLength256>;
template struct UhjStereoDecoder<UhjLength256>;

template struct UhjEncoder<UhjLength512>;
template struct UhjDecoder<UhjLength512>;
template struct UhjStereoDecoder<UhjLength512>;
