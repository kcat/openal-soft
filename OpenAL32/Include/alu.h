#ifndef _ALU_H_
#define _ALU_H_

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include <limits.h>
#include <math.h>
#ifdef HAVE_FLOAT_H
#include <float.h>
#endif
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

#ifndef M_PI
#define M_PI           3.14159265358979323846  /* pi */
#define M_PI_2         1.57079632679489661923  /* pi/2 */
#endif

#ifdef HAVE_POWF
#define aluPow(x,y) (powf((x),(y)))
#else
#define aluPow(x,y) ((ALfloat)pow((double)(x),(double)(y)))
#endif

#ifdef HAVE_SQRTF
#define aluSqrt(x) (sqrtf((x)))
#else
#define aluSqrt(x) ((ALfloat)sqrt((double)(x)))
#endif

#ifdef HAVE_ACOSF
#define aluAcos(x) (acosf((x)))
#else
#define aluAcos(x) ((ALfloat)acos((double)(x)))
#endif

#ifdef HAVE_ATANF
#define aluAtan(x) (atanf((x)))
#else
#define aluAtan(x) ((ALfloat)atan((double)(x)))
#endif

#ifdef HAVE_FABSF
#define aluFabs(x) (fabsf((x)))
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

#define QUADRANT_NUM  128
#define LUT_NUM       (4 * QUADRANT_NUM)

#ifdef __cplusplus
extern "C" {
#endif

struct ALsource;
struct ALbuffer;

typedef ALvoid (*MixerFunc)(struct ALsource *self, ALCdevice *Device,
                            const ALvoid *RESTRICT data,
                            ALuint *DataPosInt, ALuint *DataPosFrac,
                            ALuint OutPos, ALuint SamplesToDo,
                            ALuint BufferSize);

enum Resampler {
    POINT_RESAMPLER = 0,
    LINEAR_RESAMPLER,
    CUBIC_RESAMPLER,

    RESAMPLER_MAX,
    RESAMPLER_MIN = -1,
    RESAMPLER_DEFAULT = LINEAR_RESAMPLER
};

enum Channel {
    FRONT_LEFT = 0,
    FRONT_RIGHT,
    FRONT_CENTER,
    LFE,
    BACK_LEFT,
    BACK_RIGHT,
    BACK_CENTER,
    SIDE_LEFT,
    SIDE_RIGHT,

    MAXCHANNELS
};

enum DistanceModel {
    InverseDistanceClamped  = AL_INVERSE_DISTANCE_CLAMPED,
    LinearDistanceClamped   = AL_LINEAR_DISTANCE_CLAMPED,
    ExponentDistanceClamped = AL_EXPONENT_DISTANCE_CLAMPED,
    InverseDistance  = AL_INVERSE_DISTANCE,
    LinearDistance   = AL_LINEAR_DISTANCE,
    ExponentDistance = AL_EXPONENT_DISTANCE,
    DisableDistance  = AL_NONE
};

#define BUFFERSIZE 4096

#define FRACTIONBITS (14)
#define FRACTIONONE  (1<<FRACTIONBITS)
#define FRACTIONMASK (FRACTIONONE-1)

/* Size for temporary stack storage of buffer data. Larger values need more
 * stack, while smaller values may need more iterations. The value needs to be
 * a sensible size, however, as it constrains the max stepping value used for
 * mixing.
 * The mixer requires being able to do two samplings per mixing loop. A 16KB
 * buffer can hold 512 sample frames for a 7.1 float buffer. With the cubic
 * resampler (which requires 3 padding sample frames), this limits the maximum
 * step to about 508. This means that buffer_freq*source_pitch cannot exceed
 * device_freq*508 for an 8-channel 32-bit buffer. */
#ifndef STACK_DATA_SIZE
#define STACK_DATA_SIZE  16384
#endif


static __inline ALfloat minF(ALfloat a, ALfloat b)
{ return ((a > b) ? b : a); }
static __inline ALfloat maxF(ALfloat a, ALfloat b)
{ return ((a > b) ? a : b); }
static __inline ALfloat clampF(ALfloat val, ALfloat mn, ALfloat mx)
{ return minF(mx, maxF(mn, val)); }


static __inline ALdouble lerp(ALdouble val1, ALdouble val2, ALdouble mu)
{
    return val1 + (val2-val1)*mu;
}
static __inline ALdouble cubic(ALdouble val0, ALdouble val1, ALdouble val2, ALdouble val3, ALdouble mu)
{
    ALdouble mu2 = mu*mu;
    ALdouble a0 = -0.5*val0 +  1.5*val1 + -1.5*val2 +  0.5*val3;
    ALdouble a1 =      val0 + -2.5*val1 +  2.0*val2 + -0.5*val3;
    ALdouble a2 = -0.5*val0             +  0.5*val2;
    ALdouble a3 =                  val1;

    return a0*mu*mu2 + a1*mu2 + a2*mu + a3;
}

ALvoid aluInitPanning(ALCdevice *Device);
ALint aluCart2LUTpos(ALfloat re, ALfloat im);

ALvoid CalcSourceParams(struct ALsource *ALSource, const ALCcontext *ALContext);
ALvoid CalcNonAttnSourceParams(struct ALsource *ALSource, const ALCcontext *ALContext);

MixerFunc SelectMixer(struct ALbuffer *Buffer, enum Resampler Resampler);
MixerFunc SelectHrtfMixer(struct ALbuffer *Buffer, enum Resampler Resampler);

ALvoid MixSource(struct ALsource *Source, ALCdevice *Device, ALuint SamplesToDo);

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size);
ALvoid aluHandleDisconnect(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif

