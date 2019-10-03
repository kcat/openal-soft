#ifndef MIXER_HRTFBASE_H
#define MIXER_HRTFBASE_H

#include <algorithm>

#include "alu.h"
#include "../hrtf.h"
#include "opthelpers.h"
#include "voice.h"


using ApplyCoeffsT = void(&)(float2 *RESTRICT Values, const ALuint irSize, const HrirArray &Coeffs,
    const float left, const float right);

template<ApplyCoeffsT ApplyCoeffs>
inline void MixHrtfBase(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const float *InSamples, float2 *RESTRICT AccumSamples, const size_t OutPos,
    const ALuint IrSize, MixHrtfFilter *hrtfparams, const size_t BufferSize)
{
    ASSUME(BufferSize > 0);

    const HrirArray &Coeffs = *hrtfparams->Coeffs;
    const float gainstep{hrtfparams->GainStep};
    const float gain{hrtfparams->Gain};

    ALsizei Delay[2]{
        HRTF_HISTORY_LENGTH - hrtfparams->Delay[0],
        HRTF_HISTORY_LENGTH - hrtfparams->Delay[1] };
    ASSUME(Delay[0] >= 0 && Delay[1] >= 0);
    float stepcount{0.0f};
    for(size_t i{0u};i < BufferSize;++i)
    {
        const float g{gain + gainstep*stepcount};
        const float left{InSamples[Delay[0]++] * g};
        const float right{InSamples[Delay[1]++] * g};
        ApplyCoeffs(AccumSamples+i, IrSize, Coeffs, left, right);

        stepcount += 1.0f;
    }

    for(size_t i{0u};i < BufferSize;++i)
        LeftOut[OutPos+i]  += AccumSamples[i][0];
    for(size_t i{0u};i < BufferSize;++i)
        RightOut[OutPos+i] += AccumSamples[i][1];

    hrtfparams->Gain = gain + gainstep*stepcount;
}

template<ApplyCoeffsT ApplyCoeffs>
inline void MixHrtfBlendBase(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const float *InSamples, float2 *RESTRICT AccumSamples, const size_t OutPos,
    const ALuint IrSize, const HrtfFilter *oldparams, MixHrtfFilter *newparams,
    const size_t BufferSize)
{
    const auto &OldCoeffs = oldparams->Coeffs;
    const float oldGain{oldparams->Gain};
    const float oldGainStep{-oldGain / static_cast<float>(BufferSize)};
    const auto &NewCoeffs = *newparams->Coeffs;
    const float newGainStep{newparams->GainStep};

    ASSUME(BufferSize > 0);

    ALsizei Delay[2]{
        HRTF_HISTORY_LENGTH - oldparams->Delay[0],
        HRTF_HISTORY_LENGTH - oldparams->Delay[1] };
    ASSUME(Delay[0] >= 0 && Delay[1] >= 0);
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
    ASSUME(Delay[0] >= 0 && Delay[1] >= 0);
    stepcount = 0.0f;
    for(size_t i{0u};i < BufferSize;++i)
    {
        const float g{newGainStep*stepcount};
        const float left{InSamples[Delay[0]++] * g};
        const float right{InSamples[Delay[1]++] * g};
        ApplyCoeffs(AccumSamples+i, IrSize, NewCoeffs, left, right);

        stepcount += 1.0f;
    }

    for(size_t i{0u};i < BufferSize;++i)
        LeftOut[OutPos+i]  += AccumSamples[i][0];
    for(size_t i{0u};i < BufferSize;++i)
        RightOut[OutPos+i] += AccumSamples[i][1];

    newparams->Gain = newGainStep*stepcount;
}

template<ApplyCoeffsT ApplyCoeffs>
inline void MixDirectHrtfBase(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *RESTRICT AccumSamples,
    DirectHrtfState *State, const size_t BufferSize)
{
    ASSUME(BufferSize > 0);

    const ALuint IrSize{State->IrSize};

    auto accum_iter = std::copy_n(State->Values.begin(), State->Values.size(), AccumSamples);
    std::fill_n(accum_iter, BufferSize, float2{});

    auto coeff_iter = State->Coeffs.begin();
    for(const FloatBufferLine &input : InSamples)
    {
        const auto &Coeffs = *(coeff_iter++);
        for(size_t i{0u};i < BufferSize;++i)
        {
            const float insample{input[i]};
            ApplyCoeffs(AccumSamples+i, IrSize, Coeffs, insample, insample);
        }
    }
    for(size_t i{0u};i < BufferSize;++i)
        LeftOut[i]  += AccumSamples[i][0];
    for(size_t i{0u};i < BufferSize;++i)
        RightOut[i] += AccumSamples[i][1];

    std::copy_n(AccumSamples + BufferSize, State->Values.size(), State->Values.begin());
}

#endif /* MIXER_HRTFBASE_H */
