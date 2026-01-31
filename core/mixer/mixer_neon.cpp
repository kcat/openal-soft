#include "config.h"

#include <arm_neon.h>

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


#if defined(__GNUC__) && !defined(__clang__) && !defined(__ARM_NEON)
#pragma GCC target("fpu=neon")
#endif

namespace {

constexpr auto BSincPhaseDiffBits = u32{MixerFracBits - BSincPhaseBits};
constexpr auto BSincPhaseDiffOne = 1_u32 << BSincPhaseDiffBits;
constexpr auto BSincPhaseDiffMask = BSincPhaseDiffOne - 1_u32;

constexpr auto CubicPhaseDiffBits = u32{MixerFracBits - CubicPhaseBits};
constexpr auto CubicPhaseDiffOne = 1_u32 << CubicPhaseDiffBits;
constexpr auto CubicPhaseDiffMask = CubicPhaseDiffOne - 1_u32;

force_inline
void vtranspose4(float32x4_t &x0, float32x4_t &x1, float32x4_t &x2, float32x4_t &x3) noexcept
{
    auto t0_ = vzipq_f32(x0, x2);
    auto t1_ = vzipq_f32(x1, x3);
    auto u0_ = vzipq_f32(t0_.val[0], t1_.val[0]);
    auto u1_ = vzipq_f32(t0_.val[1], t1_.val[1]);
    x0 = u0_.val[0];
    x1 = u0_.val[1];
    x2 = u1_.val[0];
    x3 = u1_.val[1];
}

inline auto set_f4(float const l0, float const l1, float const l2, float const l3) -> float32x4_t
{
    auto ret = vmovq_n_f32(l0);
    ret = vsetq_lane_f32(l1, ret, 1);
    ret = vsetq_lane_f32(l2, ret, 2);
    ret = vsetq_lane_f32(l3, ret, 3);
    return ret;
}

inline void ApplyCoeffs(std::span<f32x2> const Values, usize const IrSize,
    ConstHrirSpan const Coeffs, float const left, float const right)
{
    ASSUME(IrSize >= MinIrLength);
    ASSUME(IrSize <= HrirLength);

    auto const leftright2 = vset_lane_f32(right, vmov_n_f32(left), 1);
    auto const leftright4 = vcombine_f32(leftright2, leftright2);

    /* Using a loop here instead of std::transform since some builds seem to
     * have an issue with accessing an array/span of float32x4_t.
     */
    for(auto c = 0_uz;c < IrSize;c += 2)
    {
        auto vals = vld1q_f32(&Values[c][0]);
        vals = vmlaq_f32(vals, vld1q_f32(&Coeffs[c][0]), leftright4);
        vst1q_f32(&Values[c][0], vals);
    }
}

force_inline void MixLine(std::span<float const> const InSamples, std::span<float> const dst,
    float &CurrentGain, float const TargetGain, float const delta, usize const fade_len,
    usize const realign_len, usize Counter)
{
    auto const step = float{(TargetGain-CurrentGain) * delta};

    auto pos = 0_uz;
    if(std::abs(step) > std::numeric_limits<float>::epsilon())
    {
        auto const gain = CurrentGain;
        auto step_count = 0.0f;
        /* Mix with applying gain steps in aligned multiples of 4. */
        if(auto const todo = fade_len >> 2)
        {
            auto const four4 = vdupq_n_f32(4.0f);
            auto const step4 = vdupq_n_f32(step);
            auto const gain4 = vdupq_n_f32(gain);
            auto step_count4 = set_f4(0.0f, 1.0f, 2.0f, 3.0f);

            /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) */
            auto const in4 = std::span{reinterpret_cast<const float32x4_t*>(InSamples.data()),
                InSamples.size()/4}.first(todo);
            auto const out4 = std::span{reinterpret_cast<float32x4_t*>(dst.data()), dst.size()/4};
            /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

            std::ranges::transform(in4, out4, out4.begin(),
                [gain4,step4,four4,&step_count4](float32x4_t const val4, float32x4_t dry4)
            {
                /* dry += val * (gain + step*step_count) */
                dry4 = vmlaq_f32(dry4, val4, vmlaq_f32(gain4, step4, step_count4));
                step_count4 = vaddq_f32(step_count4, four4);
                return dry4;
            });
            pos += in4.size()*4;

            /* NOTE: step_count4 now represents the next four counts after the
             * last four mixed samples, so the lowest element represents the
             * next step count to apply.
             */
            step_count = vgetq_lane_f32(step_count4, 0);
        }
        /* Mix with applying left over gain steps that aren't aligned multiples of 4. */
        if(auto const leftover = fade_len&3)
        {
            auto const in = InSamples.subspan(pos, leftover);
            auto const out = dst.subspan(pos);

            std::ranges::transform(in, out, out.begin(),
                [gain,step,&step_count](float const val, float dry) noexcept -> float
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
                [TargetGain](float const val, float const dry) noexcept -> float
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
        auto const in4 = std::span{reinterpret_cast<const float32x4_t*>(InSamples.data()),
            InSamples.size()/4}.last(todo);
        auto const out = dst.subspan(pos);
        auto const out4 = std::span{reinterpret_cast<float32x4_t*>(out.data()), out.size()/4};
        /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

        auto const gain4 = vdupq_n_f32(TargetGain);
        std::ranges::transform(in4, out4, out4.begin(),
            [gain4](float32x4_t const val4, float32x4_t const dry4) -> float32x4_t
            { return vmlaq_f32(dry4, val4, gain4); });
        pos += in4.size()*4;
    }
    if(auto const leftover = (InSamples.size()-pos)&3)
    {
        auto const in = InSamples.last(leftover);
        auto const out = dst.subspan(pos);

        std::ranges::transform(in, out, out.begin(),
            [TargetGain](float const val, float const dry) noexcept -> float
            { return dry + val*TargetGain; });
    }
}

} // namespace

void Resample_Linear_NEON(InterpState const*, std::span<float const> const src, u32 frac,
    u32 const increment, std::span<float> const dst)
{
    ASSUME(frac < MixerFracOne);

    auto const increment4 = vdupq_n_u32(increment*4u);
    auto const fracMask4 = vdupq_n_u32(MixerFracMask);
    auto const fracOne4 = vdupq_n_f32(1.0f/MixerFracOne);

    alignas(16) auto pos_ = std::array<u32, 4>{};
    alignas(16) auto frac_ = std::array<u32, 4>{};
    InitPosArrays(MaxResamplerEdge, frac, increment, std::span{frac_}, std::span{pos_});
    auto frac4 = vld1q_u32(frac_.data());
    auto pos4 = vld1q_u32(pos_.data());

    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    std::ranges::generate(std::span{reinterpret_cast<float32x4_t*>(dst.data()), dst.size()/4},
        [src,increment4,fracMask4,fracOne4,&pos4,&frac4]
    {
        auto const pos0 = vgetq_lane_u32(pos4, 0);
        auto const pos1 = vgetq_lane_u32(pos4, 1);
        auto const pos2 = vgetq_lane_u32(pos4, 2);
        auto const pos3 = vgetq_lane_u32(pos4, 3);
        ASSUME(pos0 <= pos1); ASSUME(pos1 <= pos2); ASSUME(pos2 <= pos3);
        auto const val1 = set_f4(src[pos0], src[pos1], src[pos2], src[pos3]);
        auto const val2 = set_f4(src[pos0+1_uz], src[pos1+1_uz], src[pos2+1_uz], src[pos3+1_uz]);

        /* val1 + (val2-val1)*mu */
        auto const r0 = vsubq_f32(val2, val1);
        auto const mu = vmulq_f32(vcvtq_f32_u32(frac4), fracOne4);
        auto const out = vmlaq_f32(val1, mu, r0);

        frac4 = vaddq_u32(frac4, increment4);
        pos4 = vaddq_u32(pos4, vshrq_n_u32(frac4, MixerFracBits));
        frac4 = vandq_u32(frac4, fracMask4);
        return out;
    });

    if(auto const todo = dst.size()&3)
    {
        auto pos = usize{vgetq_lane_u32(pos4, 0)};
        frac = vgetq_lane_u32(frac4, 0);

        std::ranges::generate(dst.last(todo), [&pos,&frac,src,increment]
        {
            auto const output = lerpf(src[pos+0], src[pos+1],
                gsl::narrow_cast<float>(frac) * (1.0f/MixerFracOne));

            frac += increment;
            pos  += frac>>MixerFracBits;
            frac &= MixerFracMask;
            return output;
        });
    }
}

void Resample_Cubic_NEON(InterpState const *const state, std::span<float const> const src,
    u32 frac, u32 const increment, std::span<float> const dst)
{
    ASSUME(frac < MixerFracOne);

    auto const filter = std::get<CubicState>(*state).filter;

    auto const increment4 = vdupq_n_u32(increment*4u);
    auto const fracMask4 = vdupq_n_u32(MixerFracMask);
    auto const fracDiffOne4 = vdupq_n_f32(1.0f/CubicPhaseDiffOne);
    auto const fracDiffMask4 = vdupq_n_u32(CubicPhaseDiffMask);

    alignas(16) auto pos_ = std::array<u32, 4>{};
    alignas(16) auto frac_ = std::array<u32, 4>{};
    InitPosArrays(MaxResamplerEdge-1, frac, increment, std::span{frac_}, std::span{pos_});
    auto frac4 = vld1q_u32(frac_.data());
    auto pos4 = vld1q_u32(pos_.data());

    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    std::ranges::generate(std::span{reinterpret_cast<float32x4_t*>(dst.data()), dst.size()/4},
        [src,filter,increment4,fracMask4,fracDiffOne4,fracDiffMask4,&pos4,&frac4]
    {
        auto const pos0 = vgetq_lane_u32(pos4, 0);
        auto const pos1 = vgetq_lane_u32(pos4, 1);
        auto const pos2 = vgetq_lane_u32(pos4, 2);
        auto const pos3 = vgetq_lane_u32(pos4, 3);
        ASSUME(pos0 <= pos1); ASSUME(pos1 <= pos2); ASSUME(pos2 <= pos3);
        auto const val0 = vld1q_f32(&src[pos0]);
        auto const val1 = vld1q_f32(&src[pos1]);
        auto const val2 = vld1q_f32(&src[pos2]);
        auto const val3 = vld1q_f32(&src[pos3]);

        auto const pi4 = vshrq_n_u32(frac4, CubicPhaseDiffBits);
        auto const pi0 = vgetq_lane_u32(pi4, 0); ASSUME(pi0 < CubicPhaseCount);
        auto const pi1 = vgetq_lane_u32(pi4, 1); ASSUME(pi1 < CubicPhaseCount);
        auto const pi2 = vgetq_lane_u32(pi4, 2); ASSUME(pi2 < CubicPhaseCount);
        auto const pi3 = vgetq_lane_u32(pi4, 3); ASSUME(pi3 < CubicPhaseCount);

        auto const pf4 = vmulq_f32(vcvtq_f32_u32(vandq_u32(frac4, fracDiffMask4)),
            fracDiffOne4);

        auto r0 = vmulq_f32(val0,
            vmlaq_f32(vld1q_f32(filter[pi0].mCoeffs.data()), vdupq_lane_f32(vget_low_f32(pf4), 0),
                vld1q_f32(filter[pi0].mDeltas.data())));
        auto r1 = vmulq_f32(val1,
            vmlaq_f32(vld1q_f32(filter[pi1].mCoeffs.data()), vdupq_lane_f32(vget_low_f32(pf4), 1),
                vld1q_f32(filter[pi1].mDeltas.data())));
        auto r2 = vmulq_f32(val2,
            vmlaq_f32(vld1q_f32(filter[pi2].mCoeffs.data()), vdupq_lane_f32(vget_high_f32(pf4), 0),
                vld1q_f32(filter[pi2].mDeltas.data())));
        auto r3 = vmulq_f32(val3,
            vmlaq_f32(vld1q_f32(filter[pi3].mCoeffs.data()), vdupq_lane_f32(vget_high_f32(pf4), 1),
                vld1q_f32(filter[pi3].mDeltas.data())));

        vtranspose4(r0, r1, r2, r3);
        r0 = vaddq_f32(vaddq_f32(r0, r1), vaddq_f32(r2, r3));

        frac4 = vaddq_u32(frac4, increment4);
        pos4 = vaddq_u32(pos4, vshrq_n_u32(frac4, MixerFracBits));
        frac4 = vandq_u32(frac4, fracMask4);
        return r0;
    });

    if(auto const todo = dst.size()&3)
    {
        auto pos = usize{vgetq_lane_u32(pos4, 0)};
        frac = vgetq_lane_u32(frac4, 0);

        std::ranges::generate(dst.last(todo), [&pos,&frac,src,increment,filter]
        {
            auto const pi = frac >> CubicPhaseDiffBits; ASSUME(pi < CubicPhaseCount);
            auto const pf = gsl::narrow_cast<float>(frac&CubicPhaseDiffMask)
                * float{1.0f/CubicPhaseDiffOne};
            auto const pf4 = vdupq_n_f32(pf);

            auto const f4 = vmlaq_f32(vld1q_f32(filter[pi].mCoeffs.data()), pf4,
                vld1q_f32(filter[pi].mDeltas.data()));
            auto r4 = vmulq_f32(f4, vld1q_f32(&src[pos]));

            r4 = vaddq_f32(r4, vrev64q_f32(r4));
            auto const output = vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0);

            frac += increment;
            pos  += frac>>MixerFracBits;
            frac &= MixerFracMask;
            return output;
        });
    }
}

