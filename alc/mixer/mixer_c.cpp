#include "config.h"

#include <cassert>

#include <limits>

#include "alcmain.h"
#include "alu.h"

#include "defs.h"
#include "hrtfbase.h"


namespace {

inline float do_point(const InterpState&, const float *RESTRICT vals, const ALuint)
{ return vals[0]; }
inline float do_lerp(const InterpState&, const float *RESTRICT vals, const ALuint frac)
{ return lerp(vals[0], vals[1], static_cast<float>(frac)*(1.0f/FRACTIONONE)); }
inline float do_cubic(const InterpState&, const float *RESTRICT vals, const ALuint frac)
{ return cubic(vals[0], vals[1], vals[2], vals[3], static_cast<float>(frac)*(1.0f/FRACTIONONE)); }
inline float do_bsinc(const InterpState &istate, const float *RESTRICT vals, const ALuint frac)
{
    const size_t m{istate.bsinc.m};

    // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
    const ALuint pi{frac >> FRAC_PHASE_BITDIFF};
    const float pf{static_cast<float>(frac & ((1<<FRAC_PHASE_BITDIFF)-1)) *
        (1.0f/(1<<FRAC_PHASE_BITDIFF))};
#undef FRAC_PHASE_BITDIFF

    const float *fil{istate.bsinc.filter + m*pi*4};
    const float *phd{fil + m};
    const float *scd{phd + m};
    const float *spd{scd + m};

    // Apply the scale and phase interpolated filter.
    float r{0.0f};
    for(size_t j_f{0};j_f < m;j_f++)
        r += (fil[j_f] + istate.bsinc.sf*scd[j_f] + pf*(phd[j_f] + istate.bsinc.sf*spd[j_f])) * vals[j_f];
    return r;
}
inline float do_fastbsinc(const InterpState &istate, const float *RESTRICT vals, const ALuint frac)
{
    const size_t m{istate.bsinc.m};

    // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
    const ALuint pi{frac >> FRAC_PHASE_BITDIFF};
    const float pf{static_cast<float>(frac & ((1<<FRAC_PHASE_BITDIFF)-1)) *
        (1.0f/(1<<FRAC_PHASE_BITDIFF))};
#undef FRAC_PHASE_BITDIFF

    const float *fil{istate.bsinc.filter + m*pi*4};
    const float *phd{fil + m};

    // Apply the phase interpolated filter.
    float r{0.0f};
    for(size_t j_f{0};j_f < m;j_f++)
        r += (fil[j_f] + pf*phd[j_f]) * vals[j_f];
    return r;
}

using SamplerT = float(&)(const InterpState&, const float*RESTRICT, const ALuint);
template<SamplerT Sampler>
const float *DoResample(const InterpState *state, const float *RESTRICT src, ALuint frac,
    ALuint increment, const al::span<float> dst)
{
    const InterpState istate{*state};
    auto proc_sample = [&src,&frac,istate,increment]() -> float
    {
        const float ret{Sampler(istate, src, frac)};

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;

        return ret;
    };
    std::generate(dst.begin(), dst.end(), proc_sample);

    return dst.begin();
}

inline void ApplyCoeffs(float2 *RESTRICT Values, const ALuint IrSize, const HrirArray &Coeffs,
    const float left, const float right)
{
    ASSUME(IrSize >= MIN_IR_LENGTH);
    for(ALuint c{0};c < IrSize;++c)
    {
        Values[c][0] += Coeffs[c][0] * left;
        Values[c][1] += Coeffs[c][1] * right;
    }
}

} // namespace

template<>
const ALfloat *Resample_<CopyTag,CTag>(const InterpState*, const ALfloat *RESTRICT src, ALuint,
    ALuint, const al::span<float> dst)
{
#if defined(HAVE_SSE) || defined(HAVE_NEON)
    /* Avoid copying the source data if it's aligned like the destination. */
    if((reinterpret_cast<intptr_t>(src)&15) == (reinterpret_cast<intptr_t>(dst.data())&15))
        return src;
#endif
    std::copy_n(src, dst.size(), dst.begin());
    return dst.begin();
}

