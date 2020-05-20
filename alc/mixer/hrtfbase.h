#ifndef MIXER_HRTFBASE_H
#define MIXER_HRTFBASE_H

#include <algorithm>

#include "alu.h"
#include "../hrtf.h"
#include "opthelpers.h"
#include "voice.h"


using ApplyCoeffsT = void(&)(float2 *RESTRICT Values, const uint_fast32_t irSize,
    const HrirArray &Coeffs, const float left, const float right);

template<ApplyCoeffsT ApplyCoeffs>
inline void MixHrtfBase(const float *InSamples, float2 *RESTRICT AccumSamples, const ALuint IrSize,
    const MixHrtfFilter *hrtfparams, const size_t BufferSize)
{
    ASSUME(BufferSize > 0);

    const HrirArray &Coeffs = *hrtfparams->Coeffs;
    const float gainstep{hrtfparams->GainStep};
    const float gain{hrtfparams->Gain};

    size_t Delay[2]{
        HRTF_HISTORY_LENGTH - hrtfparams->Delay[0],
        HRTF_HISTORY_LENGTH - hrtfparams->Delay[1] };
    float stepcount{0.0f};
    for(size_t i{0u};i < BufferSize;++i)
    {
        const float g{gain + gainstep*stepcount};
        const float left{InSamples[Delay[0]++] * g};
        const float right{InSamples[Delay[1]++] * g};
        ApplyCoeffs(AccumSamples+i, IrSize, Coeffs, left, right);

        stepcount += 1.0f;
    }
}

template<ApplyCoeffsT ApplyCoeffs>
inline void MixHrtfBlendBase(const float *InSamples, float2 *RESTRICT AccumSamples,
    const ALuint IrSize, const HrtfFilter *oldparams, const MixHrtfFilter *newparams,
    const size_t BufferSize)
{
    const auto &OldCoeffs = oldparams->Coeffs;
    const float oldGain{oldparams->Gain};
    const float oldGainStep{-oldGain / static_cast<float>(BufferSize)};
    const auto &NewCoeffs = *newparams->Coeffs;
    const float newGainStep{newparams->GainStep};

    ASSUME(BufferSize > 0);

    size_t Delay[2]{
        HRTF_HISTORY_LENGTH - oldparams->Delay[0],
        HRTF_HISTORY_LENGTH - oldparams->Delay[1] };
    float stepcount{0.0f};
    for(size_t i{0u};i < BufferSize;++i)
    {
        const float g{oldGain + oldGainStep*stepcount};
        const float left{InSamples[Delay[0]++] * g};
        const float right{InSamples[Delay[1]++] * g};
        ApplyCoeffs(AccumSamples+i, IrSize, OldCoeffs, left, right);

        stepcount += 1.0f;
    }

    Delay[0] = HRTF_HISTORY_LENGTH - newparams->Delay[0];
    Delay[1] = HRTF_HISTORY_LENGTH - newparams->Delay[1];
    stepcount = 0.0f;
    for(size_t i{0u};i < BufferSize;++i)
    {
        const float g{newGainStep*stepcount};
        const float left{InSamples[Delay[0]++] * g};
        const float right{InSamples[Delay[1]++] * g};
        ApplyCoeffs(AccumSamples+i, IrSize, NewCoeffs, left, right);

        stepcount += 1.0f;
    }
}

template<ApplyCoeffsT ApplyCoeffs>
inline void MixDirectHrtfBase(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *RESTRICT AccumSamples,
    DirectHrtfState *State, const size_t BufferSize)
{
    ASSUME(BufferSize > 0);

    const uint_fast32_t IrSize{State->mIrSize};

    auto chan_iter = State->mChannels.begin();
    for(const FloatBufferLine &input : InSamples)
    {
        /* For dual-band processing, the signal needs extra scaling applied to
         * the high frequency response. The band-splitter alone creates a
         * frequency-dependent phase shift, which is not ideal. To counteract
         * it, combine it with a backwards phase shift.
         */

        /* Load the input signal backwards, into a temp buffer with delay
         * padding. The delay serves to reduce the error caused by the IIR
         * filter's phase shift on a partial input.
         */
        al::span<float> tempbuf{State->mTemp.data(), HRTF_DIRECT_DELAY+BufferSize};
        auto tmpiter = std::reverse_copy(input.begin(), input.begin()+BufferSize, tempbuf.begin());
        std::copy(chan_iter->mDelay.cbegin(), chan_iter->mDelay.cend(), tmpiter);

        /* Save the unfiltered newest input samples for next time. */
        std::copy_n(tempbuf.begin(), HRTF_DIRECT_DELAY, chan_iter->mDelay.begin());

        /* Apply the all-pass on the reversed signal and reverse the resulting
         * sample array. This produces the forward response with a backwards
         * phase shift (+n degrees becomes -n degrees).
         */
        chan_iter->mSplitter.applyAllpass(tempbuf);
        tempbuf = tempbuf.subspan<HRTF_DIRECT_DELAY>();
        std::reverse(tempbuf.begin(), tempbuf.end());

        /* Now apply the band-splitter. This applies the normal phase shift,
         * which cancels out with the backwards phase shift to get the original
         * phase on the scaled signal.
         */
        chan_iter->mSplitter.processHfScale(tempbuf, chan_iter->mHfScale);

        /* Now apply the HRIR coefficients to this channel. */
        const auto &Coeffs = chan_iter->mCoeffs;
        ++chan_iter;

        for(size_t i{0u};i < BufferSize;++i)
        {
            const float insample{tempbuf[i]};
            ApplyCoeffs(AccumSamples+i, IrSize, Coeffs, insample, insample);
        }
    }

    /* Apply a delay to the existing signal to align with the input delay. */
    auto &ldelay = State->mLeftDelay;
    auto &rdelay = State->mRightDelay;
    if LIKELY(BufferSize >= HRTF_DIRECT_DELAY)
    {
        auto buffer_end = LeftOut.begin() + BufferSize;
        auto delay_end = std::rotate(LeftOut.begin(), buffer_end - HRTF_DIRECT_DELAY, buffer_end);
        std::swap_ranges(LeftOut.begin(), delay_end, ldelay.begin());

        buffer_end = RightOut.begin() + BufferSize;
        delay_end = std::rotate(RightOut.begin(), buffer_end - HRTF_DIRECT_DELAY, buffer_end);
        std::swap_ranges(RightOut.begin(), delay_end, rdelay.begin());
    }
    else
    {
        auto buffer_end = LeftOut.begin() + BufferSize;
        auto delay_start = std::swap_ranges(LeftOut.begin(), buffer_end, ldelay.begin());
        std::rotate(ldelay.begin(), delay_start, ldelay.end());

        buffer_end = RightOut.begin() + BufferSize;
        delay_start = std::swap_ranges(RightOut.begin(), buffer_end, rdelay.begin());
        std::rotate(rdelay.begin(), delay_start, rdelay.end());
    }
    for(size_t i{0u};i < BufferSize;++i)
        LeftOut[i]  += AccumSamples[i][0];
    for(size_t i{0u};i < BufferSize;++i)
        RightOut[i] += AccumSamples[i][1];

    /* Copy the new in-progress accumulation values to the front and clear the
     * following samples for the next mix.
     */
    auto accum_iter = std::copy_n(AccumSamples+BufferSize, HRIR_LENGTH, AccumSamples);
    std::fill_n(accum_iter, BufferSize, float2{});
}

#endif /* MIXER_HRTFBASE_H */
