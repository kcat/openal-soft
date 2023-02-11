#include "config.h"

#include <cassert>
#include <cmath>
#include <limits>

#include "alnumeric.h"
#include "core/bsinc_defs.h"
#include "core/cubic_defs.h"
#include "defs.h"
#include "hrtfbase.h"

struct CTag;
struct PointTag;
struct LerpTag;
struct CubicTag;
struct BSincTag;
struct FastBSincTag;


namespace {

constexpr uint BsincPhaseDiffBits{MixerFracBits - BSincPhaseBits};
constexpr uint BsincPhaseDiffOne{1 << BsincPhaseDiffBits};
constexpr uint BsincPhaseDiffMask{BsincPhaseDiffOne - 1u};

constexpr uint CubicPhaseDiffBits{MixerFracBits - CubicPhaseBits};
constexpr uint CubicPhaseDiffOne{1 << CubicPhaseDiffBits};
constexpr uint CubicPhaseDiffMask{CubicPhaseDiffOne - 1u};

inline float do_point(const InterpState&, const float *RESTRICT vals, const uint)
{ return vals[0]; }
inline float do_lerp(const InterpState&, const float *RESTRICT vals, const uint frac)
{ return lerpf(vals[0], vals[1], static_cast<float>(frac)*(1.0f/MixerFracOne)); }
inline float do_cubic(const InterpState &istate, const float *RESTRICT vals, const uint frac)
{
    /* Calculate the phase index and factor. */
    const uint pi{frac >> CubicPhaseDiffBits};
    const float pf{static_cast<float>(frac&CubicPhaseDiffMask) * (1.0f/CubicPhaseDiffOne)};

    const float *RESTRICT fil{al::assume_aligned<16>(istate.cubic.filter[pi].mCoeffs)};
    const float *RESTRICT phd{al::assume_aligned<16>(istate.cubic.filter[pi].mDeltas)};

    /* Apply the phase interpolated filter. */
    return (fil[0] + pf*phd[0])*vals[0] + (fil[1] + pf*phd[1])*vals[1]
        + (fil[2] + pf*phd[2])*vals[2] + (fil[3] + pf*phd[3])*vals[3];
}
inline float do_bsinc(const InterpState &istate, const float *RESTRICT vals, const uint frac)
{
    const size_t m{istate.bsinc.m};
    ASSUME(m > 0);

    /* Calculate the phase index and factor. */
    const uint pi{frac >> BsincPhaseDiffBits};
    const float pf{static_cast<float>(frac&BsincPhaseDiffMask) * (1.0f/BsincPhaseDiffOne)};

    const float *RESTRICT fil{istate.bsinc.filter + m*pi*2};
    const float *RESTRICT phd{fil + m};
    const float *RESTRICT scd{fil + BSincPhaseCount*2*m};
    const float *RESTRICT spd{scd + m};

    /* Apply the scale and phase interpolated filter. */
    float r{0.0f};
    for(size_t j_f{0};j_f < m;j_f++)
        r += (fil[j_f] + istate.bsinc.sf*scd[j_f] + pf*(phd[j_f] + istate.bsinc.sf*spd[j_f])) * vals[j_f];
    return r;
}
inline float do_fastbsinc(const InterpState &istate, const float *RESTRICT vals, const uint frac)
{
    const size_t m{istate.bsinc.m};
    ASSUME(m > 0);

    /* Calculate the phase index and factor. */
    const uint pi{frac >> BsincPhaseDiffBits};
    const float pf{static_cast<float>(frac&BsincPhaseDiffMask) * (1.0f/BsincPhaseDiffOne)};

    const float *RESTRICT fil{istate.bsinc.filter + m*pi*2};
    const float *RESTRICT phd{fil + m};

    /* Apply the phase interpolated filter. */
    float r{0.0f};
    for(size_t j_f{0};j_f < m;j_f++)
        r += (fil[j_f] + pf*phd[j_f]) * vals[j_f];
    return r;
}

using SamplerT = float(&)(const InterpState&, const float*RESTRICT, const uint);
template<SamplerT Sampler>
void DoResample(const InterpState *state, const float *RESTRICT src, uint frac,
    const uint increment, const al::span<float> dst)
{
    const InterpState istate{*state};
    ASSUME(frac < MixerFracOne);
    for(float &out : dst)
    {
        out = Sampler(istate, src, frac);

        frac += increment;
        src  += frac>>MixerFracBits;
        frac &= MixerFracMask;
    }
}

inline void ApplyCoeffs(float2 *RESTRICT Values, const size_t IrSize, const ConstHrirSpan Coeffs,
    const float left, const float right)
{
    ASSUME(IrSize >= MinIrLength);
    for(size_t c{0};c < IrSize;++c)
    {
        Values[c][0] += Coeffs[c][0] * left;
        Values[c][1] += Coeffs[c][1] * right;
    }
}

force_inline void MixLine(const al::span<const float> InSamples, float *RESTRICT dst,
    float &CurrentGain, const float TargetGain, const float delta, const size_t min_len,
    size_t Counter)
{
    float gain{CurrentGain};
    const float step{(TargetGain-gain) * delta};

    size_t pos{0};
    if(!(std::abs(step) > std::numeric_limits<float>::epsilon()))
        gain = TargetGain;
    else
    {
        float step_count{0.0f};
        for(;pos != min_len;++pos)
        {
            dst[pos] += InSamples[pos] * (gain + step*step_count);
            step_count += 1.0f;
        }
        if(pos == Counter)
            gain = TargetGain;
        else
            gain += step*step_count;
    }
    CurrentGain = gain;

    if(!(std::abs(gain) > GainSilenceThreshold))
        return;
    for(;pos != InSamples.size();++pos)
        dst[pos] += InSamples[pos] * gain;
}

} // namespace

