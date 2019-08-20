#ifndef MIXER_DEFS_H
#define MIXER_DEFS_H

#include "AL/al.h"

#include "alcmain.h"
#include "alspan.h"
#include "alu.h"
#include "hrtf.h"


enum InstSetType {
    CTag,
    SSETag,
    SSE2Tag,
    SSE3Tag,
    SSE4Tag,
    NEONTag
};

enum ResampleType {
    CopyTag,
    PointTag,
    LerpTag,
    CubicTag,
    BSincTag
};

template<ResampleType TypeTag, InstSetType InstTag>
const ALfloat *Resample_(const InterpState *state, const ALfloat *RESTRICT src, ALsizei frac,
    ALint increment, const al::span<float> dst);

template<InstSetType InstTag>
void Mix_(const al::span<const float> InSamples, const al::span<FloatBufferLine> OutBuffer,
    float *CurrentGains, const float *TargetGains, const size_t Counter, const size_t OutPos);
template<InstSetType InstTag>
void MixRow_(const al::span<float> OutBuffer, const al::span<const float> Gains,
    const float *InSamples, const size_t InStride);

template<InstSetType InstTag>
void MixHrtf_(FloatBufferLine &LeftOut, FloatBufferLine &RightOut, const ALfloat *InSamples, float2 *AccumSamples, const size_t OutPos, const ALsizei IrSize, MixHrtfFilter *hrtfparams, const size_t BufferSize);
template<InstSetType InstTag>
void MixHrtfBlend_(FloatBufferLine &LeftOut, FloatBufferLine &RightOut, const ALfloat *InSamples, float2 *AccumSamples, const size_t OutPos, const ALsizei IrSize, const HrtfFilter *oldparams, MixHrtfFilter *newparams, const size_t BufferSize);
template<InstSetType InstTag>
void MixDirectHrtf_(FloatBufferLine &LeftOut, FloatBufferLine &RightOut, const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State, const size_t BufferSize);

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
