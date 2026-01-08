
#include <algorithm>

#include "allpass_conv.hpp"
#include "altypes.hpp"
#include "tsmefilter.hpp"

namespace {

template<usize A, typename T, usize N>
constexpr auto assume_aligned_span(std::span<T,N> const s) noexcept -> std::span<T,N>
{ return std::span<T,N>{std::assume_aligned<A>(s.data()), s.size()}; }

} /* namespace */


/* Encoding Tetraphonic Surround from ACN/N3D B-Format is done as:
 *
 * Fl = 0.353553390592*W +  0.288623887591*Y +  0.204196677392*X
 * Fr = 0.353553390592*W + -0.288623887591*Y +  0.204196677392*X
 * Bu = 0.353553390592*W +  0.288623887591*Z + -0.204196677392*X
 * Bd = 0.353553390592*W + -0.288623887591*Z + -0.204196677392*X
 *
 * Flt = 0.985144642804*Fl - 0.169433780045*Fr
 * Frt = 0.985144642804*Fr - 0.169433780045*Fl
 *
 * Blt = -0.459812358448*Bu + j(0.888016100653*Bu) + 0.459812358448*Bd + j(0.888016100653*Bd)
 * Brt = -0.459812358448*Bd + j(0.888016100653*Bd) + 0.459812358448*Bu + j(0.888016100653*Bu)
 *
 * Left  = Flt + 0.707106781187*Blt
 * Right = Frt - 0.707106781187*Brt
 *
 * where j is a wide-band +90 degree phase shift. Breaking this down, we can
 * calculate Flt, Frt, Blt, and Brt directly:
 *
 * Flt = 0.985144642804*(0.353553390592*W + 0.288623887591*Y + 0.204196677392*X) - 0.169433780045*(0.353553390592*W + -0.288623887591*Y + 0.204196677392*X)
 *     = 0.985144642804*(0.353553390592*W + 0.288623887591*Y + 0.204196677392*X) + -0.169433780045*(0.353553390592*W + -0.288623887591*Y + 0.204196677392*X)
 *     = 0.985144642804*0.353553390592*W + 0.985144642804*0.288623887591*Y + 0.985144642804*0.204196677392*X + -0.169433780045*0.353553390592*W + -0.169433780045*-0.288623887591*Y + -0.169433780045*0.204196677392*X
 *     = 0.985144642804*0.353553390592*W + -0.169433780045*0.353553390592*W + 0.985144642804*0.288623887591*Y + -0.169433780045*-0.288623887591*Y + 0.985144642804*0.204196677392*X + -0.169433780045*0.204196677392*X
 *     = (0.985144642804*0.353553390592 + -0.169433780045*0.353553390592)*W + (0.985144642804*0.288623887591 + -0.169433780045*-0.288623887591)*Y + (0.985144642804*0.204196677392 + -0.169433780045*0.204196677392)*X
 *     = 0.288397341271*W + 0.333238912931*Y + 0.166565447888*X
 *
 * Frt = 0.985144642804*(0.353553390592*W + -0.288623887591*Y + 0.204196677392*X) - 0.169433780045*(0.353553390592*W + 0.288623887591*Y + 0.204196677392*X)
 *     = 0.985144642804*(0.353553390592*W + -0.288623887591*Y + 0.204196677392*X) + -0.169433780045*(0.353553390592*W + 0.288623887591*Y + 0.204196677392*X)
 *     = 0.985144642804*0.353553390592*W + 0.985144642804*-0.288623887591*Y + 0.985144642804*0.204196677392*X + -0.169433780045*0.353553390592*W + -0.169433780045*0.288623887591*Y + -0.169433780045*0.204196677392*X
 *     = 0.985144642804*0.353553390592*W + -0.169433780045*0.353553390592*W + 0.985144642804*-0.288623887591*Y + -0.169433780045*0.288623887591*Y + 0.985144642804*0.204196677392*X + -0.169433780045*0.204196677392*X
 *     = (0.985144642804*0.353553390592 + -0.169433780045*0.353553390592)*W + (0.985144642804*-0.288623887591 + -0.169433780045*0.288623887591)*Y + (0.985144642804*0.204196677392 + -0.169433780045*0.204196677392)*X
 *     = 0.288397341271*W + -0.333238912931*Y + 0.166565447888*X
 *
 * Blt = -0.459812358448*(0.353553390592*W + 0.288623887591*Z + -0.204196677392*X) + j(0.888016100653*(0.353553390592*W + 0.288623887591*Z + -0.204196677392*X))
 *       + 0.459812358448*(0.353553390592*W + -0.288623887591*Z + -0.204196677392*X) + j(0.888016100653*(0.353553390592*W + -0.288623887591*Z + -0.204196677392*X))
 *     = -0.459812358448*(0.353553390592*W + 0.288623887591*Z + -0.204196677392*X) + 0.459812358448*(0.353553390592*W + -0.288623887591*Z + -0.204196677392*X)
 *       + j(0.888016100653*(0.353553390592*W + 0.288623887591*Z + -0.204196677392*X) + 0.888016100653*(0.353553390592*W + -0.288623887591*Z + -0.204196677392*X))
 *     = -0.459812358448*0.353553390592*W + -0.459812358448*0.288623887591*Z + -0.459812358448*-0.204196677392*X + 0.459812358448*0.353553390592*W + 0.459812358448*-0.288623887591*Z + 0.459812358448*-0.204196677392*X
 *       + j(0.888016100653*0.353553390592*W + 0.888016100653*0.288623887591*Z + 0.888016100653*-0.204196677392*X + 0.888016100653*0.353553390592*W + 0.888016100653*-0.288623887591*Z + 0.888016100653*-0.204196677392*X)
 *     = -0.459812358448*0.353553390592*W + 0.459812358448*0.353553390592*W + -0.459812358448*0.288623887591*Z + 0.459812358448*-0.288623887591*Z + -0.459812358448*-0.204196677392*X + 0.459812358448*-0.204196677392*X
 *       + j(0.888016100653*0.353553390592*W + 0.888016100653*0.353553390592*W + 0.888016100653*0.288623887591*Z + 0.888016100653*-0.288623887591*Z + 0.888016100653*-0.204196677392*X + 0.888016100653*-0.204196677392*X)
 *     = (-0.459812358448*0.353553390592 + 0.459812358448*0.353553390592)*W + (-0.459812358448*0.288623887591 + 0.459812358448*-0.288623887591)*Z + (-0.459812358448*-0.204196677392 + 0.459812358448*-0.204196677392)*X
 *       + j((0.888016100653*0.353553390592 + 0.888016100653*0.353553390592)*W + (0.888016100653*0.288623887591 + 0.888016100653*-0.288623887591)*Z + (0.888016100653*-0.204196677392 + 0.888016100653*-0.204196677392)*X)
 *     = -0.265425660915*Z + j(0.627922206572*W + -0.362659874448*X)
 *
 * Brt = -0.459812358448*(0.353553390592*W + -0.288623887591*Z + -0.204196677392*X) + j(0.888016100653*(0.353553390592*W + -0.288623887591*Z + -0.204196677392*X))
 *       + 0.459812358448*(0.353553390592*W + 0.288623887591*Z + -0.204196677392*X) + j(0.888016100653*(0.353553390592*W + 0.288623887591*Z + -0.204196677392*X))
 *     = -0.459812358448*(0.353553390592*W + -0.288623887591*Z + -0.204196677392*X) + 0.459812358448*(0.353553390592*W + 0.288623887591*Z + -0.204196677392*X)
 *       + j(0.888016100653*(0.353553390592*W + -0.288623887591*Z + -0.204196677392*X) + 0.888016100653*(0.353553390592*W + 0.288623887591*Z + -0.204196677392*X))
 *     = -0.459812358448*0.353553390592*W + -0.459812358448*-0.288623887591*Z + -0.459812358448*-0.204196677392*X + 0.459812358448*0.353553390592*W + 0.459812358448*0.288623887591*Z + 0.459812358448*-0.204196677392*X
 *       + j(0.888016100653*0.353553390592*W + 0.888016100653*-0.288623887591*Z + 0.888016100653*-0.204196677392*X + 0.888016100653*0.353553390592*W + 0.888016100653*0.288623887591*Z + 0.888016100653*-0.204196677392*X)
 *     = -0.459812358448*0.353553390592*W + 0.459812358448*0.353553390592*W + -0.459812358448*-0.288623887591*Z + 0.459812358448*0.288623887591*Z + -0.459812358448*-0.204196677392*X + 0.459812358448*-0.204196677392*X
 *       + j(0.888016100653*0.353553390592*W + 0.888016100653*0.353553390592*W + 0.888016100653*-0.288623887591*Z + 0.888016100653*0.288623887591*Z + 0.888016100653*-0.204196677392*X + 0.888016100653*-0.204196677392*X)
 *     = (-0.459812358448*0.353553390592 + 0.459812358448*0.353553390592)*W + (-0.459812358448*-0.288623887591 + 0.459812358448*0.288623887591)*Z + (-0.459812358448*-0.204196677392 + 0.459812358448*-0.204196677392)*X
 *       + j((0.888016100653*0.353553390592 + 0.888016100653*0.353553390592)*W + (0.888016100653*-0.288623887591 + 0.888016100653*0.288623887591)*Z + (0.888016100653*-0.204196677392 + 0.888016100653*-0.204196677392)*X)
 *     = 0.265425660915*Z + j(0.627922206572*W + -0.362659874448*X)
 *
 * We can further break down the Left and Right results using the new inputs:
 *
 * Left  = Flt + 0.707106781187*Blt
 *       = 0.288397341271*W + 0.333238912931*Y + 0.166565447888*X + 0.707106781187*(-0.265425660915*Z + j(0.627922206572*W + -0.362659874448*X))
 *       = 0.288397341271*W + 0.333238912931*Y + 0.166565447888*X + 0.707106781187*-0.265425660915*Z + j(0.707106781187*0.627922206572*W + 0.707106781187*-0.362659874448*X)
 *       = 0.288397341271*W + 0.333238912931*Y + 0.166565447888*X + -0.187684284734*Z + j(0.444008050325*W + -0.256439256487*X)
 *
 * Right = Frt + -0.707106781187*Brt
 *       = 0.288397341271*W + -0.333238912931*Y + 0.166565447888*X + -0.707106781187*( 0.265425660915*Z + j(0.627922206572*W + -0.362659874448*X))
 *       = 0.288397341271*W + -0.333238912931*Y + 0.166565447888*X + -0.707106781187*0.265425660915*Z + j(-0.707106781187*0.627922206572*W + -0.707106781187*-0.362659874448*X)
 *       = 0.288397341271*W + -0.333238912931*Y + 0.166565447888*X + -0.187684284734*Z + j(-0.444008050325*W + 0.256439256487*X)
 *
 * To simplify this more, we can take the Sum and Difference signals:
 *
 * S = Left/2 + Right/2
 *   = (0.288397341271*W + 0.333238912931*Y + 0.166565447888*X + -0.187684284734*Z + j(0.444008050325*W + -0.256439256487*X))/2
 *     + (0.288397341271*W + -0.333238912931*Y + 0.166565447888*X + -0.187684284734*Z + j(-0.444008050325*W + 0.256439256487*X))/2
 *   = (0.288397341271*W + 0.166565447888*X + -0.187684284734*Z)/2
 *     + (0.288397341271*W + 0.166565447888*X + -0.187684284734*Z)/2
 *   = 0.288397341271*W + 0.166565447888*X + -0.187684284734*Z
 * D = Left/2 - Right/2
 *   = (0.288397341271*W + 0.333238912931*Y + 0.166565447888*X + -0.187684284734*Z + j(0.444008050325*W + -0.256439256487*X))/2
 *     - (0.288397341271*W + -0.333238912931*Y + 0.166565447888*X + -0.187684284734*Z + j(-0.444008050325*W + 0.256439256487*X))/2
 *   = (0.288397341271*W + 0.333238912931*Y + 0.166565447888*X + -0.187684284734*Z + j(0.444008050325*W + -0.256439256487*X))/2
 *     + (-0.288397341271*W + 0.333238912931*Y + -0.166565447888*X + 0.187684284734*Z + j(0.444008050325*W + -0.256439256487*X))/2
 *   = (0.333238912931*Y + j(0.444008050325*W + -0.256439256487*X))/2
 *     + (0.333238912931*Y + j(0.444008050325*W + -0.256439256487*X))/2
 *   = 0.333238912931*Y + j(0.444008050325*W + -0.256439256487*X)
 *
 * So finally, we get:
 *
 * S = 0.288397341271*W + 0.166565447888*X - 0.187684284734*Z
 * D = j(0.444008050325*W - 0.256439256487*X) + 0.333238912931*Y
 *
 * Left  = S + D
 * Right = S - D
 */

