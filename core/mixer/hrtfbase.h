#ifndef CORE_MIXER_HRTFBASE_H
#define CORE_MIXER_HRTFBASE_H

#include <algorithm>
#include <cmath>
#include <ranges>

#include "alnumeric.h"
#include "defs.h"
#include "gsl/gsl"
#include "hrtfdefs.h"
#include "opthelpers.h"


using ApplyCoeffsT = void(*)(std::span<f32x2> Values, usize irSize, ConstHrirSpan Coeffs,
    float left, float right);

template<ApplyCoeffsT ApplyCoeffs>
void MixHrtfBase(std::span<float const> const InSamples, std::span<f32x2> const AccumSamples,
    usize const IrSize, MixHrtfFilter const *const hrtfparams, usize const SamplesToDo)
{
    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);
    ASSUME(IrSize <= HrirLength);

    auto const Coeffs = std::span{hrtfparams->Coeffs};
    auto const gainstep = hrtfparams->GainStep;
    auto const gain = hrtfparams->Gain;

    auto ldelay = usize{HrtfHistoryLength} - hrtfparams->Delay[0];
    auto rdelay = usize{HrtfHistoryLength} - hrtfparams->Delay[1];
    auto stepcount = 0.0f;
    for(auto i = 0_uz;i < SamplesToDo;++i)
    {
        auto const g = gain + gainstep*stepcount;
        auto const left = InSamples[ldelay++] * g;
        auto const right = InSamples[rdelay++] * g;
        ApplyCoeffs(AccumSamples.subspan(i), IrSize, Coeffs, left, right);

        stepcount += 1.0f;
    }
}

template<ApplyCoeffsT ApplyCoeffs>
void MixHrtfBlendBase(std::span<float const> const InSamples, std::span<f32x2> const AccumSamples,
    usize const IrSize, HrtfFilter const *const oldparams, MixHrtfFilter const *const newparams,
    usize const SamplesToDo)
{
    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);
    ASSUME(IrSize <= HrirLength);

    auto const OldCoeffs = ConstHrirSpan{oldparams->Coeffs};
    auto const oldGainStep = oldparams->Gain / gsl::narrow_cast<float>(SamplesToDo);
    auto const NewCoeffs = ConstHrirSpan{newparams->Coeffs};
    auto const newGainStep = newparams->GainStep;

    if(oldparams->Gain > GainSilenceThreshold) [[likely]]
    {
        auto ldelay = usize{HrtfHistoryLength} - oldparams->Delay[0];
        auto rdelay = usize{HrtfHistoryLength} - oldparams->Delay[1];
        auto stepcount = gsl::narrow_cast<float>(SamplesToDo);
        for(auto i = 0_uz;i < SamplesToDo;++i)
        {
            auto const g = oldGainStep*stepcount;
            auto const left = InSamples[ldelay++] * g;
            auto const right = InSamples[rdelay++] * g;
            ApplyCoeffs(AccumSamples.subspan(i), IrSize, OldCoeffs, left, right);

            stepcount -= 1.0f;
        }
    }

    if(newGainStep*gsl::narrow_cast<float>(SamplesToDo) > GainSilenceThreshold) [[likely]]
    {
        auto ldelay = usize{HrtfHistoryLength+1} - newparams->Delay[0];
        auto rdelay = usize{HrtfHistoryLength+1} - newparams->Delay[1];
        auto stepcount = 1.0f;
        for(auto i = 1_uz;i < SamplesToDo;++i)
        {
            auto const g = newGainStep*stepcount;
            auto const left = InSamples[ldelay++] * g;
            auto const right = InSamples[rdelay++] * g;
            ApplyCoeffs(AccumSamples.subspan(i), IrSize, NewCoeffs, left, right);

            stepcount += 1.0f;
        }
    }
}

template<ApplyCoeffsT ApplyCoeffs>
void MixDirectHrtfBase(FloatBufferSpan const LeftOut, FloatBufferSpan const RightOut,
    std::span<FloatBufferLine const> const InSamples, std::span<f32x2> const AccumSamples,
    std::span<float, BufferLineSize> const TempBuf, std::span<HrtfChannelState> const ChannelState,
    usize const IrSize, usize const SamplesToDo)
{
    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);
    ASSUME(IrSize <= HrirLength);

    std::ignore = std::ranges::mismatch(InSamples, ChannelState,
        [&](FloatConstBufferSpan const input, HrtfChannelState &ChanState)
    {
        /* For dual-band processing, the signal needs extra scaling applied to
         * the high frequency response. The band-splitter applies this scaling
         * with a consistent phase shift regardless of the scale amount.
         */
        ChanState.mSplitter.processHfScale(std::span{input}.first(SamplesToDo), TempBuf,
            ChanState.mHfScale);

        /* Now apply the HRIR coefficients to this channel. */
        auto const Coeffs = ConstHrirSpan{ChanState.mCoeffs};
        for(auto i = 0_uz;i < SamplesToDo;++i)
        {
            auto const insample = TempBuf[i];
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
    auto const accum_inprog = AccumSamples.subspan(SamplesToDo, HrirLength);
    auto const accum_iter = std::ranges::copy(accum_inprog, AccumSamples.begin()).out;
    std::fill_n(accum_iter, SamplesToDo, f32x2{});
}

#endif /* CORE_MIXER_HRTFBASE_H */
