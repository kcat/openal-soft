#include "config.h"

#include "alMain.h"
#include "alSource.h"

#include "hrtf.h"
#include "mixer_defs.h"
#include "align.h"
#include "alu.h"


static inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*restrict Values)[2],
                                   const ALuint irSize,
                                   ALfloat (*restrict Coeffs)[2],
                                   const ALfloat (*restrict CoeffStep)[2],
                                   ALfloat left, ALfloat right);
static inline void ApplyCoeffs(ALuint Offset, ALfloat (*restrict Values)[2],
                               const ALuint irSize,
                               ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right);


void MixHrtf(ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *data,
             ALuint Counter, ALuint Offset, ALuint OutPos, const ALuint IrSize,
             const MixHrtfParams *hrtfparams, HrtfState *hrtfstate, ALuint BufferSize)
{
    ALfloat (*Coeffs)[2] = hrtfparams->Current->Coeffs;
    ALuint Delay[2] = { hrtfparams->Current->Delay[0], hrtfparams->Current->Delay[1] };
    ALfloat left, right;
    ALuint pos;

    pos = 0;
    if(pos < Counter)
    {
        for(;pos < BufferSize && pos < Counter;pos++)
        {
            hrtfstate->History[Offset&HRTF_HISTORY_MASK] = data[pos];
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
            OutBuffer[0][OutPos] += hrtfstate->Values[Offset&HRIR_MASK][0];
            OutBuffer[1][OutPos] += hrtfstate->Values[Offset&HRIR_MASK][1];
            OutPos++;
        }

        if(pos == Counter)
        {
            *hrtfparams->Current = *hrtfparams->Target;
            Delay[0] = hrtfparams->Target->Delay[0];
            Delay[1] = hrtfparams->Target->Delay[1];
        }
    }

    Delay[0] >>= HRTFDELAY_BITS;
    Delay[1] >>= HRTFDELAY_BITS;
    for(;pos < BufferSize;pos++)
    {
        hrtfstate->History[Offset&HRTF_HISTORY_MASK] = data[pos];
        left = hrtfstate->History[(Offset-Delay[0])&HRTF_HISTORY_MASK];
        right = hrtfstate->History[(Offset-Delay[1])&HRTF_HISTORY_MASK];

        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
        Offset++;

        ApplyCoeffs(Offset, hrtfstate->Values, IrSize, Coeffs, left, right);
        OutBuffer[0][OutPos] += hrtfstate->Values[Offset&HRIR_MASK][0];
        OutBuffer[1][OutPos] += hrtfstate->Values[Offset&HRIR_MASK][1];
        OutPos++;
    }
}
