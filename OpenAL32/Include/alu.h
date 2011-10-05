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


#define F_PI    (3.14159265358979323846f)  /* pi */
#define F_PI_2  (1.57079632679489661923f)  /* pi/2 */

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

#ifdef HAVE_COSF
#define aluCos(x) (cosf((x)))
#else
#define aluCos(x) ((ALfloat)cos((double)(x)))
#endif

#ifdef HAVE_SINF
#define aluSin(x) (sinf((x)))
#else
#define aluSin(x) ((ALfloat)sin((double)(x)))
#endif

#ifdef HAVE_ACOSF
#define aluAcos(x) (acosf((x)))
#else
#define aluAcos(x) ((ALfloat)acos((double)(x)))
#endif

#ifdef HAVE_ASINF
#define aluAsin(x) (asinf((x)))
#else
#define aluAsin(x) ((ALfloat)asin((double)(x)))
#endif

#ifdef HAVE_ATANF
#define aluAtan(x) (atanf((x)))
#else
#define aluAtan(x) ((ALfloat)atan((double)(x)))
#endif

#ifdef HAVE_ATAN2F
#define aluAtan2(x,y) (atan2f((x),(y)))
#else
#define aluAtan2(x,y) ((ALfloat)atan2((double)(x),(double)(y)))
#endif

#ifdef HAVE_FABSF
#define aluFabs(x) (fabsf((x)))
#else
#define aluFabs(x) ((ALfloat)fabs((double)(x)))
#endif

#ifdef HAVE_LOG10F
#define aluLog10(x) (log10f((x)))
#else
#define aluLog10(x) ((ALfloat)log10((double)(x)))
#endif

#ifdef HAVE_FLOORF
#define aluFloor(x) (floorf((x)))
#else
#define aluFloor(x) ((ALfloat)floor((double)(x)))
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

/* Size for temporary stack storage of buffer data. Must be a multiple of the
 * size of ALfloat, ie, 4. Larger values need more stack, while smaller values
 * may need more iterations. The value needs to be a sensible size, however, as
 * it constrains the max stepping value used for mixing.
 * The mixer requires being able to do two samplings per mixing loop. A 16KB
 * buffer can hold 512 sample frames for a 7.1 float buffer. With the cubic
 * resampler (which requires 3 padding sample frames), this limits the maximum
 * step to about 508. This means that buffer_freq*source_pitch cannot exceed
 * device_freq*508 for an 8-channel 32-bit buffer. */
#ifndef STACK_DATA_SIZE
#define STACK_DATA_SIZE  16384
#endif


static __inline ALfloat minf(ALfloat a, ALfloat b)
{ return ((a > b) ? b : a); }
static __inline ALfloat maxf(ALfloat a, ALfloat b)
{ return ((a > b) ? a : b); }
static __inline ALfloat clampf(ALfloat val, ALfloat min, ALfloat max)
{ return minf(max, maxf(min, val)); }

static __inline ALuint minu(ALuint a, ALuint b)
{ return ((a > b) ? b : a); }
static __inline ALuint maxu(ALuint a, ALuint b)
{ return ((a > b) ? a : b); }
static __inline ALuint clampu(ALuint val, ALuint min, ALuint max)
{ return minu(max, maxu(min, val)); }

static __inline ALint mini(ALint a, ALint b)
{ return ((a > b) ? b : a); }
static __inline ALint maxi(ALint a, ALint b)
{ return ((a > b) ? a : b); }
static __inline ALint clampi(ALint val, ALint min, ALint max)
{ return mini(max, maxi(min, val)); }

static __inline ALint64 mini64(ALint64 a, ALint64 b)
{ return ((a > b) ? b : a); }
static __inline ALint64 maxi64(ALint64 a, ALint64 b)
{ return ((a > b) ? a : b); }
static __inline ALint64 clampi64(ALint64 val, ALint64 min, ALint64 max)
{ return mini64(max, maxi64(min, val)); }


static __inline ALfloat lerp(ALfloat val1, ALfloat val2, ALfloat mu)
{
    return val1 + (val2-val1)*mu;
}
static __inline ALfloat cubic(ALfloat val0, ALfloat val1, ALfloat val2, ALfloat val3, ALfloat mu)
{
    ALfloat mu2 = mu*mu;
    ALfloat a0 = -0.5f*val0 +  1.5f*val1 + -1.5f*val2 +  0.5f*val3;
    ALfloat a1 =       val0 + -2.5f*val1 +  2.0f*val2 + -0.5f*val3;
    ALfloat a2 = -0.5f*val0              +  0.5f*val2;
    ALfloat a3 =                    val1;

    return a0*mu*mu2 + a1*mu2 + a2*mu + a3;
}


static __inline int SetMixerFPUMode(void)
{
#if defined(_FPU_GETCW) && defined(_FPU_SETCW)
    fpu_control_t fpuState, newState;
    _FPU_GETCW(fpuState);
    newState = fpuState&~(_FPU_EXTENDED|_FPU_DOUBLE|_FPU_SINGLE |
                          _FPU_RC_NEAREST|_FPU_RC_DOWN|_FPU_RC_UP|_FPU_RC_ZERO);
    newState |= _FPU_SINGLE | _FPU_RC_ZERO;
    _FPU_SETCW(newState);
#else
    int fpuState;
#if defined(HAVE__CONTROLFP)
    fpuState = _controlfp(0, 0);
    (void)_controlfp(_RC_CHOP|_PC_24, _MCW_RC|_MCW_PC);
#elif defined(HAVE_FESETROUND)
    fpuState = fegetround();
    fesetround(FE_TOWARDZERO);
#endif
#endif
    return fpuState;
}

static __inline void RestoreFPUMode(int state)
{
#if defined(_FPU_GETCW) && defined(_FPU_SETCW)
    fpu_control_t fpuState = state;
    _FPU_SETCW(fpuState);
#elif defined(HAVE__CONTROLFP)
    _controlfp(state, _MCW_RC|_MCW_PC);
#elif defined(HAVE_FESETROUND)
    fesetround(state);
#endif
}


ALvoid aluInitPanning(ALCdevice *Device);
ALint aluCart2LUTpos(ALfloat re, ALfloat im);

ALvoid CalcSourceParams(struct ALsource *ALSource, const ALCcontext *ALContext);
ALvoid CalcNonAttnSourceParams(struct ALsource *ALSource, const ALCcontext *ALContext);

MixerFunc SelectMixer(enum Resampler Resampler);
MixerFunc SelectHrtfMixer(enum Resampler Resampler);

ALvoid MixSource(struct ALsource *Source, ALCdevice *Device, ALuint SamplesToDo);

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size);
ALvoid aluHandleDisconnect(ALCdevice *device);

extern ALfloat ConeScale;
extern ALfloat ZScale;

#ifdef __cplusplus
}
#endif

#endif

