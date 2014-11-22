#include "config.h"

#include "alMain.h"
#include "alSource.h"

#include "hrtf.h"
#include "mixer_defs.h"
#include "align.h"


#define REAL_MERGE(a,b) a##b
#define MERGE(a,b) REAL_MERGE(a,b)

#define MixHrtf MERGE(MixHrtf_,SUFFIX)


static inline void ApplyCoeffs(ALuint Offset, ALfloat (*restrict Values)[2],
                               const ALuint irSize,
                               ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right);


void MixHrtf(ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *data,
             ALuint Offset, const ALuint IrSize,
             const HrtfParams *hrtfparams, HrtfState *hrtfstate, ALuint BufferSize)
{
    alignas(16) ALfloat Coeffs[HRIR_LENGTH][2];
    ALuint Delay[2];
    ALfloat left, right;
    ALuint pos;
    ALuint c;

    for(c = 0;c < IrSize;c++)
    {
        Coeffs[c][0] = hrtfparams->Coeffs[c][0];
        Coeffs[c][1] = hrtfparams->Coeffs[c][1];
    }
    Delay[0] = hrtfparams->Delay[0];
    Delay[1] = hrtfparams->Delay[1];

    for(pos = 0;pos < BufferSize;pos++)
    {
        hrtfstate->History[Offset&HRTF_HISTORY_MASK] = data[pos];
        left = hrtfstate->History[(Offset-Delay[0])&HRTF_HISTORY_MASK];
        right = hrtfstate->History[(Offset-Delay[1])&HRTF_HISTORY_MASK];

        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
        hrtfstate->Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
        Offset++;

        ApplyCoeffs(Offset, hrtfstate->Values, IrSize, Coeffs, left, right);
        OutBuffer[0][pos] += hrtfstate->Values[Offset&HRIR_MASK][0];
        OutBuffer[1][pos] += hrtfstate->Values[Offset&HRIR_MASK][1];
    }
}


#undef MixHrtf

#undef MERGE
#undef REAL_MERGE
