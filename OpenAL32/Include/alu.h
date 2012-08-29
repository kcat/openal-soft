#ifndef _ALU_H_
#define _ALU_H_

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include <limits.h>
#include <math.h>
#ifdef HAVE_FLOAT_H
#include <float.h>
/* HACK: Seems cross-compiling with MinGW includes the wrong float.h, which
 * doesn't define Windows' _controlfp and related macros */
#if defined(__MINGW32__) && !defined(_RC_CHOP)
/* Control word masks for unMask */
#define _MCW_EM         0x0008001F      /* Error masks */
#define _MCW_IC         0x00040000      /* Infinity */
#define _MCW_RC         0x00000300      /* Rounding */
#define _MCW_PC         0x00030000      /* Precision */
/* Control word values for unNew (use with related unMask above) */
#define _EM_INVALID     0x00000010
#define _EM_DENORMAL    0x00080000
#define _EM_ZERODIVIDE  0x00000008
#define _EM_OVERFLOW    0x00000004
#define _EM_UNDERFLOW   0x00000002
#define _EM_INEXACT     0x00000001
#define _IC_AFFINE      0x00040000
#define _IC_PROJECTIVE  0x00000000
#define _RC_CHOP        0x00000300
#define _RC_UP          0x00000200
#define _RC_DOWN        0x00000100
#define _RC_NEAR        0x00000000
#define _PC_24          0x00020000
#define _PC_53          0x00010000
#define _PC_64          0x00000000
_CRTIMP unsigned int __cdecl __MINGW_NOTHROW _controlfp (unsigned int unNew, unsigned int unMask);
#endif
#endif
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif


#define F_PI    (3.14159265358979323846f)  /* pi */
#define F_PI_2  (1.57079632679489661923f)  /* pi/2 */

#ifndef HAVE_POWF
static __inline float powf(float x, float y)
{ return (float)pow(x, y); }
#endif

#ifndef HAVE_SQRTF
static __inline float sqrtf(float x)
{ (float)sqrt(x); }
#endif

#ifndef HAVE_COSF
static __inline float cosf(float x)
{ (float)cos(x); }
#endif

#ifndef HAVE_SINF
static __inline float sinf(float x)
{ (float)sin(x); }
#endif

#ifndef HAVE_ACOSF
static __inline float acosf(float x)
{ (float)acos(x); }
#endif

#ifndef HAVE_ASINF
static __inline float asinf(float x)
{ (float)asin(x); }
#endif

#ifndef HAVE_ATANF
static __inline float atanf(float x)
{ (float)atan(x); }
#endif

#ifndef HAVE_ATAN2F
static __inline float atan2f(float x, float y)
{ (float)atan2(x, y); }
#endif

#ifndef HAVE_FABSF
static __inline float fabsf(float x)
{ (float)fabs(x); }
#endif

#ifndef HAVE_LOG10F
static __inline float log10f(float x)
{ (float)log10(x); }
#endif

#ifndef HAVE_FLOORF
static __inline float floorf(float x)
{ (float)floor(x); }
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ALsource;
struct ALbuffer;
struct DirectParams;
struct SendParams;

typedef ALvoid (*DryMixerFunc)(struct ALsource *self, ALCdevice *Device,
                               struct DirectParams *params,
                               const ALfloat *RESTRICT data, ALuint srcfrac,
                               ALuint OutPos, ALuint SamplesToDo,
                               ALuint BufferSize);
typedef ALvoid (*WetMixerFunc)(struct ALsource *self, ALuint sendidx,
                               struct SendParams *params,
                               const ALfloat *RESTRICT data, ALuint srcfrac,
                               ALuint OutPos, ALuint SamplesToDo,
                               ALuint BufferSize);

enum Resampler {
    PointResampler,
    LinearResampler,
    CubicResampler,

    ResamplerMax,
};

enum Channel {
    FrontLeft = 0,
    FrontRight,  /* 1 */
    FrontCenter, /* 2 */
    LFE,         /* 3 */
    BackLeft,    /* 4 */
    BackRight,   /* 5 */
    BackCenter,  /* 6 */
    SideLeft,    /* 7 */
    SideRight,   /* 8 */

