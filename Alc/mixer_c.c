#include "config.h"

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"


static __inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                     ALfloat (*RESTRICT Coeffs)[2],
                                     ALfloat (*RESTRICT CoeffStep)[2],
                                     ALfloat left, ALfloat right)
{
    ALuint c;
    for(c = 0;c < HRIR_LENGTH;c++)
    {
        const ALuint off = (Offset+c)&HRIR_MASK;
        Values[off][0] += Coeffs[c][0] * left;
        Values[off][1] += Coeffs[c][1] * right;
        Coeffs[c][0] += CoeffStep[c][0];
        Coeffs[c][1] += CoeffStep[c][1];
    }
}

static __inline void ApplyCoeffs(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                 ALfloat (*RESTRICT Coeffs)[2],
                                 ALfloat left, ALfloat right)
{
    ALuint c;
    for(c = 0;c < HRIR_LENGTH;c++)
    {
        const ALuint off = (Offset+c)&HRIR_MASK;
        Values[off][0] += Coeffs[c][0] * left;
        Values[off][1] += Coeffs[c][1] * right;
    }
}


static __inline void ApplyValue(ALfloat *RESTRICT Output, ALfloat value, const ALfloat *DrySend)
{
    ALuint c;
    for(c = 0;c < MaxChannels;c++)
        Output[c] += value*DrySend[c];
}


#define SUFFIX C
#define Sampler point32
#include "mixer_inc.c"
#undef Sampler
#define Sampler lerp32
#include "mixer_inc.c"
#undef Sampler
#define Sampler cubic32
#include "mixer_inc.c"
#undef Sampler
#undef SUFFIX
