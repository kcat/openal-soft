#ifndef MIXER_DEFS_H
#define MIXER_DEFS_H

#include "AL/alc.h"
#include "AL/al.h"
#include "alMain.h"
#include "alu.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MixGains;

struct MixHrtfParams;
struct HrtfState;

/* C resamplers */
const ALfloat *Resample_copy_C(const InterpState *state, const ALfloat *RESTRICT src, ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen);
const ALfloat *Resample_point_C(const InterpState *state, const ALfloat *RESTRICT src, ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen);
const ALfloat *Resample_lerp_C(const InterpState *state, const ALfloat *RESTRICT src, ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen);
const ALfloat *Resample_cubic_C(const InterpState *state, const ALfloat *RESTRICT src, ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen);
const ALfloat *Resample_bsinc_C(const InterpState *state, const ALfloat *RESTRICT src, ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen);


/* C mixers */
void MixHrtf_C(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
               const ALfloat *data, ALsizei Offset, ALsizei OutPos,
               const ALsizei IrSize, struct MixHrtfParams *hrtfparams,
               struct HrtfState *hrtfstate, ALsizei BufferSize);
void MixHrtfBlend_C(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                    const ALfloat *data, ALsizei Offset, ALsizei OutPos,
                    const ALsizei IrSize, const HrtfParams *oldparams,
                    MixHrtfParams *newparams, HrtfState *hrtfstate,
                    ALsizei BufferSize);
void MixDirectHrtf_C(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                     const ALfloat *data, ALsizei Offset, const ALsizei IrSize,
                     const ALfloat (*RESTRICT Coeffs)[2], ALfloat (*RESTRICT Values)[2],
                     ALsizei BufferSize);
void Mix_C(const ALfloat *data, ALsizei OutChans, ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE],
           ALfloat *CurrentGains, const ALfloat *TargetGains, ALsizei Counter, ALsizei OutPos,
           ALsizei BufferSize);
void MixRow_C(ALfloat *OutBuffer, const ALfloat *Gains,
              const ALfloat (*RESTRICT data)[BUFFERSIZE], ALsizei InChans,
              ALsizei InPos, ALsizei BufferSize);

/* SSE mixers */
void MixHrtf_SSE(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                 const ALfloat *data, ALsizei Offset, ALsizei OutPos,
                 const ALsizei IrSize, struct MixHrtfParams *hrtfparams,
                 struct HrtfState *hrtfstate, ALsizei BufferSize);
void MixHrtfBlend_SSE(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                      const ALfloat *data, ALsizei Offset, ALsizei OutPos,
                      const ALsizei IrSize, const HrtfParams *oldparams,
                      MixHrtfParams *newparams, HrtfState *hrtfstate,
                      ALsizei BufferSize);
void MixDirectHrtf_SSE(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                       const ALfloat *data, ALsizei Offset, const ALsizei IrSize,
                       const ALfloat (*RESTRICT Coeffs)[2], ALfloat (*RESTRICT Values)[2],
                       ALsizei BufferSize);
void Mix_SSE(const ALfloat *data, ALsizei OutChans, ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE],
             ALfloat *CurrentGains, const ALfloat *TargetGains, ALsizei Counter, ALsizei OutPos,
             ALsizei BufferSize);
void MixRow_SSE(ALfloat *OutBuffer, const ALfloat *Gains,
                const ALfloat (*RESTRICT data)[BUFFERSIZE], ALsizei InChans,
                ALsizei InPos, ALsizei BufferSize);

/* SSE resamplers */
inline void InitiatePositionArrays(ALsizei frac, ALint increment, ALsizei *RESTRICT frac_arr, ALsizei *RESTRICT pos_arr, ALsizei size)
{
    ALsizei i;

    pos_arr[0] = 0;
    frac_arr[0] = frac;
    for(i = 1;i < size;i++)
    {
        ALint frac_tmp = frac_arr[i-1] + increment;
        pos_arr[i] = pos_arr[i-1] + (frac_tmp>>FRACTIONBITS);
        frac_arr[i] = frac_tmp&FRACTIONMASK;
    }
}

const ALfloat *Resample_lerp_SSE2(const InterpState *state, const ALfloat *RESTRICT src,
                                  ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                                  ALsizei numsamples);
const ALfloat *Resample_lerp_SSE41(const InterpState *state, const ALfloat *RESTRICT src,
                                   ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                                   ALsizei numsamples);

const ALfloat *Resample_bsinc_SSE(const InterpState *state, const ALfloat *RESTRICT src,
                                  ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                                  ALsizei dstlen);

/* Neon mixers */
void MixHrtf_Neon(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                  const ALfloat *data, ALsizei Offset, ALsizei OutPos,
                  const ALsizei IrSize, struct MixHrtfParams *hrtfparams,
                  struct HrtfState *hrtfstate, ALsizei BufferSize);
void MixHrtfBlend_Neon(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                       const ALfloat *data, ALsizei Offset, ALsizei OutPos,
                       const ALsizei IrSize, const HrtfParams *oldparams,
                       MixHrtfParams *newparams, HrtfState *hrtfstate,
                       ALsizei BufferSize);
void MixDirectHrtf_Neon(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                        const ALfloat *data, ALsizei Offset, const ALsizei IrSize,
                        const ALfloat (*RESTRICT Coeffs)[2], ALfloat (*RESTRICT Values)[2],
                        ALsizei BufferSize);
void Mix_Neon(const ALfloat *data, ALsizei OutChans, ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE],
              ALfloat *CurrentGains, const ALfloat *TargetGains, ALsizei Counter, ALsizei OutPos,
              ALsizei BufferSize);
void MixRow_Neon(ALfloat *OutBuffer, const ALfloat *Gains,
                 const ALfloat (*RESTRICT data)[BUFFERSIZE], ALsizei InChans,
                 ALsizei InPos, ALsizei BufferSize);

/* Neon resamplers */
const ALfloat *Resample_lerp_Neon(const InterpState *state, const ALfloat *RESTRICT src,
                                  ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                                  ALsizei numsamples);
const ALfloat *Resample_bsinc_Neon(const InterpState *state, const ALfloat *RESTRICT src,
                                   ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                                   ALsizei dstlen);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* MIXER_DEFS_H */
