#include "config.h"

#include <algorithm>
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

constexpr auto BsincPhaseDiffBits = u32{MixerFracBits - BSincPhaseBits};
constexpr auto BsincPhaseDiffOne = 1_u32 << BsincPhaseDiffBits;
constexpr auto BsincPhaseDiffMask = BsincPhaseDiffOne - 1_u32;

constexpr auto CubicPhaseDiffBits = u32{MixerFracBits - CubicPhaseBits};
constexpr auto CubicPhaseDiffOne = 1_u32 << CubicPhaseDiffBits;
constexpr auto CubicPhaseDiffMask = CubicPhaseDiffOne - 1_u32;

using SamplerNST = auto(std::span<f32 const> vals, usize pos, u32 frac) noexcept -> f32;

template<typename T>
using SamplerT = auto(T const &istate, std::span<f32 const> vals, usize pos, u32 frac) noexcept -> f32;

[[nodiscard]] constexpr
auto do_point(std::span<f32 const> const vals, usize const pos, u32) noexcept -> f32
{ return vals[pos]; }
[[nodiscard]] constexpr
auto do_lerp(std::span<f32 const> const vals, usize const pos, u32 const frac) noexcept -> f32
{ return lerpf(vals[pos+0], vals[pos+1], gsl::narrow_cast<f32>(frac)*(1.0f/MixerFracOne)); }
[[nodiscard]] constexpr
auto do_cubic(CubicState const &istate, std::span<f32 const> const vals, usize const pos,
    u32 const frac) noexcept -> f32
{
    /* Calculate the phase index and factor. */
    auto const pi = u32{frac>>CubicPhaseDiffBits}; ASSUME(pi < CubicPhaseCount);
    auto const pf = gsl::narrow_cast<f32>(frac&CubicPhaseDiffMask) * f32{1.0f/CubicPhaseDiffOne};

    auto const fil = std::span{istate.filter[pi].mCoeffs};
    auto const phd = std::span{istate.filter[pi].mDeltas};

    /* Apply the phase interpolated filter. */
    return (fil[0] + pf*phd[0])*vals[pos+0] + (fil[1] + pf*phd[1])*vals[pos+1]
        + (fil[2] + pf*phd[2])*vals[pos+2] + (fil[3] + pf*phd[3])*vals[pos+3];
}
[[nodiscard]] constexpr
auto do_fastbsinc(BsincState const &bsinc, std::span<f32 const> const vals, usize const pos,
    u32 const frac) noexcept -> f32
{
    auto const m = usize{bsinc.m};
    ASSUME(m > 0);
    ASSUME(m <= MaxResamplerPadding);

    /* Calculate the phase index and factor. */
    auto const pi = u32{frac>>BsincPhaseDiffBits}; ASSUME(pi < BSincPhaseCount);
    auto const pf = gsl::narrow_cast<f32>(frac&BsincPhaseDiffMask) * (1.0f/BsincPhaseDiffOne);

    auto const fil = bsinc.filter.subspan(2_uz*pi*m);
    auto const phd = fil.subspan(m);

    /* Apply the phase interpolated filter. */
    auto r = 0.0f;
    for(auto j_f=0_uz;j_f < m;++j_f)
        r += (fil[j_f] + pf*phd[j_f]) * vals[pos+j_f];
    return r;
}
[[nodiscard]] constexpr
auto do_bsinc(BsincState const &bsinc, std::span<f32 const> const vals, usize const pos,
    u32 const frac) noexcept -> f32
{
    auto const m = usize{bsinc.m};
    ASSUME(m > 0);
    ASSUME(m <= MaxResamplerPadding);

    /* Calculate the phase index and factor. */
    auto const pi = u32{frac>>BsincPhaseDiffBits}; ASSUME(pi < BSincPhaseCount);
    auto const pf = gsl::narrow_cast<f32>(frac&BsincPhaseDiffMask) * f32{1.0f/BsincPhaseDiffOne};

    auto const fil = bsinc.filter.subspan(2_uz*pi*m);
    auto const phd = fil.subspan(m);
    auto const scd = fil.subspan(BSincPhaseCount*2_uz*m);
    auto const spd = scd.subspan(m);

    /* Apply the scale and phase interpolated filter. */
    auto r = 0.0f;
    for(auto j_f=0_uz;j_f < m;++j_f)
        r += (fil[j_f] + bsinc.sf*scd[j_f] + pf*(phd[j_f] + bsinc.sf*spd[j_f])) * vals[pos+j_f];
    return r;
}