template<>
const ALfloat *Resample_<PointTag,CTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALuint frac, ALuint increment, const al::span<float> dst)
{ return DoResample<do_point>(state, src, frac, increment, dst); }

template<>
const ALfloat *Resample_<LerpTag,CTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALuint frac, ALuint increment, const al::span<float> dst)
{ return DoResample<do_lerp>(state, src, frac, increment, dst); }

template<>
const ALfloat *Resample_<CubicTag,CTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALuint frac, ALuint increment, const al::span<float> dst)
{ return DoResample<do_cubic>(state, src-1, frac, increment, dst); }

template<>
const ALfloat *Resample_<BSincTag,CTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALuint frac, ALuint increment, const al::span<float> dst)
{ return DoResample<do_bsinc>(state, src-state->bsinc.l, frac, increment, dst); }

template<>
const ALfloat *Resample_<FastBSincTag,CTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALuint frac, ALuint increment, const al::span<float> dst)
{ return DoResample<do_fastbsinc>(state, src-state->bsinc.l, frac, increment, dst); }


template<>
void MixHrtf_<CTag>(const float *InSamples, float2 *AccumSamples, const ALuint IrSize,
    const MixHrtfFilter *hrtfparams, const size_t BufferSize)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, BufferSize); }

template<>
void MixHrtfBlend_<CTag>(const float *InSamples, float2 *AccumSamples, const ALuint IrSize,
    const HrtfFilter *oldparams, const MixHrtfFilter *newparams, const size_t BufferSize)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        BufferSize);
}

template<>
void MixDirectHrtf_<CTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State,
    const size_t BufferSize)
{ MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, State, BufferSize); }


template<>
void Mix_<CTag>(const al::span<const float> InSamples, const al::span<FloatBufferLine> OutBuffer,
    float *CurrentGains, const float *TargetGains, const size_t Counter, const size_t OutPos)
{
    const ALfloat delta{(Counter > 0) ? 1.0f / static_cast<ALfloat>(Counter) : 0.0f};
    const bool reached_target{InSamples.size() >= Counter};
    const auto min_end = reached_target ? InSamples.begin() + Counter : InSamples.end();
    for(FloatBufferLine &output : OutBuffer)
    {
        ALfloat *RESTRICT dst{al::assume_aligned<16>(output.data()+OutPos)};
        ALfloat gain{*CurrentGains};
        const ALfloat diff{*TargetGains - gain};

        auto in_iter = InSamples.begin();
        if(std::fabs(diff) > std::numeric_limits<float>::epsilon())
        {
            const ALfloat step{diff * delta};
            ALfloat step_count{0.0f};
            while(in_iter != min_end)
            {
                *(dst++) += *(in_iter++) * (gain + step*step_count);
                step_count += 1.0f;
            }
            if(reached_target)
                gain = *TargetGains;
            else
                gain += step*step_count;
            *CurrentGains = gain;
        }
        ++CurrentGains;
        ++TargetGains;

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;
        while(in_iter != InSamples.end())
            *(dst++) += *(in_iter++) * gain;
    }
}

/* Basically the inverse of the above. Rather than one input going to multiple
 * outputs (each with its own gain), it's multiple inputs (each with its own
 * gain) going to one output. This applies one row (vs one column) of a matrix
 * transform. And as the matrices are more or less static once set up, no
 * stepping is necessary.
 */
template<>
void MixRow_<CTag>(const al::span<float> OutBuffer, const al::span<const float> Gains,
    const float *InSamples, const size_t InStride)
{
    for(const float gain : Gains)
    {
        const float *RESTRICT input{InSamples};
        InSamples += InStride;

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        auto do_mix = [gain](const float cur, const float src) noexcept -> float
        { return cur + src*gain; };
        std::transform(OutBuffer.begin(), OutBuffer.end(), input, OutBuffer.begin(), do_mix);
    }
}
