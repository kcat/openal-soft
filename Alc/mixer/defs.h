#ifndef MIXER_DEFS_H
#define MIXER_DEFS_H

#include "AL/alc.h"
#include "AL/al.h"
#include "alMain.h"
#include "alu.h"

#include "alspan.h"


struct MixGains;
struct MixHrtfFilter;
struct HrtfState;
struct DirectHrtfState;


struct CTag { };
struct SSETag { };
struct SSE2Tag { };
struct SSE3Tag { };
struct SSE4Tag { };
struct NEONTag { };

struct CopyTag { };
struct PointTag { };
struct LerpTag { };
struct CubicTag { };
struct BSincTag { };

template<typename TypeTag, typename InstTag>
const ALfloat *Resample_(const InterpState *state, const ALfloat *RESTRICT src, ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen);

template<typename InstTag>
void Mix_(const ALfloat *data, const al::span<FloatBufferLine> OutBuffer, ALfloat *CurrentGains, const ALfloat *TargetGains, const ALsizei Counter, const ALsizei OutPos, const ALsizei BufferSize);
template<typename InstTag>
void MixRow_(FloatBufferLine &OutBuffer, const ALfloat *Gains, const al::span<const FloatBufferLine> InSamples, const ALsizei InPos, const ALsizei BufferSize);

template<typename InstTag>
void MixHrtf_(FloatBufferLine &LeftOut, FloatBufferLine &RightOut, const ALfloat *InSamples, float2 *AccumSamples, const ALsizei OutPos, const ALsizei IrSize, MixHrtfFilter *hrtfparams, const ALsizei BufferSize);
template<typename InstTag>
void MixHrtfBlend_(FloatBufferLine &LeftOut, FloatBufferLine &RightOut, const ALfloat *InSamples, float2 *AccumSamples, const ALsizei OutPos, const ALsizei IrSize, const HrtfFilter *oldparams, MixHrtfFilter *newparams, const ALsizei BufferSize);
template<typename InstTag>
void MixDirectHrtf_(FloatBufferLine &LeftOut, FloatBufferLine &RightOut, const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State, const ALsizei BufferSize);

/* Vectorized resampler helpers */
inline void InitiatePositionArrays(ALsizei frac, ALint increment, ALsizei *RESTRICT frac_arr, ALsizei *RESTRICT pos_arr, ALsizei size)
{
    pos_arr[0] = 0;
    frac_arr[0] = frac;
    for(ALsizei i{1};i < size;i++)
    {
        ALint frac_tmp = frac_arr[i-1] + increment;
        pos_arr[i] = pos_arr[i-1] + (frac_tmp>>FRACTIONBITS);
        frac_arr[i] = frac_tmp&FRACTIONMASK;
    }
}

#endif /* MIXER_DEFS_H */
