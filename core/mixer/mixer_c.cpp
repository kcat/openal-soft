#include "config.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <ranges>
#include <span>
#include <variant>

#include "alnumeric.h"
#include "core/bsinc_defs.h"
#include "core/bufferline.h"
#include "core/cubic_defs.h"
#include "core/mixer/hrtfdefs.h"
#include "core/resampler_limits.h"
#include "defs.h"
#include "gsl/gsl"
#include "hrtfbase.h"
#include "opthelpers.h"

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

using SamplerNST = float(const std::span<const float>, const size_t, const uint) noexcept;

template<typename T>
using SamplerT = float(const T&,const std::span<const float>,const size_t,const uint) noexcept;

[[nodiscard]] constexpr
auto do_point(const std::span<const float> vals, const size_t pos, const uint) noexcept -> float
{ return vals[pos]; }
[[nodiscard]] constexpr
auto do_lerp(const std::span<const float> vals, const size_t pos, const uint frac) noexcept -> float
{ return lerpf(vals[pos+0], vals[pos+1], gsl::narrow_cast<float>(frac)*(1.0f/MixerFracOne)); }
[[nodiscard]] constexpr
auto do_cubic(const CubicState &istate, const std::span<const float> vals, const size_t pos,
    const uint frac) noexcept -> float
{
    /* Calculate the phase index and factor. */
    const auto pi = uint{frac>>CubicPhaseDiffBits}; ASSUME(pi < CubicPhaseCount);
    const auto pf = gsl::narrow_cast<float>(frac&CubicPhaseDiffMask) * (1.0f/CubicPhaseDiffOne);

    const auto fil = std::span{istate.filter[pi].mCoeffs};
    const auto phd = std::span{istate.filter[pi].mDeltas};

    /* Apply the phase interpolated filter. */
    return (fil[0] + pf*phd[0])*vals[pos+0] + (fil[1] + pf*phd[1])*vals[pos+1]
        + (fil[2] + pf*phd[2])*vals[pos+2] + (fil[3] + pf*phd[3])*vals[pos+3];
}
[[nodiscard]] constexpr
auto do_fastbsinc(const BsincState &bsinc, const std::span<const float> vals, const size_t pos,
    const uint frac) noexcept -> float
{
    const auto m = size_t{bsinc.m};
    ASSUME(m > 0);
    ASSUME(m <= MaxResamplerPadding);

    /* Calculate the phase index and factor. */
    const auto pi = uint{frac>>BsincPhaseDiffBits}; ASSUME(pi < BSincPhaseCount);
    const auto pf = gsl::narrow_cast<float>(frac&BsincPhaseDiffMask) * (1.0f/BsincPhaseDiffOne);

    const auto fil = bsinc.filter.subspan(2_uz*pi*m);
    const auto phd = fil.subspan(m);

    /* Apply the phase interpolated filter. */
    auto r = 0.0f;
    for(size_t j_f{0};j_f < m;++j_f)
        r += (fil[j_f] + pf*phd[j_f]) * vals[pos+j_f];
    return r;
}
[[nodiscard]] constexpr
auto do_bsinc(const BsincState &bsinc, const std::span<const float> vals, const size_t pos,
    const uint frac) noexcept -> float
{
    const auto m = size_t{bsinc.m};
    ASSUME(m > 0);
    ASSUME(m <= MaxResamplerPadding);

    /* Calculate the phase index and factor. */
    const auto pi = uint{frac>>BsincPhaseDiffBits}; ASSUME(pi < BSincPhaseCount);
    const auto pf = gsl::narrow_cast<float>(frac&BsincPhaseDiffMask) * (1.0f/BsincPhaseDiffOne);

    const auto fil = bsinc.filter.subspan(2_uz*pi*m);
    const auto phd = fil.subspan(m);
    const auto scd = fil.subspan(BSincPhaseCount*2_uz*m);
    const auto spd = scd.subspan(m);

    /* Apply the scale and phase interpolated filter. */
    auto r = 0.0f;
    for(size_t j_f{0};j_f < m;++j_f)
        r += (fil[j_f] + bsinc.sf*scd[j_f] + pf*(phd[j_f] + bsinc.sf*spd[j_f])) * vals[pos+j_f];
    return r;
}

