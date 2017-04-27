#include "config.h"

#include "alMain.h"
#include "alSource.h"

#include "hrtf.h"
#include "mixer_defs.h"
#include "align.h"
#include "alu.h"


static inline void ApplyCoeffs(ALsizei Offset, ALfloat (*restrict Values)[2],
                               const ALsizei irSize,
                               const ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right);


void MixHrtf(ALfloat *restrict LeftOut, ALfloat *restrict RightOut,
             const ALfloat *data, ALsizei Offset, ALsizei OutPos,
             const ALsizei IrSize, MixHrtfParams *hrtfparams, HrtfState *hrtfstate,
             ALsizei BufferSize)
{
    const ALfloat (*Coeffs)[2] = ASSUME_ALIGNED(hrtfparams->Coeffs, 16);
    const ALsizei Delay[2] = { hrtfparams->Delay[0], hrtfparams->Delay[1] };
    ALfloat gainstep = hrtfparams->GainStep;
    ALfloat gain = hrtfparams->Gain;
    ALfloat left, right;
    ALsizei i;

    LeftOut  += OutPos;
    RightOut += OutPos;
    for(i = 0;i < BufferSize;i++)
    {
        hrtfstate->History[Offset&HRTF_HISTORY_MASK] = *(data++);
        left = hrtfstate->History[(Offset-Delay[0])&HRTF_HISTORY_MASK];
        right = hrtfstate->History[(Offset-Delay[1])&HRTF_HISTORY_MASK];

        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
        Offset++;

        ApplyCoeffs(Offset, hrtfstate->Values, IrSize, Coeffs, left, right);
        *(LeftOut++)  += hrtfstate->Values[Offset&HRIR_MASK][0]*gain;
        *(RightOut++) += hrtfstate->Values[Offset&HRIR_MASK][1]*gain;
        gain += gainstep;
    }
    hrtfparams->Gain = gain;
}

void MixDirectHrtf(ALfloat *restrict LeftOut, ALfloat *restrict RightOut,
                   const ALfloat *data, ALsizei Offset, const ALsizei IrSize,
                   const ALfloat (*restrict Coeffs)[2], ALfloat (*restrict Values)[2],
                   ALsizei BufferSize)
{
    ALfloat insample;
    ALsizei i;

    for(i = 0;i < BufferSize;i++)
    {
        Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
        Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
        Offset++;

        insample = *(data++);
        ApplyCoeffs(Offset, Values, IrSize, Coeffs, insample, insample);
        *(LeftOut++)  += Values[Offset&HRIR_MASK][0];
        *(RightOut++) += Values[Offset&HRIR_MASK][1];
    }
}
