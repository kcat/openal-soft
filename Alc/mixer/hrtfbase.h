#ifndef MIXER_HRTFBASE_H
#define MIXER_HRTFBASE_H

#include "alu.h"
#include "../hrtf.h"
#include "opthelpers.h"


using ApplyCoeffsT = void(ALsizei Offset, HrirArray<ALfloat> &Values, const ALsizei irSize,
    const HrirArray<ALfloat> &Coeffs, const ALfloat left, const ALfloat right);

template<ApplyCoeffsT &ApplyCoeffs>
inline void MixHrtfBase(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut, const ALfloat *data,
    ALsizei Offset, const ALsizei OutPos, const ALsizei IrSize, MixHrtfParams *hrtfparams,
    HrtfState *hrtfstate, const ALsizei BufferSize)
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

    Offset &= HRIR_MASK;
    ALsizei HeadOffset{(Offset+IrSize-1)&HRIR_MASK};

    LeftOut  += OutPos;
    RightOut += OutPos;
    for(ALsizei i{0};i < BufferSize;)
    {
        /* Calculate the number of samples we can do until one of the indices
         * wraps on its buffer, or we reach the end.
         */
        const ALsizei todo_hrir{HRIR_LENGTH - maxi(HeadOffset, Offset)};
        const ALsizei todo{mini(BufferSize-i, todo_hrir) + i};
        ASSUME(todo > i);

        for(;i < todo;++i)
        {
            hrtfstate->Values[HeadOffset][0] = 0.0f;
            hrtfstate->Values[HeadOffset][1] = 0.0f;
            ++HeadOffset;

            const ALfloat g{gain + gainstep*stepcount};
            const ALfloat left{data[Delay[0]++] * g};
            const ALfloat right{data[Delay[1]++] * g};
            ApplyCoeffs(Offset, hrtfstate->Values, IrSize, Coeffs, left, right);

            *(LeftOut++)  += hrtfstate->Values[Offset][0];
            *(RightOut++) += hrtfstate->Values[Offset][1];
            ++Offset;

            stepcount += 1.0f;
        }

        HeadOffset &= HRIR_MASK;
        Offset &= HRIR_MASK;
    }
    hrtfparams->Gain = gain + gainstep*stepcount;
}

template<ApplyCoeffsT &ApplyCoeffs>
inline void MixHrtfBlendBase(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
    const ALfloat *data, ALsizei Offset, const ALsizei OutPos, const ALsizei IrSize,
    const HrtfParams *oldparams, MixHrtfParams *newparams, HrtfState *hrtfstate,
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

    Offset &= HRIR_MASK;
    ALsizei HeadOffset{(Offset+IrSize-1)&HRIR_MASK};

    LeftOut  += OutPos;
    RightOut += OutPos;
    for(ALsizei i{0};i < BufferSize;)
    {
        const ALsizei todo_hrir{HRIR_LENGTH - maxi(HeadOffset, Offset)};
        const ALsizei todo{mini(BufferSize-i, todo_hrir) + i};
        ASSUME(todo > i);

        for(;i < todo;++i)
        {
            hrtfstate->Values[HeadOffset][0] = 0.0f;
            hrtfstate->Values[HeadOffset][1] = 0.0f;
            ++HeadOffset;

            ALfloat g{oldGain + oldGainStep*stepcount};
            ALfloat left{data[OldDelay[0]++] * g};
            ALfloat right{data[OldDelay[1]++] * g};
            ApplyCoeffs(Offset, hrtfstate->Values, IrSize, OldCoeffs, left, right);

            g = newGainStep*stepcount;
            left = data[NewDelay[0]++] * g;
            right = data[NewDelay[1]++] * g;
            ApplyCoeffs(Offset, hrtfstate->Values, IrSize, NewCoeffs, left, right);

            *(LeftOut++)  += hrtfstate->Values[Offset][0];
            *(RightOut++) += hrtfstate->Values[Offset][1];
            ++Offset;

            stepcount += 1.0f;
        }

        HeadOffset &= HRIR_MASK;
        Offset &= HRIR_MASK;
    }
    newparams->Gain = newGainStep*stepcount;
}

template<ApplyCoeffsT &ApplyCoeffs>
inline void MixDirectHrtfBase(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
    const ALfloat (*data)[BUFFERSIZE], DirectHrtfState *State, const ALsizei NumChans,
    const ALsizei BufferSize)
{
    ASSUME(NumChans > 0);
    ASSUME(BufferSize > 0);

    const ALsizei IrSize{State->IrSize};
    ASSUME(IrSize >= 4);

    for(ALsizei c{0};c < NumChans;++c)
    {
        const ALfloat (&input)[BUFFERSIZE] = data[c];
        const auto &Coeffs = State->Chan[c].Coeffs;
        auto &Values = State->Chan[c].Values;
        ALsizei Offset{State->Offset&HRIR_MASK};

        ALsizei HeadOffset{(Offset+IrSize-1)&HRIR_MASK};
        for(ALsizei i{0};i < BufferSize;)
        {
            const ALsizei todo_hrir{HRIR_LENGTH - maxi(HeadOffset, Offset)};
            const ALsizei todo{mini(BufferSize-i, todo_hrir) + i};
            ASSUME(todo > i);

            for(;i < todo;++i)
            {
                Values[HeadOffset][0] = 0.0f;
                Values[HeadOffset][1] = 0.0f;
                ++HeadOffset;

                const ALfloat insample{input[i]};
                ApplyCoeffs(Offset, Values, IrSize, Coeffs, insample, insample);

                LeftOut[i]  += Values[Offset][0];
                RightOut[i] += Values[Offset][1];
                ++Offset;
            }
            HeadOffset &= HRIR_MASK;
            Offset &= HRIR_MASK;
        }
    }
}

#endif /* MIXER_HRTFBASE_H */
