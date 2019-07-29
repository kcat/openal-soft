#ifndef MIXER_HRTFBASE_H
#define MIXER_HRTFBASE_H

#include <algorithm>

#include "alu.h"
#include "../hrtf.h"
#include "opthelpers.h"


using ApplyCoeffsT = void(ALsizei Offset, float2 *RESTRICT Values, const ALsizei irSize,
    const HrirArray<ALfloat> &Coeffs, const ALfloat left, const ALfloat right);

template<ApplyCoeffsT &ApplyCoeffs>
inline void MixHrtfBase(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *RESTRICT AccumSamples, const ALsizei OutPos,
    const ALsizei IrSize, MixHrtfFilter *hrtfparams, const ALsizei BufferSize)
{
    ASSUME(OutPos >= 0);
    ASSUME(IrSize >= 4);
    ASSUME(BufferSize > 0);

    const auto &Coeffs = *hrtfparams->Coeffs;
    const ALfloat gainstep{hrtfparams->GainStep};
    const ALfloat gain{hrtfparams->Gain};

    ALsizei Delay[2]{
        HRTF_HISTORY_LENGTH - hrtfparams->Delay[0],
        HRTF_HISTORY_LENGTH - hrtfparams->Delay[1] };
    ASSUME(Delay[0] >= 0 && Delay[1] >= 0);
    ALfloat stepcount{0.0f};
    for(ALsizei i{0};i < BufferSize;++i)
    {
        const ALfloat g{gain + gainstep*stepcount};
        const ALfloat left{InSamples[Delay[0]++] * g};
        const ALfloat right{InSamples[Delay[1]++] * g};
        ApplyCoeffs(i, AccumSamples+i, IrSize, Coeffs, left, right);

        stepcount += 1.0f;
    }

    for(ALsizei i{0};i < BufferSize;++i)
        LeftOut[OutPos+i]  += AccumSamples[i][0];
    for(ALsizei i{0};i < BufferSize;++i)
        RightOut[OutPos+i] += AccumSamples[i][1];

    hrtfparams->Gain = gain + gainstep*stepcount;
}

template<ApplyCoeffsT &ApplyCoeffs>
inline void MixHrtfBlendBase(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *RESTRICT AccumSamples, const ALsizei OutPos,
    const ALsizei IrSize, const HrtfFilter *oldparams, MixHrtfFilter *newparams,
    const ALsizei BufferSize)
{
    const auto &OldCoeffs = oldparams->Coeffs;
    const ALfloat oldGain{oldparams->Gain};
    const ALfloat oldGainStep{-oldGain / static_cast<ALfloat>(BufferSize)};
    const auto &NewCoeffs = *newparams->Coeffs;
    const ALfloat newGainStep{newparams->GainStep};

    ASSUME(OutPos >= 0);
    ASSUME(IrSize >= 4);
    ASSUME(BufferSize > 0);

    ALsizei Delay[2]{
        HRTF_HISTORY_LENGTH - oldparams->Delay[0],
        HRTF_HISTORY_LENGTH - oldparams->Delay[1] };
    ASSUME(Delay[0] >= 0 && Delay[1] >= 0);
    ALfloat stepcount{0.0f};
    for(ALsizei i{0};i < BufferSize;++i)
    {
        const ALfloat g{oldGain + oldGainStep*stepcount};
        const ALfloat left{InSamples[Delay[0]++] * g};
        const ALfloat right{InSamples[Delay[1]++] * g};
        ApplyCoeffs(i, AccumSamples+i, IrSize, OldCoeffs, left, right);

        stepcount += 1.0f;
    }

    Delay[0] = HRTF_HISTORY_LENGTH - newparams->Delay[0];
    Delay[1] = HRTF_HISTORY_LENGTH - newparams->Delay[1];
    ASSUME(Delay[0] >= 0 && Delay[1] >= 0);
    stepcount = 0.0f;
    for(ALsizei i{0};i < BufferSize;++i)
    {
        const ALfloat g{newGainStep*stepcount};
        const ALfloat left{InSamples[Delay[0]++] * g};
        const ALfloat right{InSamples[Delay[1]++] * g};
        ApplyCoeffs(i, AccumSamples+i, IrSize, NewCoeffs, left, right);

        stepcount += 1.0f;
    }

    for(ALsizei i{0};i < BufferSize;++i)
        LeftOut[OutPos+i]  += AccumSamples[i][0];
    for(ALsizei i{0};i < BufferSize;++i)
        RightOut[OutPos+i] += AccumSamples[i][1];

    newparams->Gain = newGainStep*stepcount;
}

template<ApplyCoeffsT &ApplyCoeffs>
inline void MixDirectHrtfBase(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *RESTRICT AccumSamples,
    DirectHrtfState *State, const ALsizei BufferSize)
{
    ASSUME(BufferSize > 0);

    const ALsizei IrSize{State->IrSize};
    ASSUME(IrSize >= 4);

    auto chanstate = State->Chan.begin();
    for(const FloatBufferLine &input : InSamples)
    {
        const auto &Coeffs = chanstate->Coeffs;

        auto accum_iter = std::copy_n(chanstate->Values.begin(),
            chanstate->Values.size(), AccumSamples);
        std::fill_n(accum_iter, BufferSize, float2{});

        for(ALsizei i{0};i < BufferSize;++i)
        {
            const ALfloat insample{input[i]};
            ApplyCoeffs(i, AccumSamples+i, IrSize, Coeffs, insample, insample);
        }
        for(ALsizei i{0};i < BufferSize;++i)
            LeftOut[i]  += AccumSamples[i][0];
        for(ALsizei i{0};i < BufferSize;++i)
            RightOut[i] += AccumSamples[i][1];

        std::copy_n(AccumSamples + BufferSize, chanstate->Values.size(),
            chanstate->Values.begin());
        ++chanstate;
    }
}

#endif /* MIXER_HRTFBASE_H */
