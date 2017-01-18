#include "config.h"

#include "alMain.h"
#include "alSource.h"

#include "hrtf.h"
#include "mixer_defs.h"
#include "align.h"
#include "alu.h"


#define MAX_UPDATE_SAMPLES 128


static inline void ApplyCoeffsStep(ALsizei Offset, ALfloat (*restrict Values)[2],
                                   const ALsizei irSize,
                                   ALfloat (*restrict Coeffs)[2],
                                   const ALfloat (*restrict CoeffStep)[2],
                                   ALfloat left, ALfloat right);
static inline void ApplyCoeffs(ALsizei Offset, ALfloat (*restrict Values)[2],
                               const ALsizei irSize,
                               ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right);


void MixHrtf(ALfloat *restrict LeftOut, ALfloat *restrict RightOut,
             const ALfloat *data, ALsizei Counter, ALsizei Offset, ALsizei OutPos,
             const ALsizei IrSize, const MixHrtfParams *hrtfparams, HrtfState *hrtfstate,
             ALsizei BufferSize)
{
    ALfloat (*Coeffs)[2] = hrtfparams->Current->Coeffs;
    ALsizei Delay[2] = { hrtfparams->Current->Delay[0], hrtfparams->Current->Delay[1] };
    ALfloat out[MAX_UPDATE_SAMPLES][2];
    ALfloat left, right;
    ALsizei minsize;
    ALsizei pos, i;

    pos = 0;
    if(Counter == 0)
        goto skip_stepping;

    minsize = minu(BufferSize, Counter);
    while(pos < minsize)
    {
        ALsizei todo = mini(minsize-pos, MAX_UPDATE_SAMPLES);

        for(i = 0;i < todo;i++)
        {
            hrtfstate->History[Offset&HRTF_HISTORY_MASK] = data[pos++];
            left  = lerp(hrtfstate->History[(Offset-(Delay[0]>>HRTFDELAY_BITS))&HRTF_HISTORY_MASK],
                         hrtfstate->History[(Offset-(Delay[0]>>HRTFDELAY_BITS)-1)&HRTF_HISTORY_MASK],
                         (Delay[0]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));
            right = lerp(hrtfstate->History[(Offset-(Delay[1]>>HRTFDELAY_BITS))&HRTF_HISTORY_MASK],
                         hrtfstate->History[(Offset-(Delay[1]>>HRTFDELAY_BITS)-1)&HRTF_HISTORY_MASK],
                         (Delay[1]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));

            Delay[0] += hrtfparams->Steps.Delay[0];
            Delay[1] += hrtfparams->Steps.Delay[1];

            hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
            hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
            Offset++;

            ApplyCoeffsStep(Offset, hrtfstate->Values, IrSize, Coeffs, hrtfparams->Steps.Coeffs, left, right);
            out[i][0] = hrtfstate->Values[Offset&HRIR_MASK][0];
            out[i][1] = hrtfstate->Values[Offset&HRIR_MASK][1];
        }

        for(i = 0;i < todo;i++)
            LeftOut[OutPos+i] += out[i][0];
        for(i = 0;i < todo;i++)
            RightOut[OutPos+i] += out[i][1];
        OutPos += todo;
    }

    if(pos == Counter)
    {
        *hrtfparams->Current = *hrtfparams->Target;
        Delay[0] = hrtfparams->Target->Delay[0];
        Delay[1] = hrtfparams->Target->Delay[1];
    }
    else
    {
        hrtfparams->Current->Delay[0] = Delay[0];
        hrtfparams->Current->Delay[1] = Delay[1];
    }

skip_stepping:
    Delay[0] >>= HRTFDELAY_BITS;
    Delay[1] >>= HRTFDELAY_BITS;
    while(pos < BufferSize)
    {
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
            out[i][0] = hrtfstate->Values[Offset&HRIR_MASK][0];
            out[i][1] = hrtfstate->Values[Offset&HRIR_MASK][1];
        }

        for(i = 0;i < todo;i++)
            LeftOut[OutPos+i] += out[i][0];
        for(i = 0;i < todo;i++)
            RightOut[OutPos+i] += out[i][1];
        OutPos += todo;
    }
}

void MixDirectHrtf(ALfloat *restrict LeftOut, ALfloat *restrict RightOut,
                   const ALfloat *data, ALsizei Offset, const ALsizei IrSize,
                   ALfloat (*restrict Coeffs)[2], ALfloat (*restrict Values)[2],
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