void Resample_FastBSinc_NEON(InterpState const *const state, std::span<float const> const src,
    u32 frac, u32 const increment, std::span<float> const dst)
{
    auto const &bsinc = std::get<BsincState>(*state);
    auto const m = usize{bsinc.m};
    ASSUME(m > 0);
    ASSUME(m <= MaxResamplerPadding);
    ASSUME(frac < MixerFracOne);

    auto const filter = bsinc.filter.first(2_uz*BSincPhaseCount*m);

    ASSUME(bsinc.l <= MaxResamplerEdge);
    auto pos = usize{MaxResamplerEdge-bsinc.l};
    std::ranges::generate(dst, [&pos,&frac,src,increment,m,filter]() -> float
    {
        // Calculate the phase index and factor.
        auto const pi = frac >> BSincPhaseDiffBits; ASSUME(pi < BSincPhaseCount);
        auto const pf = static_cast<float>(frac&BSincPhaseDiffMask)*float{1.0f/BSincPhaseDiffOne};

        // Apply the phase interpolated filter.
        auto r4 = vdupq_n_f32(0.0f);
        {
            auto const pf4 = vdupq_n_f32(pf);
            auto const fil = filter.subspan(2_uz*pi*m);
            auto const phd = fil.subspan(m);
            auto td = m >> 2_uz;
            auto j = 0_uz;

            do {
                /* f = fil + pf*phd */
                auto const f4 = vmlaq_f32(vld1q_f32(&fil[j]), pf4, vld1q_f32(&phd[j]));
                /* r += f*src */
                r4 = vmlaq_f32(r4, f4, vld1q_f32(&src[pos+j]));
                j += 4;
            } while(--td);
        }
        r4 = vaddq_f32(r4, vrev64q_f32(r4));
        auto const output = vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0);

        frac += increment;
        pos  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

void Resample_BSinc_NEON(InterpState const *const state, std::span<float const> const src,
    u32 frac, u32 const increment, std::span<float> const dst)
{
    auto const &bsinc = std::get<BsincState>(*state);
    auto const sf4 = vdupq_n_f32(bsinc.sf);
    auto const m = usize{bsinc.m};
    ASSUME(m > 0);
    ASSUME(m <= MaxResamplerPadding);
    ASSUME(frac < MixerFracOne);

    auto const filter = bsinc.filter.first(4_uz*BSincPhaseCount*m);

    ASSUME(bsinc.l <= MaxResamplerEdge);
    auto pos = usize{MaxResamplerEdge-bsinc.l};
    std::ranges::generate(dst, [&pos,&frac,src,increment,sf4,m,filter]() -> float
    {
        // Calculate the phase index and factor.
        auto const pi = frac >> BSincPhaseDiffBits; ASSUME(pi < BSincPhaseCount);
        auto const pf = static_cast<float>(frac&BSincPhaseDiffMask)*float{1.0f/BSincPhaseDiffOne};

        // Apply the scale and phase interpolated filter.
        auto r4 = vdupq_n_f32(0.0f);
        {
            auto const pf4 = vdupq_n_f32(pf);
            auto const fil = filter.subspan(2_uz*pi*m);
            auto const phd = fil.subspan(m);
            auto const scd = fil.subspan(2_uz*BSincPhaseCount*m);
            auto const spd = scd.subspan(m);
            auto td = m >> 2_uz;
            auto j = 0_uz;

            do {
                /* f = ((fil + sf*scd) + pf*(phd + sf*spd)) */
                auto const f4 = vmlaq_f32(
                    vmlaq_f32(vld1q_f32(&fil[j]), sf4, vld1q_f32(&scd[j])),
                    pf4, vmlaq_f32(vld1q_f32(&phd[j]), sf4, vld1q_f32(&spd[j])));
                /* r += f*src */
                r4 = vmlaq_f32(r4, f4, vld1q_f32(&src[pos+j]));
                j += 4;
            } while(--td);
        }
        r4 = vaddq_f32(r4, vrev64q_f32(r4));
        auto const output = vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0);

        frac += increment;
        pos  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}


void MixHrtf_NEON(std::span<float const> const InSamples, std::span<f32x2> const AccumSamples,
    u32 const IrSize, MixHrtfFilter const *const hrtfparams, usize const SamplesToDo)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, SamplesToDo); }

void MixHrtfBlend_NEON(std::span<float const> const InSamples, std::span<f32x2> const AccumSamples,
    u32 const IrSize, HrtfFilter const *const oldparams, MixHrtfFilter const *const newparams,
    usize const SamplesToDo)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        SamplesToDo);
}