template<SamplerNST Sampler>
void DoResample(const std::span<const float> src, uint frac, const uint increment,
    const std::span<float> dst)
{
    ASSUME(frac < MixerFracOne);
    auto pos = 0_uz;
    std::generate(dst.begin(), dst.end(), [&pos,&frac,src,increment]() -> float
    {
        const auto output = Sampler(src, pos, frac);
        frac += increment;
        pos  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

template<typename U, SamplerT<U> Sampler>
void DoResample(const U istate, const std::span<const float> src, uint frac, const uint increment,
    const std::span<float> dst)
{
    ASSUME(frac < MixerFracOne);
    auto pos = 0_uz;
    std::generate(dst.begin(), dst.end(), [istate,src,&pos,&frac,increment]() -> float
    {
        const auto output = Sampler(istate, src, pos, frac);
        frac += increment;
        pos  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

inline void ApplyCoeffs(const std::span<float2> Values, const size_t IrSize,
    const ConstHrirSpan Coeffs, const float left, const float right) noexcept
{
    ASSUME(IrSize >= MinIrLength);
    ASSUME(IrSize <= HrirLength);

    std::ranges::transform(Values | std::views::take(IrSize), Coeffs, Values.begin(),
        [left,right](const float2 &value, const float2 &coeff) noexcept -> float2
    { return float2{{value[0] + coeff[0]*left, value[1] + coeff[1]*right}}; });
}

force_inline void MixLine(std::span<const float> InSamples, const std::span<float> dst,
    float &CurrentGain, const float TargetGain, const float delta, const size_t fade_len,
    size_t Counter)
{
    const float step{(TargetGain-CurrentGain) * delta};

    auto output = dst.begin();
    if(std::abs(step) > std::numeric_limits<float>::epsilon())
    {
        auto input = InSamples.first(fade_len);
        InSamples = InSamples.subspan(fade_len);

        const float gain{CurrentGain};
        float step_count{0.0f};
        output = std::transform(input.begin(), input.end(), output, output,
            [gain,step,&step_count](const float in, float out) noexcept -> float
            {
                out += in * (gain + step*step_count);
                step_count += 1.0f;
                return out;
            });

        if(fade_len < Counter)
        {
            CurrentGain = gain + step*step_count;
            return;
        }
    }
    CurrentGain = TargetGain;

    if(!(std::abs(TargetGain) > GainSilenceThreshold))
        return;

    std::transform(InSamples.begin(), InSamples.end(), output, output,
        [TargetGain](const float in, const float out) noexcept -> float
        { return out + in*TargetGain; });
}

} // namespace

template<>
void Resample_<PointTag,CTag>(const InterpState*, const std::span<const float> src, uint frac,
    const uint increment, const std::span<float> dst)
{ DoResample<do_point>(src.subspan(MaxResamplerEdge), frac, increment, dst); }

template<>
void Resample_<LerpTag,CTag>(const InterpState*, const std::span<const float> src, uint frac,
    const uint increment, const std::span<float> dst)
{ DoResample<do_lerp>(src.subspan(MaxResamplerEdge), frac, increment, dst); }

template<>
void Resample_<CubicTag,CTag>(const InterpState *state, const std::span<const float> src,
    uint frac, const uint increment, const std::span<float> dst)
{
    DoResample<CubicState,do_cubic>(std::get<CubicState>(*state), src.subspan(MaxResamplerEdge-1),
        frac, increment, dst);
}

template<>
void Resample_<FastBSincTag,CTag>(const InterpState *state, const std::span<const float> src,
    uint frac, const uint increment, const std::span<float> dst)
{
    const auto istate = std::get<BsincState>(*state);
    ASSUME(istate.l <= MaxResamplerEdge);
    DoResample<BsincState,do_fastbsinc>(istate, src.subspan(MaxResamplerEdge-istate.l), frac,
        increment, dst);
}

template<>
void Resample_<BSincTag,CTag>(const InterpState *state, const std::span<const float> src,
    uint frac, const uint increment, const std::span<float> dst)
{
    const auto istate = std::get<BsincState>(*state);
    ASSUME(istate.l <= MaxResamplerEdge);
    DoResample<BsincState,do_bsinc>(istate, src.subspan(MaxResamplerEdge-istate.l), frac,
        increment, dst);
}


template<>
void MixHrtf_<CTag>(const std::span<const float> InSamples, const std::span<float2> AccumSamples,
    const uint IrSize, const MixHrtfFilter *hrtfparams, const size_t SamplesToDo)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, SamplesToDo); }

template<>
void MixHrtfBlend_<CTag>(const std::span<const float> InSamples,
    const std::span<float2> AccumSamples, const uint IrSize, const HrtfFilter *oldparams,
    const MixHrtfFilter *newparams, const size_t SamplesToDo)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        SamplesToDo);
}

template<>
void MixDirectHrtf_<CTag>(const FloatBufferSpan LeftOut, const FloatBufferSpan RightOut,
    const std::span<const FloatBufferLine> InSamples, const std::span<float2> AccumSamples,
    const std::span<float,BufferLineSize> TempBuf, const std::span<HrtfChannelState> ChanState,
    const size_t IrSize, const size_t SamplesToDo)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, TempBuf, ChanState,
        IrSize, SamplesToDo);
}


template<>
void Mix_<CTag>(const std::span<const float> InSamples, const std::span<FloatBufferLine> OutBuffer,
    const std::span<float> CurrentGains, const std::span<const float> TargetGains,
    const size_t Counter, const size_t OutPos)
{
    const auto delta = (Counter > 0) ? 1.0f / gsl::narrow_cast<float>(Counter) : 0.0f;
    const auto fade_len = std::min(Counter, InSamples.size());

    auto curgains = CurrentGains.begin();
    auto targetgains = TargetGains.begin();
    for(FloatBufferLine &output : OutBuffer)
        MixLine(InSamples, std::span{output}.subspan(OutPos), *curgains++, *targetgains++, delta,
            fade_len, Counter);
}

template<>
void Mix_<CTag>(const std::span<const float> InSamples, const std::span<float> OutBuffer,
    float &CurrentGain, const float TargetGain, const size_t Counter)
{
    const auto delta = (Counter > 0) ? 1.0f / gsl::narrow_cast<float>(Counter) : 0.0f;
    const auto fade_len = std::min(Counter, InSamples.size());

    MixLine(InSamples, OutBuffer, CurrentGain, TargetGain, delta, fade_len, Counter);
}
