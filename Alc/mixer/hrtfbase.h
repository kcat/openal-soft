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
    const ALsizei IrSize, MixHrtfParams *hrtfparams, const ALsizei BufferSize)
{
    ASSUME(OutPos >= 0);
    ASSUME(IrSize >= 4);
    ASSUME(BufferSize > 0);

    const auto &Coeffs = *hrtfparams->Coeffs;
    const ALfloat gainstep{hrtfparams->GainStep};
    const ALfloat gain{hrtfparams->Gain};
    ALfloat stepcount{0.0f};

    ALsizei Delay[2]{
        HRTF_HISTORY_LENGTH - hrtfparams->Delay[0],
        HRTF_HISTORY_LENGTH - hrtfparams->Delay[1] };
    ASSUME(Delay[0] >= 0 && Delay[1] >= 0);

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
    const ALsizei IrSize, const HrtfParams *oldparams, MixHrtfParams *newparams,
    const ALsizei BufferSize)
{
    const auto &OldCoeffs = oldparams->Coeffs;
    const ALfloat oldGain{oldparams->Gain};
    const ALfloat oldGainStep{-oldGain / static_cast<ALfloat>(BufferSize)};
    const auto &NewCoeffs = *newparams->Coeffs;
    const ALfloat newGainStep{newparams->GainStep};
    ALfloat stepcount{0.0f};

    ASSUME(OutPos >= 0);
    ASSUME(IrSize >= 4);
    ASSUME(BufferSize > 0);

    ALsizei OldDelay[2]{
        HRTF_HISTORY_LENGTH - oldparams->Delay[0],
        HRTF_HISTORY_LENGTH - oldparams->Delay[1] };
    ASSUME(OldDelay[0] >= 0 && OldDelay[1] >= 0);
    ALsizei NewDelay[2]{
        HRTF_HISTORY_LENGTH - newparams->Delay[0],
        HRTF_HISTORY_LENGTH - newparams->Delay[1] };
    ASSUME(NewDelay[0] >= 0 && NewDelay[1] >= 0);

    for(ALsizei i{0};i < BufferSize;++i)
    {
        ALfloat g{oldGain + oldGainStep*stepcount};
        ALfloat left{InSamples[OldDelay[0]++] * g};
        ALfloat right{InSamples[OldDelay[1]++] * g};
        ApplyCoeffs(i, AccumSamples+i, IrSize, OldCoeffs, left, right);

        g = newGainStep*stepcount;
        left = InSamples[NewDelay[0]++] * g;
        right = InSamples[NewDelay[1]++] * g;
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
    const FloatBufferLine *InSamples, float2 *RESTRICT AccumSamples, DirectHrtfState *State,
    const ALsizei NumChans, const ALsizei BufferSize)
{
    ASSUME(NumChans > 0);
    ASSUME(BufferSize > 0);

    const ALsizei IrSize{State->IrSize};
    ASSUME(IrSize >= 4);

    for(ALsizei c{0};c < NumChans;++c)
    {
        const FloatBufferLine &input = InSamples[c];
        const auto &Coeffs = State->Chan[c].Coeffs;

        auto accum_iter = std::copy_n(State->Chan[c].Values.begin(),
            State->Chan[c].Values.size(), AccumSamples);
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

        std::copy_n(AccumSamples + BufferSize, State->Chan[c].Values.size(),
            State->Chan[c].Values.begin());
    }
}

#endif /* MIXER_HRTFBASE_H */
