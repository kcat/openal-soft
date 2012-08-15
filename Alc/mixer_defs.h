#ifndef MIXER_DEFS_H
#define MIXER_DEFS_H

#include "AL/alc.h"
#include "AL/al.h"
#include "alMain.h"
#include "alu.h"

static __inline ALfloat point32(const ALfloat *vals, ALint step, ALint frac)
{ return vals[0]; (void)step; (void)frac; }
static __inline ALfloat lerp32(const ALfloat *vals, ALint step, ALint frac)
{ return lerp(vals[0], vals[step], frac * (1.0f/FRACTIONONE)); }
static __inline ALfloat cubic32(const ALfloat *vals, ALint step, ALint frac)
{ return cubic(vals[-step], vals[0], vals[step], vals[step+step],
               frac * (1.0f/FRACTIONONE)); }

struct ALsource;
struct DirectParams;
struct SendParams;

/* C mixers */
void MixDirect_Hrtf_point32_C(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_Hrtf_lerp32_C(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_Hrtf_cubic32_C(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

void MixDirect_point32_C(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_lerp32_C(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_cubic32_C(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

void MixSend_point32_C(struct ALsource*,ALuint,struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixSend_lerp32_C(struct ALsource*,ALuint,struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixSend_cubic32_C(struct ALsource*,ALuint,struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

/* SSE mixers */
void MixDirect_Hrtf_point32_SSE(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_Hrtf_lerp32_SSE(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_Hrtf_cubic32_SSE(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

void MixDirect_point32_SSE(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_lerp32_SSE(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_cubic32_SSE(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

void MixSend_point32_SSE(struct ALsource*,ALuint,struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixSend_lerp32_SSE(struct ALsource*,ALuint,struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixSend_cubic32_SSE(struct ALsource*,ALuint,struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

/* Neon mixers */
void MixDirect_Hrtf_point32_Neon(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_Hrtf_lerp32_Neon(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_Hrtf_cubic32_Neon(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

void MixDirect_point32_Neon(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_lerp32_Neon(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_cubic32_Neon(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

void MixSend_point32_Neon(struct ALsource*,ALuint,struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixSend_lerp32_Neon(struct ALsource*,ALuint,struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixSend_cubic32_Neon(struct ALsource*,ALuint,struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

#endif /* MIXER_DEFS_H */
