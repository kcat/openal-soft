#ifndef CORE_MIXER_HRTFBASE_H
#define CORE_MIXER_HRTFBASE_H

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <ranges>

#include "alnumeric.h"
#include "defs.h"
#include "hrtfdefs.h"
#include "opthelpers.h"


using uint = unsigned int;

using ApplyCoeffsT = void(*)(const std::span<float2> Values, const size_t irSize,
    const ConstHrirSpan Coeffs, const float left, const float right);

template<ApplyCoeffsT ApplyCoeffs>
inline void MixHrtfBase(const std::span<const float> InSamples,
    const std::span<float2> AccumSamples, const size_t IrSize, const MixHrtfFilter *hrtfparams,
    const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);
    ASSUME(IrSize <= HrirLength);

    const auto Coeffs = std::span{hrtfparams->Coeffs};
    const auto gainstep = hrtfparams->GainStep;
    const auto gain = hrtfparams->Gain;

    auto ldelay = size_t{HrtfHistoryLength} - hrtfparams->Delay[0];
    auto rdelay = size_t{HrtfHistoryLength} - hrtfparams->Delay[1];
    auto stepcount = 0.0f;
    for(auto i = 0_uz;i < SamplesToDo;++i)
    {
        const auto g = gain + gainstep*stepcount;
        const auto left = InSamples[ldelay++] * g;
        const auto right = InSamples[rdelay++] * g;
        ApplyCoeffs(AccumSamples.subspan(i), IrSize, Coeffs, left, right);

        stepcount += 1.0f;
    }
}

template<ApplyCoeffsT ApplyCoeffs>
inline void MixHrtfBlendBase(const std::span<const float> InSamples,
    const std::span<float2> AccumSamples, const size_t IrSize, const HrtfFilter *oldparams,
    const MixHrtfFilter *newparams, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);
    ASSUME(IrSize <= HrirLength);

    const auto OldCoeffs = ConstHrirSpan{oldparams->Coeffs};
    const auto oldGainStep = oldparams->Gain / static_cast<float>(SamplesToDo);
    const auto NewCoeffs = ConstHrirSpan{newparams->Coeffs};
    const auto newGainStep = newparams->GainStep;

    if(oldparams->Gain > GainSilenceThreshold) [[likely]]
    {
        auto ldelay = size_t{HrtfHistoryLength} - oldparams->Delay[0];
        auto rdelay = size_t{HrtfHistoryLength} - oldparams->Delay[1];
        auto stepcount = static_cast<float>(SamplesToDo);
        for(auto i = 0_uz;i < SamplesToDo;++i)
        {
            const auto g = oldGainStep*stepcount;
            const auto left = InSamples[ldelay++] * g;
            const auto right = InSamples[rdelay++] * g;
            ApplyCoeffs(AccumSamples.subspan(i), IrSize, OldCoeffs, left, right);

            stepcount -= 1.0f;
        }
    }

    if(newGainStep*static_cast<float>(SamplesToDo) > GainSilenceThreshold) [[likely]]
    {
        auto ldelay = size_t{HrtfHistoryLength}+1 - newparams->Delay[0];
        auto rdelay = size_t{HrtfHistoryLength}+1 - newparams->Delay[1];
        auto stepcount = 1.0f;
        for(auto i = 1_uz;i < SamplesToDo;++i)
        {
            const auto g = newGainStep*stepcount;
            const auto left = InSamples[ldelay++] * g;
            const auto right = InSamples[rdelay++] * g;
            ApplyCoeffs(AccumSamples.subspan(i), IrSize, NewCoeffs, left, right);

            stepcount += 1.0f;
        }
    }
}

template<ApplyCoeffsT ApplyCoeffs>
inline void MixDirectHrtfBase(const FloatBufferSpan LeftOut, const FloatBufferSpan RightOut,
    const std::span<const FloatBufferLine> InSamples, const std::span<float2> AccumSamples,
    const std::span<float,BufferLineSize> TempBuf, const std::span<HrtfChannelState> ChannelState,
    const size_t IrSize, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);
    ASSUME(IrSize <= HrirLength);
    assert(ChannelState.size() == InSamples.size());

    std::ignore = std::ranges::mismatch(InSamples, ChannelState,
        [&](const FloatConstBufferSpan input, HrtfChannelState &ChanState)
    {
        /* For dual-band processing, the signal needs extra scaling applied to
         * the high frequency response. The band-splitter applies this scaling
         * with a consistent phase shift regardless of the scale amount.
         */
        ChanState.mSplitter.processHfScale(std::span{input}.first(SamplesToDo), TempBuf,
            ChanState.mHfScale);

        /* Now apply the HRIR coefficients to this channel. */
        const auto Coeffs = ConstHrirSpan{ChanState.mCoeffs};
        for(auto i = 0_uz;i < SamplesToDo;++i)
        {
            const auto insample = TempBuf[i];
            ApplyCoeffs(AccumSamples.subspan(i), IrSize, Coeffs, insample, insample);
        }
        return true;
    });

    /* Add the HRTF signal to the existing "direct" signal. */
    std::ranges::transform(LeftOut | std::views::take(SamplesToDo),
        AccumSamples | std::views::elements<0>, LeftOut.begin(), std::plus{});
    std::ranges::transform(RightOut | std::views::take(SamplesToDo),
        AccumSamples | std::views::elements<1>, RightOut.begin(), std::plus{});

    /* Copy the new in-progress accumulation values to the front and clear the
     * following samples for the next mix.
     */
    const auto accum_inprog = AccumSamples.subspan(SamplesToDo, HrirLength);
    auto accum_iter = std::ranges::copy(accum_inprog, AccumSamples.begin()).out;
    std::fill_n(accum_iter, SamplesToDo, float2{});
}

#endif /* CORE_MIXER_HRTFBASE_H */
