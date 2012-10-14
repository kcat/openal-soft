#ifndef _ALU_H_
#define _ALU_H_

#include "alMain.h"

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

#ifndef FLT_EPSILON
#define FLT_EPSILON (1.19209290e-07f)
#endif


#ifndef HAVE_POWF
static __inline float powf(float x, float y)
{ return (float)pow(x, y); }
#endif

#ifndef HAVE_SQRTF
static __inline float sqrtf(float x)
{ return (float)sqrt(x); }
#endif

#ifndef HAVE_COSF
static __inline float cosf(float x)
{ return (float)cos(x); }
#endif

#ifndef HAVE_SINF
static __inline float sinf(float x)
{ return (float)sin(x); }
#endif

#ifndef HAVE_ACOSF
static __inline float acosf(float x)
{ return (float)acos(x); }
#endif

#ifndef HAVE_ASINF
static __inline float asinf(float x)
{ return (float)asin(x); }
#endif

#ifndef HAVE_ATANF
static __inline float atanf(float x)
{ return (float)atan(x); }
#endif

#ifndef HAVE_ATAN2F
static __inline float atan2f(float x, float y)
{ return (float)atan2(x, y); }
#endif

#ifndef HAVE_FABSF
static __inline float fabsf(float x)
{ return (float)fabs(x); }
#endif

#ifndef HAVE_LOG10F
static __inline float log10f(float x)
{ return (float)log10(x); }
#endif

#ifndef HAVE_FLOORF
static __inline float floorf(float x)
{ return (float)floor(x); }
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ALsource;
struct ALbuffer;
struct DirectParams;
struct SendParams;

typedef void (*ResamplerFunc)(const ALfloat *src, ALuint frac, ALuint increment,
                              ALfloat *RESTRICT dst, ALuint dstlen);

typedef ALvoid (*DryMixerFunc)(ALCdevice *Device, struct DirectParams *params,
                               const ALfloat *RESTRICT data, ALuint srcchan,
                               ALuint OutPos, ALuint SamplesToDo,
                               ALuint BufferSize);
typedef ALvoid (*WetMixerFunc)(struct SendParams *params,
                               const ALfloat *RESTRICT data,
                               ALuint OutPos, ALuint SamplesToDo,
                               ALuint BufferSize);


#define SPEEDOFSOUNDMETRESPERSEC  (343.3f)
#define AIRABSORBGAINHF           (0.99426f) /* -0.05dB */

#define FRACTIONBITS (14)
#define FRACTIONONE  (1<<FRACTIONBITS)
#define FRACTIONMASK (FRACTIONONE-1)


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

ALvoid MixSource(struct ALsource *Source, ALCdevice *Device, ALuint SamplesToDo);

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size);
ALvoid aluHandleDisconnect(ALCdevice *device);

extern ALfloat ConeScale;
extern ALfloat ZScale;

#ifdef __cplusplus
}
#endif

#endif

