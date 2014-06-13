#include "config.h"

#include "alMain.h"
#include "alSource.h"

#include "hrtf.h"
#include "mixer_defs.h"
#include "align.h"


#define REAL_MERGE(a,b) a##b
#define MERGE(a,b) REAL_MERGE(a,b)

#define MixHrtf MERGE(MixHrtf_,SUFFIX)


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
             const HrtfParams *hrtfparams, HrtfState *hrtfstate, ALuint BufferSize)
{
    alignas(16) ALfloat Coeffs[HRIR_LENGTH][2];
    ALuint Delay[2];
    ALfloat left, right;
    ALuint pos;
    ALuint c;

    for(c = 0;c < IrSize;c++)
    {
        Coeffs[c][0] = hrtfparams->Coeffs[c][0] - (hrtfparams->CoeffStep[c][0]*Counter);
        Coeffs[c][1] = hrtfparams->Coeffs[c][1] - (hrtfparams->CoeffStep[c][1]*Counter);
    }
    Delay[0] = hrtfparams->Delay[0] - (hrtfparams->DelayStep[0]*Counter);
    Delay[1] = hrtfparams->Delay[1] - (hrtfparams->DelayStep[1]*Counter);

    for(pos = 0;pos < BufferSize && pos < Counter;pos++)
    {
        hrtfstate->History[Offset&SRC_HISTORY_MASK] = data[pos];
        left  = lerp(hrtfstate->History[(Offset-(Delay[0]>>HRTFDELAY_BITS))&SRC_HISTORY_MASK],
                     hrtfstate->History[(Offset-(Delay[0]>>HRTFDELAY_BITS)-1)&SRC_HISTORY_MASK],
                     (Delay[0]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));
        right = lerp(hrtfstate->History[(Offset-(Delay[1]>>HRTFDELAY_BITS))&SRC_HISTORY_MASK],
                     hrtfstate->History[(Offset-(Delay[1]>>HRTFDELAY_BITS)-1)&SRC_HISTORY_MASK],
                     (Delay[1]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));

        Delay[0] += hrtfparams->DelayStep[0];
        Delay[1] += hrtfparams->DelayStep[1];

        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
        Offset++;

        ApplyCoeffsStep(Offset, hrtfstate->Values, IrSize, Coeffs, hrtfparams->CoeffStep, left, right);
        OutBuffer[FrontLeft][OutPos]  += hrtfstate->Values[Offset&HRIR_MASK][0];
        OutBuffer[FrontRight][OutPos] += hrtfstate->Values[Offset&HRIR_MASK][1];
        OutPos++;
    }

    Delay[0] >>= HRTFDELAY_BITS;
    Delay[1] >>= HRTFDELAY_BITS;
    for(;pos < BufferSize;pos++)
    {
        hrtfstate->History[Offset&SRC_HISTORY_MASK] = data[pos];
        left = hrtfstate->History[(Offset-Delay[0])&SRC_HISTORY_MASK];
        right = hrtfstate->History[(Offset-Delay[1])&SRC_HISTORY_MASK];

        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
        Offset++;

        ApplyCoeffs(Offset, hrtfstate->Values, IrSize, Coeffs, left, right);
        OutBuffer[FrontLeft][OutPos]  += hrtfstate->Values[Offset&HRIR_MASK][0];
        OutBuffer[FrontRight][OutPos] += hrtfstate->Values[Offset&HRIR_MASK][1];

        OutPos++;
    }
}


#undef MixHrtf

#undef MERGE
#undef REAL_MERGE
