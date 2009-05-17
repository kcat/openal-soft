#ifndef _ALU_H_
#define _ALU_H_

#include "AL/al.h"
#include "AL/alc.h"

#ifdef HAVE_FLOAT_H
#include <float.h>
#endif

#ifndef M_PI
#define M_PI           3.14159265358979323846  /* pi */
#define M_PI_2         1.57079632679489661923  /* pi/2 */
#endif

#ifdef HAVE_SQRTF
#define aluSqrt(x) ((ALfloat)sqrtf((float)(x)))
#else
#define aluSqrt(x) ((ALfloat)sqrt((double)(x)))
#endif

#ifdef HAVE_ACOSF
#define aluAcos(x) ((ALfloat)acosf((float)(x)))
#else
#define aluAcos(x) ((ALfloat)acos((double)(x)))
#endif

#ifdef HAVE_ATANF
#define aluAtan(x) ((ALfloat)atanf((float)(x)))
#else
#define aluAtan(x) ((ALfloat)atan((double)(x)))
#endif

#ifdef HAVE_FABSF
#define aluFabs(x) ((ALfloat)fabsf((float)(x)))
#else
#define aluFabs(x) ((ALfloat)fabs((double)(x)))
#endif

// fixes for mingw32.
#if defined(max) && !defined(__max)
#define __max max
#endif
#if defined(min) && !defined(__min)
#define __min min
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FRONT_LEFT = 0,
    FRONT_RIGHT,
    FRONT_CENTER,
    SIDE_LEFT,
    SIDE_RIGHT,
    BACK_LEFT,
    BACK_RIGHT,
    BACK_CENTER,
    LFE,

    OUTPUTCHANNELS
};

#define BUFFERSIZE 24000

extern ALboolean DuplicateStereo;

__inline ALuint aluBytesFromFormat(ALenum format);
__inline ALuint aluChannelsFromFormat(ALenum format);
ALvoid aluInitPanning(ALCcontext *Context);
ALvoid aluMixData(ALCcontext *context,ALvoid *buffer,ALsizei size,ALenum format);

#ifdef __cplusplus
}
#endif

#endif

