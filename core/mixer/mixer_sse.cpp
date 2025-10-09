#include "config.h"

#include <mmintrin.h>
#include <xmmintrin.h>

#include <algorithm>
#include <array>
#include <limits>
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
struct SSETag;
struct CubicTag;
struct BSincTag;
struct FastBSincTag;


#if defined(__GNUC__) && !defined(__clang__) && !defined(__SSE__)
#pragma GCC target("sse")
#endif

namespace {

constexpr auto BSincPhaseDiffBits = u32{MixerFracBits - BSincPhaseBits};
constexpr auto BSincPhaseDiffOne = 1_u32 << BSincPhaseDiffBits;
constexpr auto BSincPhaseDiffMask = BSincPhaseDiffOne - 1_u32;

constexpr auto CubicPhaseDiffBits = u32{MixerFracBits - CubicPhaseBits};
constexpr auto CubicPhaseDiffOne = 1_u32 << CubicPhaseDiffBits;
constexpr auto CubicPhaseDiffMask = CubicPhaseDiffOne - 1_u32;

force_inline auto vmadd(__m128 const x, __m128 const y, __m128 const z) noexcept -> __m128
{ return _mm_add_ps(x, _mm_mul_ps(y, z)); }

void ApplyCoeffs(std::span<f32x2> const Values, usize const IrSize, ConstHrirSpan const Coeffs,
    f32 const left, f32 const right)
{
    ASSUME(IrSize >= MinIrLength);
    ASSUME(IrSize <= HrirLength);
    auto const lrlr = _mm_setr_ps(left, right, left, right);
    /* Round up the IR size to a multiple of 2 for SIMD (2 IRs for 2 channels
     * is 4 floats), to avoid cutting the last sample for odd IR counts. The
     * underlying HRIR is a fixed-size multiple of 2, any extra samples are
     * either 0 (silence) or more IR samples that get applied for "free".
     */
    auto const count4 = usize{(IrSize+1) >> 1};

    /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
     * This isn't technically correct to test alignment, but it's true for
     * systems that support SSE, which is the only one that needs to know the
     * alignment of Values (which alternates between 8- and 16-byte aligned).
     */
    if(!(reinterpret_cast<uintptr_t>(Values.data())&15))
    {
        auto const vals4 = std::span{reinterpret_cast<__m128*>(Values[0].data()), count4};
        auto const coeffs4 = std::span{reinterpret_cast<const __m128*>(Coeffs[0].data()), count4};

        std::ranges::transform(vals4, coeffs4, vals4.begin(),
            [lrlr](__m128 const &val, __m128 const &coeff) -> __m128
            { return vmadd(val, coeff, lrlr); });
    }
    else
    {
        auto coeffs = _mm_load_ps(Coeffs[0].data());
        auto vals = _mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<__m64*>(Values[0].data()));
        auto imp0 = _mm_mul_ps(lrlr, coeffs);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi(reinterpret_cast<__m64*>(Values[0].data()), vals);
        auto td = count4 - 1_uz;
        auto i = 1_uz;
        do {
            coeffs = _mm_load_ps(Coeffs[i+1].data());
            vals = _mm_load_ps(Values[i].data());
            auto const imp1 = _mm_mul_ps(lrlr, coeffs);
            imp0 = _mm_shuffle_ps(imp0, imp1, _MM_SHUFFLE(1, 0, 3, 2));
            vals = _mm_add_ps(imp0, vals);
            _mm_store_ps(Values[i].data(), vals);
            imp0 = imp1;
            i += 2;
        } while(--td);
        vals = _mm_loadl_pi(vals, reinterpret_cast<__m64*>(Values[i].data()));
        imp0 = _mm_movehl_ps(imp0, imp0);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi(reinterpret_cast<__m64*>(Values[i].data()), vals);
    }
    /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */
}

