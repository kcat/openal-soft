#include "config.h"

#include <cassert>

#include <limits>

#include "alcmain.h"
#include "alu.h"

#include "defs.h"
#include "hrtfbase.h"


namespace {

inline ALfloat do_point(const InterpState&, const ALfloat *RESTRICT vals, const ALuint)
{ return vals[0]; }
inline ALfloat do_lerp(const InterpState&, const ALfloat *RESTRICT vals, const ALuint frac)
{ return lerp(vals[0], vals[1], static_cast<float>(frac)*(1.0f/FRACTIONONE)); }
inline ALfloat do_cubic(const InterpState&, const ALfloat *RESTRICT vals, const ALuint frac)
{ return cubic(vals[0], vals[1], vals[2], vals[3], static_cast<float>(frac)*(1.0f/FRACTIONONE)); }
inline ALfloat do_bsinc(const InterpState &istate, const ALfloat *RESTRICT vals, const ALuint frac)
{
    ASSUME(istate.bsinc.m > 0);

    // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
    const ALuint pi{frac >> FRAC_PHASE_BITDIFF};
    const ALfloat pf{static_cast<float>(frac & ((1<<FRAC_PHASE_BITDIFF)-1)) *
        (1.0f/(1<<FRAC_PHASE_BITDIFF))};
#undef FRAC_PHASE_BITDIFF

    const ALfloat *fil{istate.bsinc.filter + static_cast<ptrdiff_t>(istate.bsinc.m)*pi*4};
    const ALfloat *scd{fil + istate.bsinc.m};
    const ALfloat *phd{scd + istate.bsinc.m};
    const ALfloat *spd{phd + istate.bsinc.m};

    // Apply the scale and phase interpolated filter.
    ALfloat r{0.0f};
    for(ALsizei j_f{0};j_f < istate.bsinc.m;j_f++)
        r += (fil[j_f] + istate.bsinc.sf*scd[j_f] + pf*(phd[j_f] + istate.bsinc.sf*spd[j_f])) * vals[j_f];
    return r;
}

using SamplerT = ALfloat(const InterpState&, const ALfloat*RESTRICT, const ALuint);
template<SamplerT &Sampler>
const ALfloat *DoResample(const InterpState *state, const ALfloat *RESTRICT src,
    ALuint frac, ALuint increment, const al::span<float> dst)
{
    const InterpState istate{*state};
    auto proc_sample = [&src,&frac,istate,increment]() -> ALfloat
    {
        const ALfloat ret{Sampler(istate, src, frac)};

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;

        return ret;
    };
    std::generate(dst.begin(), dst.end(), proc_sample);

    return dst.begin();
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


static inline void ApplyCoeffs(size_t /*Offset*/, float2 *RESTRICT Values, const ALuint IrSize,
    const HrirArray &Coeffs, const ALfloat left, const ALfloat right)
{
    ASSUME(IrSize >= 4);
    for(ALuint c{0};c < IrSize;++c)
    {
        Values[c][0] += Coeffs[c][0] * left;
        Values[c][1] += Coeffs[c][1] * right;
    }
}

template<>
void MixHrtf_<CTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const size_t OutPos, const ALuint IrSize,
    MixHrtfFilter *hrtfparams, const size_t BufferSize)
{
    MixHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        hrtfparams, BufferSize);
}

template<>
void MixHrtfBlend_<CTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const size_t OutPos, const ALuint IrSize,
    const HrtfFilter *oldparams, MixHrtfFilter *newparams, const size_t BufferSize)
{
    MixHrtfBlendBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        oldparams, newparams, BufferSize);
}

template<>
void MixDirectHrtf_<CTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State,
    const size_t BufferSize)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, State, BufferSize);
}


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
        const float *RESTRICT src{InSamples};
        InSamples += InStride;

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        std::transform(OutBuffer.begin(), OutBuffer.end(), src, OutBuffer.begin(),
            [gain](const ALfloat cur, const ALfloat src) -> ALfloat { return cur + src*gain; });
    }
}