template<SamplerNST Sampler>
void DoResample(std::span<f32 const> const src, u32 frac, u32 const increment,
    std::span<f32> const dst)
{
    ASSUME(frac < MixerFracOne);
    auto pos = 0_uz;
    std::generate(dst.begin(), dst.end(), [&pos,&frac,src,increment]() -> f32
    {
        const auto output = Sampler(src, pos, frac);
        frac += increment;
        pos  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

template<typename U, SamplerT<U> Sampler>
void DoResample(U const istate, std::span<f32 const> const src, u32 frac, u32 const increment,
    std::span<f32> const dst)
{
    ASSUME(frac < MixerFracOne);
    auto pos = 0_uz;
    std::generate(dst.begin(), dst.end(), [istate,src,&pos,&frac,increment]() -> f32
    {
        auto const output = Sampler(istate, src, pos, frac);
        frac += increment;
        pos  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

void ApplyCoeffs(std::span<f32x2> const Values, usize const IrSize, ConstHrirSpan const Coeffs,
    f32 const left, f32 const right) noexcept
{
    ASSUME(IrSize >= MinIrLength);
    ASSUME(IrSize <= HrirLength);

    std::ranges::transform(Values | std::views::take(IrSize), Coeffs, Values.begin(),
        [left,right](f32x2 const &value, f32x2 const &coeff) noexcept -> f32x2
    { return f32x2{{value[0] + coeff[0]*left, value[1] + coeff[1]*right}}; });
}

force_inline void MixLine(std::span<f32 const> InSamples, std::span<f32> const dst,
    f32 &CurrentGain, f32 const TargetGain, f32 const delta, usize const fade_len,
    usize Counter)
{
    auto const step = (TargetGain-CurrentGain) * delta;

    auto output = dst.begin();
    if(std::abs(step) > std::numeric_limits<f32>::epsilon())
    {
        auto input = InSamples.first(fade_len);
        InSamples = InSamples.subspan(fade_len);

        auto const gain = CurrentGain;
        auto step_count = 0.0f;
        output = std::transform(input.begin(), input.end(), output, output,
            [gain,step,&step_count](f32 const in, f32 out) noexcept -> f32
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
        [TargetGain](f32 const in, f32 const out) noexcept -> f32
        { return out + in*TargetGain; });
}

} // namespace

template<>
void Resample_<PointTag,CTag>(InterpState const*, std::span<f32 const> const src, u32 const frac,
    u32 const increment, std::span<f32> const dst)
{ DoResample<do_point>(src.subspan(MaxResamplerEdge), frac, increment, dst); }

template<>
void Resample_<LerpTag,CTag>(InterpState const*, std::span<f32 const> const src, u32 const frac,
    u32 const increment, std::span<f32> const dst)
{ DoResample<do_lerp>(src.subspan(MaxResamplerEdge), frac, increment, dst); }

template<>
void Resample_<CubicTag,CTag>(InterpState const *const state, std::span<f32 const> const src,
    u32 const frac, u32 const increment, std::span<f32> const dst)
{
    DoResample<CubicState,do_cubic>(std::get<CubicState>(*state), src.subspan(MaxResamplerEdge-1),
        frac, increment, dst);
}

template<>
void Resample_<FastBSincTag,CTag>(InterpState const *const state, std::span<f32 const> const src,
    u32 const frac, u32 const increment, std::span<f32> const dst)
{
    auto const istate = std::get<BsincState>(*state);
    ASSUME(istate.l <= MaxResamplerEdge);
    DoResample<BsincState,do_fastbsinc>(istate, src.subspan(MaxResamplerEdge-istate.l), frac,
        increment, dst);
}

template<>
void Resample_<BSincTag,CTag>(InterpState const *const state, std::span<f32 const> const src,
    u32 const frac, u32 const increment, std::span<f32> const dst)
{
    auto const istate = std::get<BsincState>(*state);
    ASSUME(istate.l <= MaxResamplerEdge);
    DoResample<BsincState,do_bsinc>(istate, src.subspan(MaxResamplerEdge-istate.l), frac,
        increment, dst);
}


template<>
void MixHrtf_<CTag>(std::span<f32 const> const InSamples, std::span<f32x2> const AccumSamples,
    u32 const IrSize, MixHrtfFilter const *const hrtfparams, usize const SamplesToDo)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, SamplesToDo); }

template<>
void MixHrtfBlend_<CTag>(std::span<f32 const> const InSamples,
    std::span<f32x2> const AccumSamples, u32 const IrSize, HrtfFilter const *const oldparams,
    MixHrtfFilter const *const newparams, usize const SamplesToDo)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        SamplesToDo);
}

template<>
void MixDirectHrtf_<CTag>(FloatBufferSpan const LeftOut, FloatBufferSpan const RightOut,
    std::span<FloatBufferLine const> const InSamples, std::span<f32x2> const AccumSamples,
    std::span<f32, BufferLineSize> const TempBuf, std::span<HrtfChannelState> const ChanState,
    usize const IrSize, usize const SamplesToDo)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, TempBuf, ChanState,
        IrSize, SamplesToDo);
}


template<>
void Mix_<CTag>(std::span<f32 const> const InSamples, std::span<FloatBufferLine> const OutBuffer,
    std::span<f32> const CurrentGains, std::span<f32 const> const TargetGains,
    usize const Counter, usize const OutPos)
{
    auto const delta = (Counter > 0) ? 1.0f / gsl::narrow_cast<f32>(Counter) : 0.0f;
    auto const fade_len = std::min(Counter, InSamples.size());

    auto curgains = CurrentGains.begin();
    auto targetgains = TargetGains.begin();
    for(FloatBufferLine &output : OutBuffer)
        MixLine(InSamples, std::span{output}.subspan(OutPos), *curgains++, *targetgains++, delta,
            fade_len, Counter);
}

template<>
void Mix_<CTag>(std::span<f32 const> const InSamples, std::span<f32> const OutBuffer,
    f32 &CurrentGain, f32 const TargetGain, usize const Counter)
{
    const auto delta = (Counter > 0) ? 1.0f / gsl::narrow_cast<f32>(Counter) : 0.0f;
    const auto fade_len = std::min(Counter, InSamples.size());

    MixLine(InSamples, OutBuffer, CurrentGain, TargetGain, delta, fade_len, Counter);
}
