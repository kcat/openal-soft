#include "config.h"

#include "alMain.h"
#include "alSource.h"

#include "hrtf.h"
#include "alu.h"
#include "defs.h"


static inline void ApplyCoeffs(ALsizei Offset, ALfloat (&Values)[HRIR_LENGTH][2],
                               const ALsizei irSize, const ALfloat (&Coeffs)[HRIR_LENGTH][2],
                               const ALfloat left, const ALfloat right);


void MixHrtf(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
             const ALfloat *data, ALsizei Offset, const ALsizei OutPos,
             const ALsizei IrSize, MixHrtfParams *hrtfparams, HrtfState *hrtfstate,
             const ALsizei BufferSize)
{
    ASSUME(OutPos >= 0);
    ASSUME(IrSize >= 4);
    ASSUME(BufferSize > 0);

    const ALfloat (&Coeffs)[HRIR_LENGTH][2] = *hrtfparams->Coeffs;
    const ALfloat gainstep{hrtfparams->GainStep};
    const ALfloat gain{hrtfparams->Gain};
    ALfloat stepcount{0.0f};

    ALsizei HistOffset{Offset&HRTF_HISTORY_MASK};
    ALsizei Delay[2]{
        (HistOffset-hrtfparams->Delay[0])&HRTF_HISTORY_MASK,
        (HistOffset-hrtfparams->Delay[1])&HRTF_HISTORY_MASK };

    Offset &= HRIR_MASK;
    ALsizei HeadOffset{(Offset+IrSize-1)&HRIR_MASK};

    LeftOut  += OutPos;
    RightOut += OutPos;
    for(ALsizei i{0};i < BufferSize;)
    {
        /* Calculate the number of samples we can do until one of the indices
         * wraps on its buffer, or we reach the end.
         */
        const ALsizei todo_hist{HRTF_HISTORY_LENGTH - maxi(maxi(HistOffset, Delay[0]), Delay[1])};
        const ALsizei todo_hrir{HRIR_LENGTH - maxi(HeadOffset, Offset)};
        const ALsizei todo{mini(BufferSize-i, mini(todo_hist, todo_hrir)) + i};
        ASSUME(todo > i);

        for(;i < todo;++i)
        {
            hrtfstate->Values[HeadOffset][0] = 0.0f;
            hrtfstate->Values[HeadOffset][1] = 0.0f;
            ++HeadOffset;

            hrtfstate->History[HistOffset++] = *(data++);

            const ALfloat g{gain + gainstep*stepcount};
            const ALfloat left{hrtfstate->History[Delay[0]++] * g};
            const ALfloat right{hrtfstate->History[Delay[1]++] * g};
            ApplyCoeffs(Offset, hrtfstate->Values, IrSize, Coeffs, left, right);

            *(LeftOut++)  += hrtfstate->Values[Offset][0];
            *(RightOut++) += hrtfstate->Values[Offset][1];
            ++Offset;

            stepcount += 1.0f;
        }

        HeadOffset &= HRIR_MASK;
        HistOffset &= HRTF_HISTORY_MASK;
        Delay[0] &= HRTF_HISTORY_MASK;
        Delay[1] &= HRTF_HISTORY_MASK;
        Offset &= HRIR_MASK;
    }
    hrtfparams->Gain = gain + gainstep*stepcount;
}

void MixHrtfBlend(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                  const ALfloat *data, ALsizei Offset, const ALsizei OutPos,
                  const ALsizei IrSize, const HrtfParams *oldparams,
                  MixHrtfParams *newparams, HrtfState *hrtfstate,
                  const ALsizei BufferSize)
{
    const ALfloat (&OldCoeffs)[HRIR_LENGTH][2] = oldparams->Coeffs;
    const ALfloat oldGain{oldparams->Gain};
    const ALfloat oldGainStep{-oldGain / (ALfloat)BufferSize};
    const ALfloat (&NewCoeffs)[HRIR_LENGTH][2] = *newparams->Coeffs;
    const ALfloat newGainStep{newparams->GainStep};
    ALfloat stepcount{0.0f};

    ASSUME(OutPos >= 0);
    ASSUME(IrSize >= 4);
    ASSUME(BufferSize > 0);

    ALsizei HistOffset{Offset&HRTF_HISTORY_MASK};
    ALsizei OldDelay[2]{
        (HistOffset-oldparams->Delay[0])&HRTF_HISTORY_MASK,
        (HistOffset-oldparams->Delay[1])&HRTF_HISTORY_MASK };
    ALsizei NewDelay[2]{
        (HistOffset-newparams->Delay[0])&HRTF_HISTORY_MASK,
        (HistOffset-newparams->Delay[1])&HRTF_HISTORY_MASK };

    Offset &= HRIR_MASK;
    ALsizei HeadOffset{(Offset+IrSize-1)&HRIR_MASK};

    LeftOut  += OutPos;
    RightOut += OutPos;
    for(ALsizei i{0};i < BufferSize;)
    {
        const ALsizei todo_hist{HRTF_HISTORY_LENGTH -
            maxi(maxi(maxi(maxi(HistOffset, OldDelay[0]), OldDelay[1]), NewDelay[0]), NewDelay[1])
        };
        const ALsizei todo_hrir{HRIR_LENGTH - maxi(HeadOffset, Offset)};
        const ALsizei todo{mini(BufferSize-i, mini(todo_hist, todo_hrir)) + i};
        ASSUME(todo > i);

        for(;i < todo;++i)
        {
            hrtfstate->Values[HeadOffset][0] = 0.0f;
            hrtfstate->Values[HeadOffset][1] = 0.0f;
            ++HeadOffset;

            hrtfstate->History[HistOffset++] = *(data++);

            ALfloat g{oldGain + oldGainStep*stepcount};
            ALfloat left{hrtfstate->History[OldDelay[0]++] * g};
            ALfloat right{hrtfstate->History[OldDelay[1]++] * g};
            ApplyCoeffs(Offset, hrtfstate->Values, IrSize, OldCoeffs, left, right);

            g = newGainStep*stepcount;
            left = hrtfstate->History[NewDelay[0]++] * g;
            right = hrtfstate->History[NewDelay[1]++] * g;
            ApplyCoeffs(Offset, hrtfstate->Values, IrSize, NewCoeffs, left, right);

            *(LeftOut++)  += hrtfstate->Values[Offset][0];
            *(RightOut++) += hrtfstate->Values[Offset][1];
            ++Offset;

            stepcount += 1.0f;
        }

        HeadOffset &= HRIR_MASK;
        HistOffset &= HRTF_HISTORY_MASK;
        OldDelay[0] &= HRTF_HISTORY_MASK;
        OldDelay[1] &= HRTF_HISTORY_MASK;
        NewDelay[0] &= HRTF_HISTORY_MASK;
        NewDelay[1] &= HRTF_HISTORY_MASK;
        Offset &= HRIR_MASK;
    }
    newparams->Gain = newGainStep*stepcount;
}

void MixDirectHrtf(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                   const ALfloat (*data)[BUFFERSIZE], DirectHrtfState *State,
                   const ALsizei NumChans, const ALsizei BufferSize)
{
    ASSUME(NumChans > 0);
    ASSUME(BufferSize > 0);

    const ALsizei IrSize{State->IrSize};
    ASSUME(IrSize >= 4);

    for(ALsizei c{0};c < NumChans;++c)
    {
        const ALfloat (&input)[BUFFERSIZE] = data[c];
        const ALfloat (&Coeffs)[HRIR_LENGTH][2] = State->Chan[c].Coeffs;
        ALfloat (&Values)[HRIR_LENGTH][2] = State->Chan[c].Values;
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
