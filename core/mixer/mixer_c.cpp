#include "config.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <variant>

#include "alnumeric.h"
#include "alspan.h"
#include "core/bsinc_defs.h"
#include "core/bufferline.h"
#include "core/cubic_defs.h"
#include "core/mixer/hrtfdefs.h"
#include "defs.h"
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

constexpr
auto do_point(const float *vals, const uint) noexcept -> float { return vals[0]; }
constexpr
auto do_lerp(const float *vals, const uint frac) noexcept -> float
{ return lerpf(vals[0], vals[1], static_cast<float>(frac)*(1.0f/MixerFracOne)); }
constexpr
auto do_cubic(const CubicState &istate, const float *vals, const uint frac) noexcept -> float
{
    /* Calculate the phase index and factor. */
    const uint pi{frac >> CubicPhaseDiffBits};
    const float pf{static_cast<float>(frac&CubicPhaseDiffMask) * (1.0f/CubicPhaseDiffOne)};

    const auto fil = al::span{istate.filter[pi].mCoeffs};
    const auto phd = al::span{istate.filter[pi].mDeltas};

    /* Apply the phase interpolated filter. */
    return (fil[0] + pf*phd[0])*vals[0] + (fil[1] + pf*phd[1])*vals[1]
        + (fil[2] + pf*phd[2])*vals[2] + (fil[3] + pf*phd[3])*vals[3];
}
constexpr
auto do_bsinc(const BsincState &istate, const float *vals, const uint frac) noexcept -> float
{
    const size_t m{istate.m};
    ASSUME(m > 0);

    /* Calculate the phase index and factor. */
    const uint pi{frac >> BsincPhaseDiffBits};
    const float pf{static_cast<float>(frac&BsincPhaseDiffMask) * (1.0f/BsincPhaseDiffOne)};

    const float *fil{istate.filter.data() + m*pi*2_uz};
    const float *phd{fil + m};
    const float *scd{fil + BSincPhaseCount*2_uz*m};
    const float *spd{scd + m};

    /* Apply the scale and phase interpolated filter. */
    float r{0.0f};
    for(size_t j_f{0};j_f < m;j_f++)
        r += (fil[j_f] + istate.sf*scd[j_f] + pf*(phd[j_f] + istate.sf*spd[j_f])) * vals[j_f];
    return r;
}
constexpr
auto do_fastbsinc(const BsincState &istate, const float *vals, const uint frac) noexcept -> float
{
    const size_t m{istate.m};
    ASSUME(m > 0);

    /* Calculate the phase index and factor. */
    const uint pi{frac >> BsincPhaseDiffBits};
    const float pf{static_cast<float>(frac&BsincPhaseDiffMask) * (1.0f/BsincPhaseDiffOne)};

    const float *fil{istate.filter.data() + m*pi*2_uz};
    const float *phd{fil + m};

    /* Apply the phase interpolated filter. */
    float r{0.0f};
    for(size_t j_f{0};j_f < m;j_f++)
        r += (fil[j_f] + pf*phd[j_f]) * vals[j_f];
    return r;
}