template<usize N>
void TsmeEncoder<N>::encode(const std::span<float> LeftOut, const std::span<float> RightOut,
    const std::span<const std::span<const float>> InSamples)
{
    static_assert(sFftLength == gSegmentedFilter<N>.sFftLength);
    static_assert(sSegmentSize == gSegmentedFilter<N>.sSampleLength);
    static_assert(sNumSegments == gSegmentedFilter<N>.sNumSegments);

    const auto samplesToDo = InSamples[0].size();
    const auto winput = assume_aligned_span<16>(InSamples[0]);
    const auto yinput = assume_aligned_span<16>(InSamples[1].first(samplesToDo));
    const auto zinput = assume_aligned_span<16>(InSamples[2].first(samplesToDo));
    const auto xinput = assume_aligned_span<16>(InSamples[3].first(samplesToDo));

    std::ranges::copy(winput, std::next(mW.begin(), sFilterDelay));
    std::ranges::copy(yinput, std::next(mY.begin(), sFilterDelay));
    std::ranges::copy(zinput, std::next(mZ.begin(), sFilterDelay));
    std::ranges::copy(xinput, std::next(mX.begin(), sFilterDelay));

    /* S = 0.288397341271*W + 0.166565447888*X - 0.187684284734*Z */
    std::ranges::transform(mW | std::views::take(samplesToDo), mX, mS.begin(),
        [](const float w, const float x) { return 0.288397341271f*w + 0.166565447888f*x; });
    std::ranges::transform(mS | std::views::take(samplesToDo), mZ, mS.begin(),
        [](const float wx, const float z) { return wx - 0.187684284734f*z; });

    /* Precompute j(0.444008050325*W - 0.256439256487*X) and store in mD. */
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
        { return 0.444008050325f*w - 0.256439256487f*x; });

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

    /* D = j(0.444008050325*W - 0.256439256487*X) + 0.333238912931*Y */
    std::ranges::transform(mD | std::views::take(samplesToDo), mY, mD.begin(),
        [](const float jwx, const float y) noexcept { return jwx + 0.333238912931f*y; });

    /* Copy the future samples to the front for next time. */
    const auto take_end = std::views::drop(samplesToDo) | std::views::take(sFilterDelay);
    std::ranges::copy(mW | take_end, mW.begin());
    std::ranges::copy(mY | take_end, mY.begin());
    std::ranges::copy(mZ | take_end, mZ.begin());
    std::ranges::copy(mX | take_end, mX.begin());

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

    /* Left = S + D */
    const auto left = assume_aligned_span<16>(LeftOut);
    for(auto i = 0_uz;i < samplesToDo;++i)
        left[i] += mS[i] + mD[i];

    /* Right = S - D */
    const auto right = assume_aligned_span<16>(RightOut);
    for(auto i = 0_uz;i < samplesToDo;++i)
        right[i] += mS[i] - mD[i];
}