force_inline void MixLine(std::span<f32 const> const InSamples, std::span<f32> const dst,
    f32 &CurrentGain, f32 const TargetGain, f32 const delta, usize const fade_len,
    usize const realign_len, usize const Counter)
{
    auto const step = f32{(TargetGain-CurrentGain) * delta};

    auto pos = 0_uz;
    if(std::abs(step) > std::numeric_limits<float>::epsilon())
    {
        auto const gain = CurrentGain;
        auto step_count = 0.0f;
        /* Mix with applying gain steps in aligned multiples of 4. */
        if(auto const todo = fade_len>>2)
        {
            auto const four4 = _mm_set1_ps(4.0f);
            auto const step4 = _mm_set1_ps(step);
            auto const gain4 = _mm_set1_ps(gain);
            auto step_count4 = _mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f);

            /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) */
            auto const in4 = std::span{reinterpret_cast<const __m128*>(InSamples.data()),
                InSamples.size()/4}.first(todo);
            auto const out4 = std::span{reinterpret_cast<__m128*>(dst.data()), dst.size()/4};
            /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */
            std::ranges::transform(in4, out4, out4.begin(),
                [gain4,step4,four4,&step_count4](__m128 const val4, __m128 dry4) -> __m128
            {
                /* dry += val * (gain + step*step_count) */
                dry4 = vmadd(dry4, val4, vmadd(gain4, step4, step_count4));
                step_count4 = _mm_add_ps(step_count4, four4);
                return dry4;
            });
            pos += in4.size()*4;

            /* NOTE: step_count4 now represents the next four counts after the
             * last four mixed samples, so the lowest element represents the
             * next step count to apply.
             */
            step_count = _mm_cvtss_f32(step_count4);
        }
        /* Mix with applying left over gain steps that aren't aligned multiples of 4. */
        if(auto const leftover = fade_len&3)
        {
            auto const in = InSamples.subspan(pos, leftover);
            auto const out = dst.subspan(pos);

            std::ranges::transform(in, out, out.begin(),
                [gain,step,&step_count](f32 const val, f32 dry) noexcept -> f32
            {
                dry += val * (gain + step*step_count);
                step_count += 1.0f;
                return dry;
            });
            pos += leftover;
        }
        if(pos < Counter)
        {
            CurrentGain = gain + step*step_count;
            return;
        }

        /* Mix until pos is aligned with 4 or the mix is done. */
        if(auto const leftover = realign_len&3)
        {
            auto const in = InSamples.subspan(pos, leftover);
            auto const out = dst.subspan(pos);

            std::ranges::transform(in, out, out.begin(),
                [TargetGain](f32 const val, f32 const dry) noexcept -> f32
                { return dry + val*TargetGain; });
            pos += leftover;
        }
    }
    CurrentGain = TargetGain;

    if(!(std::abs(TargetGain) > GainSilenceThreshold))
        return;
    if(auto const todo = (InSamples.size()-pos) >> 2)
    {
        /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) */
        auto const in4 = std::span{reinterpret_cast<const __m128*>(InSamples.data()),
            InSamples.size()/4}.last(todo);
        auto const out = dst.subspan(pos);
        auto const out4 = std::span{reinterpret_cast<__m128*>(out.data()), out.size()/4};
        /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

        auto const gain4 = _mm_set1_ps(TargetGain);
        std::ranges::transform(in4, out4, out4.begin(),
            [gain4](__m128 const val4, __m128 const dry4) -> __m128
            { return vmadd(dry4, val4, gain4); });
        pos += in4.size()*4;
    }
    if(auto const leftover = (InSamples.size()-pos)&3)
    {
        auto const in = InSamples.last(leftover);
        auto const out = dst.subspan(pos);

        std::ranges::transform(in, out, out.begin(),
            [TargetGain](f32 const val, f32 const dry) noexcept -> f32
            { return dry + val*TargetGain; });
    }
}

} // namespace

