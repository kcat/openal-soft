#ifndef CORE_MIXER_HRTFBASE_H
#define CORE_MIXER_HRTFBASE_H

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>

#include "defs.h"
#include "hrtfdefs.h"
#include "opthelpers.h"


using uint = unsigned int;

using ApplyCoeffsT = void(const std::span<float2> Values, const size_t irSize,
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
    for(size_t i{0u};i < SamplesToDo;++i)
    {
        const float g{gain + gainstep*stepcount};
        const float left{InSamples[ldelay++] * g};
        const float right{InSamples[rdelay++] * g};
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

    const ConstHrirSpan OldCoeffs{oldparams->Coeffs};
    const float oldGainStep{oldparams->Gain / static_cast<float>(SamplesToDo)};
    const ConstHrirSpan NewCoeffs{newparams->Coeffs};
    const float newGainStep{newparams->GainStep};

    if(oldparams->Gain > GainSilenceThreshold) [[likely]]
    {
        size_t ldelay{HrtfHistoryLength - oldparams->Delay[0]};
        size_t rdelay{HrtfHistoryLength - oldparams->Delay[1]};
        auto stepcount = static_cast<float>(SamplesToDo);
        for(size_t i{0u};i < SamplesToDo;++i)
        {
            const float g{oldGainStep*stepcount};
            const float left{InSamples[ldelay++] * g};
            const float right{InSamples[rdelay++] * g};
            ApplyCoeffs(AccumSamples.subspan(i), IrSize, OldCoeffs, left, right);

            stepcount -= 1.0f;
        }
    }

    if(newGainStep*static_cast<float>(SamplesToDo) > GainSilenceThreshold) [[likely]]
    {
        size_t ldelay{HrtfHistoryLength+1 - newparams->Delay[0]};
        size_t rdelay{HrtfHistoryLength+1 - newparams->Delay[1]};
        float stepcount{1.0f};
        for(size_t i{1u};i < SamplesToDo;++i)
        {
            const float g{newGainStep*stepcount};
            const float left{InSamples[ldelay++] * g};
            const float right{InSamples[rdelay++] * g};
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

    auto ChanState = ChannelState.begin();
    for(const FloatBufferLine &input : InSamples)
    {
        /* For dual-band processing, the signal needs extra scaling applied to
         * the high frequency response. The band-splitter applies this scaling
         * with a consistent phase shift regardless of the scale amount.
         */
        ChanState->mSplitter.processHfScale(std::span{input}.first(SamplesToDo), TempBuf,
            ChanState->mHfScale);

        /* Now apply the HRIR coefficients to this channel. */
        const ConstHrirSpan Coeffs{ChanState->mCoeffs};
        for(size_t i{0u};i < SamplesToDo;++i)
        {
            const float insample{TempBuf[i]};
            ApplyCoeffs(AccumSamples.subspan(i), IrSize, Coeffs, insample, insample);
        }

        ++ChanState;
    }

    /* Add the HRTF signal to the existing "direct" signal. */
    const auto left = std::span{std::assume_aligned<16>(LeftOut.data()), SamplesToDo};
    std::transform(left.begin(), left.end(), AccumSamples.begin(), left.begin(),
        [](const float sample, const float2 &accum) noexcept -> float
        { return sample + accum[0]; });
    const auto right = std::span{std::assume_aligned<16>(RightOut.data()), SamplesToDo};
    std::transform(right.begin(), right.end(), AccumSamples.begin(), right.begin(),
        [](const float sample, const float2 &accum) noexcept -> float
        { return sample + accum[1]; });

    /* Copy the new in-progress accumulation values to the front and clear the
     * following samples for the next mix.
     */
    const auto accum_inprog = AccumSamples.subspan(SamplesToDo, HrirLength);
    auto accum_iter = std::copy(accum_inprog.begin(), accum_inprog.end(), AccumSamples.begin());
    std::fill_n(accum_iter, SamplesToDo, float2{});
}

#endif /* CORE_MIXER_HRTFBASE_H */