template<>
void Resample_<PointTag,CTag>(const InterpState *state, const float *RESTRICT src, uint frac,
    const uint increment, const al::span<float> dst)
{ DoResample<do_point>(state, src, frac, increment, dst); }

template<>
void Resample_<LerpTag,CTag>(const InterpState *state, const float *RESTRICT src, uint frac,
    const uint increment, const al::span<float> dst)
{ DoResample<do_lerp>(state, src, frac, increment, dst); }

template<>
void Resample_<CubicTag,CTag>(const InterpState *state, const float *RESTRICT src, uint frac,
    const uint increment, const al::span<float> dst)
{ DoResample<do_cubic>(state, src-1, frac, increment, dst); }

template<>
void Resample_<BSincTag,CTag>(const InterpState *state, const float *RESTRICT src, uint frac,
    const uint increment, const al::span<float> dst)
{ DoResample<do_bsinc>(state, src-state->bsinc.l, frac, increment, dst); }

template<>
void Resample_<FastBSincTag,CTag>(const InterpState *state, const float *RESTRICT src, uint frac,
    const uint increment, const al::span<float> dst)
{ DoResample<do_fastbsinc>(state, src-state->bsinc.l, frac, increment, dst); }


template<>
void MixHrtf_<CTag>(const float *InSamples, float2 *AccumSamples, const uint IrSize,
    const MixHrtfFilter *hrtfparams, const size_t BufferSize)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, BufferSize); }

template<>
void MixHrtfBlend_<CTag>(const float *InSamples, float2 *AccumSamples, const uint IrSize,
    const HrtfFilter *oldparams, const MixHrtfFilter *newparams, const size_t BufferSize)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        BufferSize);
}

template<>
void MixDirectHrtf_<CTag>(const FloatBufferSpan LeftOut, const FloatBufferSpan RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples,
    float *TempBuf, HrtfChannelState *ChanState, const size_t IrSize, const size_t BufferSize)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, TempBuf, ChanState,
        IrSize, BufferSize);
}


template<>
void Mix_<CTag>(const al::span<const float> InSamples, const al::span<FloatBufferLine> OutBuffer,
    float *CurrentGains, const float *TargetGains, const size_t Counter, const size_t OutPos)
{
    const float delta{(Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f};
    const auto min_len = minz(Counter, InSamples.size());

    for(FloatBufferLine &output : OutBuffer)
        MixLine(InSamples, al::assume_aligned<16>(output.data()+OutPos), *CurrentGains++,
            *TargetGains++, delta, min_len, Counter);
}

template<>
void Mix_<CTag>(const al::span<const float> InSamples, float *OutBuffer, float &CurrentGain,
    const float TargetGain, const size_t Counter)
{
    const float delta{(Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f};
    const auto min_len = minz(Counter, InSamples.size());

    MixLine(InSamples, al::assume_aligned<16>(OutBuffer), CurrentGain,
        TargetGain, delta, min_len, Counter);
}