template<>
void Resample_<CubicTag,SSETag>(InterpState const *const state, std::span<f32 const> const src,
    u32 frac, u32 const increment, std::span<f32> const dst)
{
    ASSUME(frac < MixerFracOne);

    auto const filter = std::get<CubicState>(*state).filter;

    auto pos = usize{MaxResamplerEdge-1};
    std::ranges::generate(dst, [&pos,&frac,src,increment,filter]() -> f32
    {
        auto const pi = usize{frac >> CubicPhaseDiffBits}; ASSUME(pi < CubicPhaseCount);
        auto const pf = gsl::narrow_cast<f32>(frac&CubicPhaseDiffMask)*(1.0f/CubicPhaseDiffOne);
        auto const pf4 = _mm_set1_ps(pf);

        /* Apply the phase interpolated filter. */

        /* f = fil + pf*phd */
        auto const f4 = vmadd(_mm_load_ps(filter[pi].mCoeffs.data()), pf4,
            _mm_load_ps(filter[pi].mDeltas.data()));
        /* r = f*src */
        auto r4 = _mm_mul_ps(f4, _mm_loadu_ps(&src[pos]));

        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
        auto const output = _mm_cvtss_f32(r4);

        frac += increment;
        pos  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

template<>
void Resample_<BSincTag,SSETag>(InterpState const *const state, std::span<f32 const> const src,
    u32 frac, u32 const increment, std::span<f32> const dst)
{
    auto const &bsinc = std::get<BsincState>(*state);
    auto const sf4 = _mm_set1_ps(bsinc.sf);
    auto const m = usize{bsinc.m};
    ASSUME(m > 0);
    ASSUME(m <= MaxResamplerPadding);
    ASSUME(frac < MixerFracOne);

    auto const filter = bsinc.filter.first(4_uz*BSincPhaseCount*m);

    ASSUME(bsinc.l <= MaxResamplerEdge);
    auto pos = usize{MaxResamplerEdge-bsinc.l};
    std::ranges::generate(dst, [&pos,&frac,src,increment,sf4,m,filter]() -> f32
    {
        // Calculate the phase index and factor.
        auto const pi = usize{frac >> BSincPhaseDiffBits}; ASSUME(pi < BSincPhaseCount);
        auto const pf = gsl::narrow_cast<f32>(frac&BSincPhaseDiffMask)*(1.0f/BSincPhaseDiffOne);

        // Apply the scale and phase interpolated filter.
        auto r4 = _mm_setzero_ps();
        {
            auto const pf4 = _mm_set1_ps(pf);
            auto const fil = filter.subspan(2_uz*pi*m);
            auto const phd = fil.subspan(m);
            auto const scd = fil.subspan(2_uz*BSincPhaseCount*m);
            auto const spd = scd.subspan(m);
            auto td = m >> 2;
            auto j = 0_uz;

            do {
                /* f = ((fil + sf*scd) + pf*(phd + sf*spd)) */
                auto const f4 = vmadd(
                    vmadd(_mm_load_ps(&fil[j]), sf4, _mm_load_ps(&scd[j])),
                    pf4, vmadd(_mm_load_ps(&phd[j]), sf4, _mm_load_ps(&spd[j])));
                /* r += f*src */
                r4 = vmadd(r4, f4, _mm_loadu_ps(&src[pos+j]));
                j += 4;
            } while(--td);
        }
        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
        auto const output = _mm_cvtss_f32(r4);

        frac += increment;
        pos  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

template<>
void Resample_<FastBSincTag,SSETag>(InterpState const *const state, std::span<f32 const> const src,
    u32 frac, u32 const increment, std::span<f32> const dst)
{
    auto const &bsinc = std::get<BsincState>(*state);
    auto const m = usize{bsinc.m};
    ASSUME(m > 0);
    ASSUME(m <= MaxResamplerPadding);
    ASSUME(frac < MixerFracOne);

    auto const filter = bsinc.filter.first(2_uz*m*BSincPhaseCount);

    ASSUME(bsinc.l <= MaxResamplerEdge);
    auto pos = usize{MaxResamplerEdge-bsinc.l};
    std::ranges::generate(dst, [&pos,&frac,src,increment,filter,m]() -> f32
    {
        // Calculate the phase index and factor.
        auto const pi = usize{frac >> BSincPhaseDiffBits}; ASSUME(pi < BSincPhaseCount);
        auto const pf = gsl::narrow_cast<f32>(frac&BSincPhaseDiffMask)*(1.0f/BSincPhaseDiffOne);

        // Apply the phase interpolated filter.
        auto r4 = _mm_setzero_ps();
        {
            auto const pf4 = _mm_set1_ps(pf);
            auto const fil = filter.subspan(2_uz*m*pi);
            auto const phd = fil.subspan(m);
            auto td = m >> 2;
            auto j = 0_uz;

            do {
                /* f = fil + pf*phd */
                auto const f4 = vmadd(_mm_load_ps(&fil[j]), pf4, _mm_load_ps(&phd[j]));
                /* r += f*src */
                r4 = vmadd(r4, f4, _mm_loadu_ps(&src[pos+j]));
                j += 4;
            } while(--td);
        }
        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
        auto const output = _mm_cvtss_f32(r4);

        frac += increment;
        pos  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}


template<>
void MixHrtf_<SSETag>(std::span<f32 const> const InSamples, std::span<f32x2> const AccumSamples,
    u32 const IrSize, MixHrtfFilter const *const hrtfparams, usize const SamplesToDo)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, SamplesToDo); }

template<>
void MixHrtfBlend_<SSETag>(std::span<f32 const> const InSamples,
    std::span<f32x2> const AccumSamples, u32 const IrSize, HrtfFilter const *const oldparams,
    MixHrtfFilter const *const newparams, usize const SamplesToDo)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        SamplesToDo);
}

template<>
void MixDirectHrtf_<SSETag>(FloatBufferSpan const LeftOut, FloatBufferSpan const RightOut,
    std::span<FloatBufferLine const> const InSamples, std::span<f32x2> const AccumSamples,
    std::span<f32, BufferLineSize> const TempBuf, std::span<HrtfChannelState> const ChanState,
    usize const IrSize, usize const SamplesToDo)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, TempBuf, ChanState,
        IrSize, SamplesToDo);
}


template<>
void Mix_<SSETag>(std::span<f32 const> const InSamples,
    std::span<FloatBufferLine> const OutBuffer, std::span<f32> const CurrentGains,
    std::span<f32 const> const TargetGains, usize const Counter, usize const OutPos)
{
    if((OutPos&3) != 0) [[unlikely]]
        return Mix_<CTag>(InSamples, OutBuffer, CurrentGains, TargetGains, Counter, OutPos);

    auto const delta = (Counter > 0) ? 1.0f / gsl::narrow_cast<f32>(Counter) : 0.0f;
    auto const fade_len = std::min(Counter, InSamples.size());
    auto const realign_len = std::min((fade_len+3_uz) & ~3_uz, InSamples.size()) - fade_len;

    auto curgains = CurrentGains.begin();
    auto targetgains = TargetGains.begin();
    for(FloatBufferSpan const output : OutBuffer)
        MixLine(InSamples, output.subspan(OutPos), *curgains++, *targetgains++, delta, fade_len,
            realign_len, Counter);
}

template<>
void Mix_<SSETag>(std::span<f32 const> const InSamples, std::span<f32> const OutBuffer,
    f32 &CurrentGain, f32 const TargetGain, usize const Counter)
{
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    if((reinterpret_cast<uintptr_t>(OutBuffer.data())&15) != 0) [[unlikely]]
        return Mix_<CTag>(InSamples, OutBuffer, CurrentGain, TargetGain, Counter);

    auto const delta = (Counter > 0) ? 1.0f / gsl::narrow_cast<f32>(Counter) : 0.0f;
    auto const fade_len = std::min(Counter, InSamples.size());
    auto const realign_len = std::min((fade_len+3_uz) & ~3_uz, InSamples.size()) - fade_len;

    MixLine(InSamples, OutBuffer, CurrentGain, TargetGain, delta, fade_len, realign_len, Counter);
}
