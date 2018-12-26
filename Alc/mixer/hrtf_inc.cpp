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
             const ALfloat *data, ALsizei Offset, ALsizei OutPos,
             const ALsizei IrSize, MixHrtfParams *hrtfparams, HrtfState *hrtfstate,
             ALsizei BufferSize)
{
    const ALfloat (&Coeffs)[HRIR_LENGTH][2] = *hrtfparams->Coeffs;
    const ALsizei Delay[2] = { hrtfparams->Delay[0], hrtfparams->Delay[1] };
    const ALfloat gainstep = hrtfparams->GainStep;
    const ALfloat gain = hrtfparams->Gain;
    ALfloat g, stepcount = 0.0f;
    ALfloat left, right;
    ALsizei i;

    ASSUME(IrSize >= 4);
    ASSUME(BufferSize > 0);

    LeftOut  += OutPos;
    RightOut += OutPos;
    for(i = 0;i < BufferSize;i++)
    {
        hrtfstate->History[Offset&HRTF_HISTORY_MASK] = *(data++);

        g = gain + gainstep*stepcount;
        left = hrtfstate->History[(Offset-Delay[0])&HRTF_HISTORY_MASK]*g;
        right = hrtfstate->History[(Offset-Delay[1])&HRTF_HISTORY_MASK]*g;

        hrtfstate->Values[(Offset+IrSize-1)&HRIR_MASK][0] = 0.0f;
        hrtfstate->Values[(Offset+IrSize-1)&HRIR_MASK][1] = 0.0f;

        ApplyCoeffs(Offset, hrtfstate->Values, IrSize, Coeffs, left, right);
        *(LeftOut++)  += hrtfstate->Values[Offset&HRIR_MASK][0];
        *(RightOut++) += hrtfstate->Values[Offset&HRIR_MASK][1];

        stepcount += 1.0f;
        Offset++;
    }
    hrtfparams->Gain = gain + gainstep*stepcount;
}

void MixHrtfBlend(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                  const ALfloat *data, ALsizei Offset, ALsizei OutPos,
                  const ALsizei IrSize, const HrtfParams *oldparams,
                  MixHrtfParams *newparams, HrtfState *hrtfstate,
                  ALsizei BufferSize)
{
    const ALfloat (&OldCoeffs)[HRIR_LENGTH][2] = oldparams->Coeffs;
    const ALsizei OldDelay[2] = { oldparams->Delay[0], oldparams->Delay[1] };
    const ALfloat oldGain = oldparams->Gain;
    const ALfloat oldGainStep = -oldGain / (ALfloat)BufferSize;
    const ALfloat (&NewCoeffs)[HRIR_LENGTH][2] = *newparams->Coeffs;
    const ALsizei NewDelay[2] = { newparams->Delay[0], newparams->Delay[1] };
    const ALfloat newGain = newparams->Gain;
    const ALfloat newGainStep = newparams->GainStep;
    ALfloat g, stepcount = 0.0f;
    ALfloat left, right;
    ALsizei i;

    ASSUME(IrSize >= 4);
    ASSUME(BufferSize > 0);

    LeftOut  += OutPos;
    RightOut += OutPos;
    for(i = 0;i < BufferSize;i++)
    {
        hrtfstate->Values[(Offset+IrSize-1)&HRIR_MASK][0] = 0.0f;
        hrtfstate->Values[(Offset+IrSize-1)&HRIR_MASK][1] = 0.0f;

        hrtfstate->History[Offset&HRTF_HISTORY_MASK] = *(data++);

        g = oldGain + oldGainStep*stepcount;
        left = hrtfstate->History[(Offset-OldDelay[0])&HRTF_HISTORY_MASK]*g;
        right = hrtfstate->History[(Offset-OldDelay[1])&HRTF_HISTORY_MASK]*g;
        ApplyCoeffs(Offset, hrtfstate->Values, IrSize, OldCoeffs, left, right);

        g = newGain + newGainStep*stepcount;
        left = hrtfstate->History[(Offset-NewDelay[0])&HRTF_HISTORY_MASK]*g;
        right = hrtfstate->History[(Offset-NewDelay[1])&HRTF_HISTORY_MASK]*g;
        ApplyCoeffs(Offset, hrtfstate->Values, IrSize, NewCoeffs, left, right);

        *(LeftOut++)  += hrtfstate->Values[Offset&HRIR_MASK][0];
        *(RightOut++) += hrtfstate->Values[Offset&HRIR_MASK][1];

        stepcount += 1.0f;
        Offset++;
    }
    newparams->Gain = newGain + newGainStep*stepcount;
}

void MixDirectHrtf(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                   const ALfloat *data, DirectHrtfState *State, const ALsizei Chan,
                   const ALsizei BufferSize)
{
    const ALsizei IrSize{State->IrSize};
    ALsizei Offset{State->Offset};
    ALfloat (&Values)[HRIR_LENGTH][2] = State->Chan[Chan].Values;
    const ALfloat (&Coeffs)[HRIR_LENGTH][2] = State->Chan[Chan].Coeffs;

    ASSUME(IrSize >= 4);
    ASSUME(BufferSize > 0);

    for(ALsizei i{0};i < BufferSize;i++)
    {
        Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
        Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
        Offset++;

        const ALfloat insample{*(data++)};
        ApplyCoeffs(Offset, Values, IrSize, Coeffs, insample, insample);
        *(LeftOut++)  += Values[Offset&HRIR_MASK][0];
        *(RightOut++) += Values[Offset&HRIR_MASK][1];
    }
}