template<float(&Sampler)(const float*, const uint)noexcept>
void DoResample(const float *src, uint frac, const uint increment, const al::span<float> dst)
{
    ASSUME(frac < MixerFracOne);
    std::generate(dst.begin(), dst.end(), [&src,&frac,increment]() -> float
    {
        const float output{Sampler(src, frac)};
        frac += increment;
        src  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

template<typename U, float(&Sampler)(const U&, const float*,const uint)noexcept>
void DoResample(const U istate, const float *src, uint frac, const uint increment,
    const al::span<float> dst)
{
    ASSUME(frac < MixerFracOne);
    std::generate(dst.begin(), dst.end(), [istate,&src,&frac,increment]() -> float
    {
        const float output{Sampler(istate, src, frac)};
        frac += increment;
        src  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

inline void ApplyCoeffs(const al::span<float2> Values, const size_t IrSize,
    const ConstHrirSpan Coeffs, const float left, const float right) noexcept
{
    ASSUME(IrSize >= MinIrLength);
    ASSUME(IrSize <= HrirLength);

    auto mix_impulse = [left,right](const float2 &value, const float2 &coeff) noexcept -> float2
    { return float2{{value[0] + coeff[0]*left, value[1] + coeff[1]*right}}; };
    std::transform(Values.cbegin(), Values.cbegin()+ptrdiff_t(IrSize), Coeffs.cbegin(),
        Values.begin(), mix_impulse);
}

force_inline void MixLine(const al::span<const float> InSamples, const al::span<float> dst,
    float &CurrentGain, const float TargetGain, const float delta, const size_t min_len,
    size_t Counter)
{
    const float step{(TargetGain-CurrentGain) * delta};

    auto output = dst.begin();
    auto input = InSamples.cbegin();
    if(std::abs(step) > std::numeric_limits<float>::epsilon())
    {
        const float gain{CurrentGain};
        float step_count{0.0f};
        output = std::transform(output, output+ptrdiff_t(min_len), input, output,
            [gain,step,&step_count](float out, const float in) noexcept -> float
            {
                out += in * (gain + step*step_count);
                step_count += 1.0f;
                return out;
            });
        input += ptrdiff_t(min_len);

        if(min_len < Counter)
        {
            CurrentGain = gain + step*step_count;
            return;
        }
    }
    CurrentGain = TargetGain;

    if(!(std::abs(TargetGain) > GainSilenceThreshold))
        return;

    std::transform(output, dst.end(), input, output,
        [TargetGain](float out, const float in) noexcept -> float
        { return out + in*TargetGain; });
}

} // namespace

template<>
void Resample_<PointTag,CTag>(const InterpState*, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{ DoResample<do_point>(src, frac, increment, dst); }

template<>
void Resample_<LerpTag,CTag>(const InterpState*, const float *src, uint frac, const uint increment,
    const al::span<float> dst)
{ DoResample<do_lerp>(src, frac, increment, dst); }

template<>
void Resample_<CubicTag,CTag>(const InterpState *state, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{ DoResample<CubicState,do_cubic>(std::get<CubicState>(*state), src-1, frac, increment, dst); }

template<>
void Resample_<BSincTag,CTag>(const InterpState *state, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{
    const auto istate = std::get<BsincState>(*state);
    DoResample<BsincState,do_bsinc>(istate, src-istate.l, frac, increment, dst);
}

template<>
void Resample_<FastBSincTag,CTag>(const InterpState *state, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{
    const auto istate = std::get<BsincState>(*state);
    DoResample<BsincState,do_fastbsinc>(istate, src-istate.l, frac, increment, dst);
}


template<>
void MixHrtf_<CTag>(const al::span<const float> InSamples, const al::span<float2> AccumSamples,
    const uint IrSize, const MixHrtfFilter *hrtfparams, const size_t SamplesToDo)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, SamplesToDo); }

template<>
void MixHrtfBlend_<CTag>(const al::span<const float> InSamples,const al::span<float2> AccumSamples,
    const uint IrSize, const HrtfFilter *oldparams, const MixHrtfFilter *newparams,
    const size_t SamplesToDo)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        SamplesToDo);
}

template<>
void MixDirectHrtf_<CTag>(const FloatBufferSpan LeftOut, const FloatBufferSpan RightOut,
    const al::span<const FloatBufferLine> InSamples, const al::span<float2> AccumSamples,
    const al::span<float,BufferLineSize> TempBuf, const al::span<HrtfChannelState> ChanState,
    const size_t IrSize, const size_t SamplesToDo)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, TempBuf, ChanState,
        IrSize, SamplesToDo);
}


template<>
void Mix_<CTag>(const al::span<const float> InSamples, const al::span<FloatBufferLine> OutBuffer,
    const al::span<float> CurrentGains, const al::span<const float> TargetGains,
    const size_t Counter, const size_t OutPos)
{
    const float delta{(Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f};
    const auto min_len = std::min(Counter, InSamples.size());

    auto curgains = CurrentGains.begin();
    auto targetgains = TargetGains.cbegin();
    for(FloatBufferLine &output : OutBuffer)
        MixLine(InSamples, al::span{output}.subspan(OutPos), *curgains++, *targetgains++, delta,
            min_len, Counter);
}

template<>
void Mix_<CTag>(const al::span<const float> InSamples, const al::span<float> OutBuffer,
    float &CurrentGain, const float TargetGain, const size_t Counter)
{
    const float delta{(Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f};
    const auto min_len = std::min(Counter, InSamples.size());

    MixLine(InSamples, OutBuffer, CurrentGain, TargetGain, delta, min_len, Counter);
}
