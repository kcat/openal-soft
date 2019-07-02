#include "config.h"

#include <cassert>

#include <limits>

#include "alMain.h"
#include "alu.h"
#include "alSource.h"
#include "alAuxEffectSlot.h"
#include "defs.h"
#include "hrtfbase.h"


namespace {

inline ALfloat do_point(const InterpState&, const ALfloat *RESTRICT vals, const ALsizei)
{ return vals[0]; }
inline ALfloat do_lerp(const InterpState&, const ALfloat *RESTRICT vals, const ALsizei frac)
{ return lerp(vals[0], vals[1], frac * (1.0f/FRACTIONONE)); }
inline ALfloat do_cubic(const InterpState&, const ALfloat *RESTRICT vals, const ALsizei frac)
{ return cubic(vals[0], vals[1], vals[2], vals[3], frac * (1.0f/FRACTIONONE)); }
inline ALfloat do_bsinc(const InterpState &istate, const ALfloat *RESTRICT vals, const ALsizei frac)
{
    ASSUME(istate.bsinc.m > 0);

    // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
    const ALsizei pi{frac >> FRAC_PHASE_BITDIFF};
    const ALfloat pf{(frac & ((1<<FRAC_PHASE_BITDIFF)-1)) * (1.0f/(1<<FRAC_PHASE_BITDIFF))};
#undef FRAC_PHASE_BITDIFF

    const ALfloat *fil{istate.bsinc.filter + istate.bsinc.m*pi*4};
    const ALfloat *scd{fil + istate.bsinc.m};
    const ALfloat *phd{scd + istate.bsinc.m};
    const ALfloat *spd{phd + istate.bsinc.m};

    // Apply the scale and phase interpolated filter.
    ALfloat r{0.0f};
    for(ALsizei j_f{0};j_f < istate.bsinc.m;j_f++)
        r += (fil[j_f] + istate.bsinc.sf*scd[j_f] + pf*(phd[j_f] + istate.bsinc.sf*spd[j_f])) * vals[j_f];
    return r;
}

using SamplerT = ALfloat(const InterpState&, const ALfloat*RESTRICT, const ALsizei);
template<SamplerT &Sampler>
const ALfloat *DoResample(const InterpState *state, const ALfloat *RESTRICT src,
    ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei numsamples)
{
    ASSUME(numsamples > 0);
    ASSUME(increment > 0);
    ASSUME(frac >= 0);

    const InterpState istate{*state};
    auto proc_sample = [&src,&frac,istate,increment]() -> ALfloat
    {
        const ALfloat ret{Sampler(istate, src, frac)};

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;

        return ret;
    };
    std::generate_n(dst, numsamples, proc_sample);

    return dst;
}

} // namespace

template<>
const ALfloat *Resample_<CopyTag,CTag>(const InterpState* UNUSED(state),
    const ALfloat *RESTRICT src, ALsizei UNUSED(frac), ALint UNUSED(increment),
    ALfloat *RESTRICT dst, ALsizei dstlen)
{
    ASSUME(dstlen > 0);
#if defined(HAVE_SSE) || defined(HAVE_NEON)
    /* Avoid copying the source data if it's aligned like the destination. */
    if((reinterpret_cast<intptr_t>(src)&15) == (reinterpret_cast<intptr_t>(dst)&15))
        return src;
#endif
    std::copy_n(src, dstlen, dst);
    return dst;
}

template<>
const ALfloat *Resample_<PointTag,CTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen)
{ return DoResample<do_point>(state, src, frac, increment, dst, dstlen); }

template<>
const ALfloat *Resample_<LerpTag,CTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen)
{ return DoResample<do_lerp>(state, src, frac, increment, dst, dstlen); }

template<>
const ALfloat *Resample_<CubicTag,CTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen)
{ return DoResample<do_cubic>(state, src-1, frac, increment, dst, dstlen); }

template<>
const ALfloat *Resample_<BSincTag,CTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen)
{ return DoResample<do_bsinc>(state, src-state->bsinc.l, frac, increment, dst, dstlen); }


static inline void ApplyCoeffs(ALsizei /*Offset*/, float2 *RESTRICT Values, const ALsizei IrSize,
    const HrirArray<ALfloat> &Coeffs, const ALfloat left, const ALfloat right)
{
    ASSUME(IrSize >= 2);
    for(ALsizei c{0};c < IrSize;++c)
    {
        Values[c][0] += Coeffs[c][0] * left;
        Values[c][1] += Coeffs[c][1] * right;
    }
}

template<>
void MixHrtf_<CTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const ALsizei OutPos, const ALsizei IrSize,
    MixHrtfFilter *hrtfparams, const ALsizei BufferSize)
{
    MixHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        hrtfparams, BufferSize);
}

template<>
void MixHrtfBlend_<CTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const ALsizei OutPos, const ALsizei IrSize,
    const HrtfFilter *oldparams, MixHrtfFilter *newparams, const ALsizei BufferSize)
{
    MixHrtfBlendBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        oldparams, newparams, BufferSize);
}

template<>
void MixDirectHrtf_<CTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State,
    const ALsizei BufferSize)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, State, BufferSize);
}


template<>
void Mix_<CTag>(const ALfloat *data, const al::span<FloatBufferLine> OutBuffer,
    ALfloat *CurrentGains, const ALfloat *TargetGains, const ALsizei Counter, const ALsizei OutPos,
    const ALsizei BufferSize)
{
    ASSUME(BufferSize > 0);

    const ALfloat delta{(Counter > 0) ? 1.0f / static_cast<ALfloat>(Counter) : 0.0f};
    for(FloatBufferLine &output : OutBuffer)
    {
        ALfloat *RESTRICT dst{output.data()+OutPos};
        ALfloat gain{*CurrentGains};
        const ALfloat diff{*TargetGains - gain};

        ALsizei pos{0};
        if(std::fabs(diff) > std::numeric_limits<float>::epsilon())
        {
            ALsizei minsize{mini(BufferSize, Counter)};
            const ALfloat step{diff * delta};
            ALfloat step_count{0.0f};
            for(;pos < minsize;pos++)
            {
                dst[pos] += data[pos] * (gain + step*step_count);
                step_count += 1.0f;
            }
            if(pos == Counter)
                gain = *TargetGains;
            else
                gain += step*step_count;
            *CurrentGains = gain;
        }
        ++CurrentGains;
        ++TargetGains;

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;
        for(;pos < BufferSize;pos++)
            dst[pos] += data[pos]*gain;
    }
}

/* Basically the inverse of the above. Rather than one input going to multiple
 * outputs (each with its own gain), it's multiple inputs (each with its own
 * gain) going to one output. This applies one row (vs one column) of a matrix
 * transform. And as the matrices are more or less static once set up, no
 * stepping is necessary.
 */
template<>
void MixRow_<CTag>(FloatBufferLine &OutBuffer, const ALfloat *Gains,
    const al::span<const FloatBufferLine> InSamples, const ALsizei InPos, const ALsizei BufferSize)
{
    ASSUME(BufferSize > 0);

    for(const FloatBufferLine &input : InSamples)
    {
        const ALfloat *RESTRICT src{input.data()+InPos};
        const ALfloat gain{*(Gains++)};
        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        for(ALsizei i{0};i < BufferSize;i++)
            OutBuffer[i] += src[i] * gain;
    }
}
