#include "config.h"

#include "alMain.h"
#include "alSource.h"

#include "hrtf.h"
#include "mixer_defs.h"
#include "align.h"
#include "alu.h"


#define MAX_UPDATE_SAMPLES 128


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
    ALsizei pos, i;

    for(pos = 0;pos < BufferSize;)
    {
        ALfloat out[MAX_UPDATE_SAMPLES][2];
        ALsizei todo = mini(BufferSize-pos, MAX_UPDATE_SAMPLES);

        for(i = 0;i < todo;i++)
        {
            hrtfstate->History[Offset&HRTF_HISTORY_MASK] = data[pos++];
            left = hrtfstate->History[(Offset-Delay[0])&HRTF_HISTORY_MASK];
            right = hrtfstate->History[(Offset-Delay[1])&HRTF_HISTORY_MASK];

            hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
            hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
            Offset++;

            ApplyCoeffs(Offset, hrtfstate->Values, IrSize, Coeffs, left, right);
            out[i][0] = hrtfstate->Values[Offset&HRIR_MASK][0]*gain;
            out[i][1] = hrtfstate->Values[Offset&HRIR_MASK][1]*gain;
            gain += gainstep;
        }

        for(i = 0;i < todo;i++)
            LeftOut[OutPos+i] += out[i][0];
        for(i = 0;i < todo;i++)
            RightOut[OutPos+i] += out[i][1];
        OutPos += todo;
    }
    hrtfparams->Gain = gain;
}

void MixDirectHrtf(ALfloat *restrict LeftOut, ALfloat *restrict RightOut,
                   const ALfloat *data, ALsizei Offset, const ALsizei IrSize,
                   const ALfloat (*restrict Coeffs)[2], ALfloat (*restrict Values)[2],
                   ALsizei BufferSize)
{
    ALfloat out[MAX_UPDATE_SAMPLES][2];
    ALfloat insample;
    ALsizei pos, i;

    for(pos = 0;pos < BufferSize;)
    {
        ALsizei todo = mini(BufferSize-pos, MAX_UPDATE_SAMPLES);

        for(i = 0;i < todo;i++)
        {
            Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
            Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
            Offset++;

            insample = *(data++);
            ApplyCoeffs(Offset, Values, IrSize, Coeffs, insample, insample);
            out[i][0] = Values[Offset&HRIR_MASK][0];
            out[i][1] = Values[Offset&HRIR_MASK][1];
        }

        for(i = 0;i < todo;i++)
            LeftOut[pos+i] += out[i][0];
        for(i = 0;i < todo;i++)
            RightOut[pos+i] += out[i][1];
        pos += todo;
    }
}
