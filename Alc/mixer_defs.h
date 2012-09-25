#ifndef MIXER_DEFS_H
#define MIXER_DEFS_H

#include "AL/alc.h"
#include "AL/al.h"
#include "alMain.h"

struct ALsource;
struct DirectParams;
struct SendParams;

/* C resamplers */
void Resample_point32_C(const ALfloat *src, ALuint frac, ALuint increment, ALuint NumChannels, ALfloat *RESTRICT dst, ALuint dstlen);
void Resample_lerp32_C(const ALfloat *src, ALuint frac, ALuint increment, ALuint NumChannels, ALfloat *RESTRICT dst, ALuint dstlen);
void Resample_cubic32_C(const ALfloat *src, ALuint frac, ALuint increment, ALuint NumChannels, ALfloat *RESTRICT dst, ALuint dstlen);


/* C mixers */
void MixDirect_Hrtf_C(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_C(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixSend_C(struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint);

/* SSE mixers */
void MixDirect_Hrtf_SSE(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixDirect_SSE(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);
void MixSend_SSE(struct SendParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint);

/* Neon mixers */
void MixDirect_Hrtf_Neon(struct ALsource*,ALCdevice*,struct DirectParams*,const ALfloat*RESTRICT,ALuint,ALuint,ALuint,ALuint);

#endif /* MIXER_DEFS_H */
