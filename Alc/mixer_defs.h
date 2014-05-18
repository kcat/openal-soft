#ifndef MIXER_DEFS_H
#define MIXER_DEFS_H

#include "AL/alc.h"
#include "AL/al.h"
#include "alMain.h"

struct DirectParams;
struct SendParams;

struct HrtfParams;
struct HrtfState;

/* C resamplers */
void Resample_copy32_C(const ALfloat *src, ALuint frac, ALuint increment, ALfloat *restrict dst, ALuint dstlen);
void Resample_point32_C(const ALfloat *src, ALuint frac, ALuint increment, ALfloat *restrict dst, ALuint dstlen);
void Resample_lerp32_C(const ALfloat *src, ALuint frac, ALuint increment, ALfloat *restrict dst, ALuint dstlen);
void Resample_cubic32_C(const ALfloat *src, ALuint frac, ALuint increment, ALfloat *restrict dst, ALuint dstlen);


/* C mixers */
void MixDirect_Hrtf_C(ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *data,
                      ALuint Counter, ALuint Offset, const ALuint IrSize,
                      const struct HrtfParams *hrtfparams, struct HrtfState *hrtfstate,
                      ALuint OutPos, ALuint BufferSize);
void MixDirect_C(struct DirectParams *params,
                 ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *restrict data,
                 ALuint Counter, ALuint srcchan, ALuint OutPos, ALuint BufferSize);
void MixSend_C(struct SendParams*,const ALfloat*restrict,ALuint,ALuint);

/* SSE mixers */
void MixDirect_Hrtf_SSE(ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *data,
                        ALuint Counter, ALuint Offset, const ALuint IrSize,
                        const struct HrtfParams *hrtfparams, struct HrtfState *hrtfstate,
                        ALuint OutPos, ALuint BufferSize);
void MixDirect_SSE(struct DirectParams *params,
                   ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *restrict data,
                   ALuint Counter, ALuint srcchan, ALuint OutPos, ALuint BufferSize);
void MixSend_SSE(struct SendParams*,const ALfloat*restrict,ALuint,ALuint);

/* Neon mixers */
void MixDirect_Hrtf_Neon(ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *data,
                         ALuint Counter, ALuint Offset, const ALuint IrSize,
                         const struct HrtfParams *hrtfparams, struct HrtfState *hrtfstate,
                         ALuint OutPos, ALuint BufferSize);
void MixDirect_Neon(struct DirectParams *params,
                    ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *restrict data,
                    ALuint Counter, ALuint srcchan, ALuint OutPos, ALuint BufferSize);
void MixSend_Neon(struct SendParams*,const ALfloat*restrict,ALuint,ALuint);

#endif /* MIXER_DEFS_H */
