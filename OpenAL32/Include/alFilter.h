#ifndef _AL_FILTER_H_
#define _AL_FILTER_H_

#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float *history; /* pointer to history in filter */
    float *coef;    /* pointer to coefficients of filter */
} FILTER;

#define AL_FILTER_TYPE                                     0x8001

#define AL_FILTER_NULL                                     0x0000
#define AL_FILTER_LOWPASS                                  0x0001
#define AL_FILTER_HIGHPASS                                 0x0002
#define AL_FILTER_BANDPASS                                 0x0003

#define AL_LOWPASS_GAIN                                    0x0001
#define AL_LOWPASS_GAINHF                                  0x0002


typedef struct ALfilter_struct
{
    // Filter type (AL_FILTER_NULL, ...)
    ALenum type;

    ALfloat Gain;
    ALfloat GainHF;

    // Index to itself
    ALuint filter;

    struct ALfilter_struct *next;
} ALfilter;

AL_API ALvoid AL_APIENTRY alGenFilters(ALsizei n, ALuint *filters);
AL_API ALvoid AL_APIENTRY alDeleteFilters(ALsizei n, ALuint *filters);
AL_API ALboolean AL_APIENTRY alIsFilter(ALuint filter);

AL_API ALvoid AL_APIENTRY alFilteri(ALuint filter, ALenum param, ALint iValue);
AL_API ALvoid AL_APIENTRY alFilteriv(ALuint filter, ALenum param, ALint *piValues);
AL_API ALvoid AL_APIENTRY alFilterf(ALuint filter, ALenum param, ALfloat flValue);
AL_API ALvoid AL_APIENTRY alFilterfv(ALuint filter, ALenum param, ALfloat *pflValues);

AL_API ALvoid AL_APIENTRY alGetFilteri(ALuint filter, ALenum param, ALint *piValue);
AL_API ALvoid AL_APIENTRY alGetFilteriv(ALuint filter, ALenum param, ALint *piValues);
AL_API ALvoid AL_APIENTRY alGetFilterf(ALuint filter, ALenum param, ALfloat *pflValue);
AL_API ALvoid AL_APIENTRY alGetFilterfv(ALuint filter, ALenum param, ALfloat *pflValues);

ALvoid ReleaseALFilters(ALvoid);

#define FILTER_SECTIONS 2   /* 2 filter sections for 24 db/oct filter */

int InitLowPassFilter(ALCcontext *Context, FILTER *iir);

#ifdef __cplusplus
}
#endif

#endif