/* This encoding implementation uses two sets of four chained IIR filters to
 * produce the desired relative phase shift. See uhjfilter.cpp for more
 * details.
 */
void TsmeEncoderIIR::encode(const std::span<float> LeftOut, const std::span<float> RightOut,
    const std::span<const std::span<const float>> InSamples)
{
    const auto samplesToDo = InSamples[0].size();
    const auto winput = assume_aligned_span<16>(InSamples[0]);
    const auto yinput = assume_aligned_span<16>(InSamples[1].first(samplesToDo));
    const auto zinput = assume_aligned_span<16>(InSamples[2].first(samplesToDo));
    const auto xinput = assume_aligned_span<16>(InSamples[3].first(samplesToDo));

    /* S = 0.288397341271*W + 0.166565447888*X - 0.187684284734*Z */
    std::ranges::transform(winput, xinput, mTemp.begin(),
        [](const float w, const float x) { return 0.288397341271f*w + 0.166565447888f*x; });
    std::ranges::transform(mTemp, zinput, mTemp.begin(),
        [](const float wx, const float z) { return wx - 0.187684284734f*z; });
    process(mFilter1WXZ, Filter1Coeff, std::span{mTemp}.first(samplesToDo), true,
        std::span{mS}.subspan(1));
    mS[0] = mDelayWXZ; mDelayWXZ = mS[samplesToDo];

    /* Precompute j(0.444008050325*W - 0.256439256487*X) and store in mWX. */
    std::ranges::transform(winput, xinput, mTemp.begin(),
        [](const float w, const float x) { return 0.444008050325f*w - 0.256439256487f*x; });
    process(mFilter2WX, Filter2Coeff, std::span{mTemp}.first(samplesToDo), true, mWX);

    /* Apply filter1 to Y and store in mD. */
    process(mFilter1Y, Filter1Coeff, yinput, true, std::span{mD}.subspan(1));
    mD[0] = mDelayY; mDelayY = mD[samplesToDo];

    /* D = j(0.444008050325*W - 0.256439256487*X) + 0.333238912931*Y */
    std::ranges::transform(mWX | std::views::take(samplesToDo), mD, mD.begin(),
        [](const float jwx, const float y) noexcept { return jwx + 0.333238912931f*y; });

    /* Apply the base filter to the existing output to align with the processed
     * signal.
     */
    const auto left = assume_aligned_span<16>(LeftOut.first(samplesToDo));
    process(mFilter1Direct[0], Filter1Coeff, left, true, std::span{mTemp}.subspan(1));
    mTemp[0] = mDirectDelay[0]; mDirectDelay[0] = mTemp[samplesToDo];

    /* Left = S + D */
    for(auto i = 0_uz;i < samplesToDo;++i)
        left[i] = mS[i] + mD[i] + mTemp[i];

    const auto right = assume_aligned_span<16>(RightOut.first(samplesToDo));
    process(mFilter1Direct[1], Filter1Coeff, right, true, std::span{mTemp}.subspan(1));
    mTemp[0] = mDirectDelay[1]; mDirectDelay[1] = mTemp[samplesToDo];

    /* Right = S - D */
    for(auto i = 0_uz;i < samplesToDo;++i)
        right[i] = mS[i] - mD[i] + mTemp[i];
}

template struct TsmeEncoder<TsmeLength256>;

template struct TsmeEncoder<TsmeLength512>;