    /* Must be a multiple of 4 */
    MaxChannels = 12,
};

enum DistanceModel {
    InverseDistanceClamped  = AL_INVERSE_DISTANCE_CLAMPED,
    LinearDistanceClamped   = AL_LINEAR_DISTANCE_CLAMPED,
    ExponentDistanceClamped = AL_EXPONENT_DISTANCE_CLAMPED,
    InverseDistance  = AL_INVERSE_DISTANCE,
    LinearDistance   = AL_LINEAR_DISTANCE,
    ExponentDistance = AL_EXPONENT_DISTANCE,
    DisableDistance  = AL_NONE,

    DefaultDistanceModel = InverseDistanceClamped
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

static __inline ALuint64 minu64(ALuint64 a, ALuint64 b)
{ return ((a > b) ? b : a); }
static __inline ALuint64 maxu64(ALuint64 a, ALuint64 b)
{ return ((a > b) ? a : b); }
static __inline ALuint64 clampu64(ALuint64 val, ALuint64 min, ALuint64 max)
{ return minu64(max, maxu64(min, val)); }


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
#if defined(_FPU_GETCW) && defined(_FPU_SETCW) && (defined(__i386__) || defined(__x86_64__))
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
#ifdef FE_TOWARDZERO
    fesetround(FE_TOWARDZERO);
#endif
#endif
#endif
    return fpuState;
}

static __inline void RestoreFPUMode(int state)
{
#if defined(_FPU_GETCW) && defined(_FPU_SETCW) && (defined(__i386__) || defined(__x86_64__))
    fpu_control_t fpuState = state;
    _FPU_SETCW(fpuState);
#elif defined(HAVE__CONTROLFP)
    _controlfp(state, _MCW_RC|_MCW_PC);
#elif defined(HAVE_FESETROUND)
    fesetround(state);
#endif
}


static __inline void aluCrossproduct(const ALfloat *inVector1, const ALfloat *inVector2, ALfloat *outVector)
{
    outVector[0] = inVector1[1]*inVector2[2] - inVector1[2]*inVector2[1];
    outVector[1] = inVector1[2]*inVector2[0] - inVector1[0]*inVector2[2];
    outVector[2] = inVector1[0]*inVector2[1] - inVector1[1]*inVector2[0];
}

static __inline ALfloat aluDotproduct(const ALfloat *inVector1, const ALfloat *inVector2)
{
    return inVector1[0]*inVector2[0] + inVector1[1]*inVector2[1] +
           inVector1[2]*inVector2[2];
}

static __inline void aluNormalize(ALfloat *inVector)
{
    ALfloat lengthsqr = aluDotproduct(inVector, inVector);
    if(lengthsqr > 0.0f)
    {
        ALfloat inv_length = 1.0f/sqrtf(lengthsqr);
        inVector[0] *= inv_length;
        inVector[1] *= inv_length;
        inVector[2] *= inv_length;
    }
}


ALvoid aluInitPanning(ALCdevice *Device);

ALvoid ComputeAngleGains(const ALCdevice *device, ALfloat angle, ALfloat hwidth, ALfloat ingain, ALfloat *gains);

ALvoid CalcSourceParams(struct ALsource *ALSource, const ALCcontext *ALContext);
ALvoid CalcNonAttnSourceParams(struct ALsource *ALSource, const ALCcontext *ALContext);

DryMixerFunc SelectDirectMixer(enum Resampler Resampler);
DryMixerFunc SelectHrtfMixer(enum Resampler Resampler);
WetMixerFunc SelectSendMixer(enum Resampler Resampler);

ALvoid MixSource(struct ALsource *Source, ALCdevice *Device, ALuint SamplesToDo);

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size);
ALvoid aluHandleDisconnect(ALCdevice *device);

extern ALfloat ConeScale;
extern ALfloat ZScale;

#ifdef __cplusplus
}
#endif

#endif