void MixDirectHrtf_NEON(FloatBufferSpan const LeftOut, FloatBufferSpan const RightOut,
    std::span<FloatBufferLine const> const InSamples, std::span<f32x2> const AccumSamples,
    std::span<float, BufferLineSize> const TempBuf, std::span<HrtfChannelState> const ChanState,
    usize const IrSize, usize const SamplesToDo)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, TempBuf, ChanState,
        IrSize, SamplesToDo);
}


void Mix_NEON(std::span<float const> const InSamples, std::span<FloatBufferLine> const OutBuffer,
    std::span<float> const CurrentGains, std::span<float const> const TargetGains,
    usize const Counter, usize const OutPos)
{
    if((OutPos&3) != 0) [[unlikely]]
        return Mix_C(InSamples, OutBuffer, CurrentGains, TargetGains, Counter, OutPos);

    auto const delta = (Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f;
    auto const fade_len = std::min(Counter, InSamples.size());
    auto const realign_len = std::min((fade_len+3_uz) & ~3_uz, InSamples.size()) - fade_len;

    auto curgains = CurrentGains.begin();
    auto targetgains = TargetGains.begin();
    for(FloatBufferSpan const output : OutBuffer)
        MixLine(InSamples, output.subspan(OutPos), *curgains++, *targetgains++, delta, fade_len,
            realign_len, Counter);
}

void Mix_NEON(std::span<float const> const InSamples, std::span<float> const OutBuffer,
    float &CurrentGain, float const TargetGain, usize const Counter)
{
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    if((reinterpret_cast<uintptr_t>(OutBuffer.data())&15) != 0) [[unlikely]]
        return Mix_C(InSamples, OutBuffer, CurrentGain, TargetGain, Counter);

    auto const delta = (Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f;
    auto const fade_len = std::min(Counter, InSamples.size());
    auto const realign_len = std::min((fade_len+3_uz) & ~3_uz, InSamples.size()) - fade_len;

    MixLine(InSamples, OutBuffer, CurrentGain, TargetGain, delta, fade_len, realign_len, Counter);
}
