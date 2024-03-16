#include "config.h"

#include <arm_neon.h>

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

struct NEONTag;
struct LerpTag;
struct CubicTag;
struct BSincTag;
struct FastBSincTag;


#if defined(__GNUC__) && !defined(__clang__) && !defined(__ARM_NEON)
#pragma GCC target("fpu=neon")
#endif

using uint = unsigned int;

namespace {

constexpr uint BSincPhaseDiffBits{MixerFracBits - BSincPhaseBits};
constexpr uint BSincPhaseDiffOne{1 << BSincPhaseDiffBits};
constexpr uint BSincPhaseDiffMask{BSincPhaseDiffOne - 1u};

constexpr uint CubicPhaseDiffBits{MixerFracBits - CubicPhaseBits};
constexpr uint CubicPhaseDiffOne{1 << CubicPhaseDiffBits};
constexpr uint CubicPhaseDiffMask{CubicPhaseDiffOne - 1u};

force_inline
void vtranspose4(float32x4_t &x0, float32x4_t &x1, float32x4_t &x2, float32x4_t &x3) noexcept
{
    float32x4x2_t t0_{vzipq_f32(x0, x2)};
    float32x4x2_t t1_{vzipq_f32(x1, x3)};
    float32x4x2_t u0_{vzipq_f32(t0_.val[0], t1_.val[0])};
    float32x4x2_t u1_{vzipq_f32(t0_.val[1], t1_.val[1])};
    x0 = u0_.val[0];
    x1 = u0_.val[1];
    x2 = u1_.val[0];
    x3 = u1_.val[1];
}

inline float32x4_t set_f4(float l0, float l1, float l2, float l3)
{
    float32x4_t ret{vmovq_n_f32(l0)};
    ret = vsetq_lane_f32(l1, ret, 1);
    ret = vsetq_lane_f32(l2, ret, 2);
    ret = vsetq_lane_f32(l3, ret, 3);
    return ret;
}

inline void ApplyCoeffs(float2 *RESTRICT Values, const size_t IrSize, const ConstHrirSpan Coeffs,
    const float left, const float right)
{
    auto dup_samples = [left,right]
    {
        float32x2_t leftright2{vset_lane_f32(right, vmov_n_f32(left), 1)};
        return vcombine_f32(leftright2, leftright2);
    };
    const float32x4_t leftright4{dup_samples()};

    ASSUME(IrSize >= MinIrLength);
    for(size_t c{0};c < IrSize;c += 2)
    {
        float32x4_t vals = vld1q_f32(&Values[c][0]);
        float32x4_t coefs = vld1q_f32(&Coeffs[c][0]);

        vals = vmlaq_f32(vals, coefs, leftright4);

        vst1q_f32(&Values[c][0], vals);
    }
}

force_inline void MixLine(const al::span<const float> InSamples, float *RESTRICT dst,
    float &CurrentGain, const float TargetGain, const float delta, const size_t min_len,
    const size_t aligned_len, size_t Counter)
{
    float gain{CurrentGain};
    const float step{(TargetGain-gain) * delta};

    size_t pos{0};
    if(!(std::abs(step) > std::numeric_limits<float>::epsilon()))
        gain = TargetGain;
    else
    {
        float step_count{0.0f};
        /* Mix with applying gain steps in aligned multiples of 4. */
        if(size_t todo{min_len >> 2})
        {
            const float32x4_t four4{vdupq_n_f32(4.0f)};
            const float32x4_t step4{vdupq_n_f32(step)};
            const float32x4_t gain4{vdupq_n_f32(gain)};
            float32x4_t step_count4{vdupq_n_f32(0.0f)};
            step_count4 = vsetq_lane_f32(1.0f, step_count4, 1);
            step_count4 = vsetq_lane_f32(2.0f, step_count4, 2);
            step_count4 = vsetq_lane_f32(3.0f, step_count4, 3);

            do {
                const float32x4_t val4 = vld1q_f32(&InSamples[pos]);
                float32x4_t dry4 = vld1q_f32(&dst[pos]);
                dry4 = vmlaq_f32(dry4, val4, vmlaq_f32(gain4, step4, step_count4));
                step_count4 = vaddq_f32(step_count4, four4);
                vst1q_f32(&dst[pos], dry4);
                pos += 4;
            } while(--todo);
            /* NOTE: step_count4 now represents the next four counts after the
             * last four mixed samples, so the lowest element represents the
             * next step count to apply.
             */
            step_count = vgetq_lane_f32(step_count4, 0);
        }
        /* Mix with applying left over gain steps that aren't aligned multiples of 4. */
        for(size_t leftover{min_len&3};leftover;++pos,--leftover)
        {
            dst[pos] += InSamples[pos] * (gain + step*step_count);
            step_count += 1.0f;
        }
        if(pos == Counter)
            gain = TargetGain;
        else
            gain += step*step_count;

        /* Mix until pos is aligned with 4 or the mix is done. */
        for(size_t leftover{aligned_len&3};leftover;++pos,--leftover)
            dst[pos] += InSamples[pos] * gain;
    }
    CurrentGain = gain;

    if(!(std::abs(gain) > GainSilenceThreshold))
        return;
    if(size_t todo{(InSamples.size()-pos) >> 2})
    {
        const float32x4_t gain4 = vdupq_n_f32(gain);
        do {
            const float32x4_t val4 = vld1q_f32(&InSamples[pos]);
            float32x4_t dry4 = vld1q_f32(&dst[pos]);
            dry4 = vmlaq_f32(dry4, val4, gain4);
            vst1q_f32(&dst[pos], dry4);
            pos += 4;
        } while(--todo);
    }
    for(size_t leftover{(InSamples.size()-pos)&3};leftover;++pos,--leftover)
        dst[pos] += InSamples[pos] * gain;
}

} // namespace

template<>
void Resample_<LerpTag,NEONTag>(const InterpState*, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{
    ASSUME(frac < MixerFracOne);

    const uint32x4_t increment4 = vdupq_n_u32(increment*4u);
    const float32x4_t fracOne4 = vdupq_n_f32(1.0f/MixerFracOne);
    const uint32x4_t fracMask4 = vdupq_n_u32(MixerFracMask);

    alignas(16) std::array<uint,4> pos_, frac_;
    InitPosArrays(frac, increment, al::span{frac_}, al::span{pos_});
    uint32x4_t frac4 = vld1q_u32(frac_.data());
    uint32x4_t pos4 = vld1q_u32(pos_.data());

    auto vecout = al::span<float32x4_t>{reinterpret_cast<float32x4_t*>(dst.data()), dst.size()/4};
    std::generate(vecout.begin(), vecout.end(), [=,&pos4,&frac4]() -> float32x4_t
    {
        const uint pos0{vgetq_lane_u32(pos4, 0)};
        const uint pos1{vgetq_lane_u32(pos4, 1)};
        const uint pos2{vgetq_lane_u32(pos4, 2)};
        const uint pos3{vgetq_lane_u32(pos4, 3)};
        const float32x4_t val1{set_f4(src[pos0], src[pos1], src[pos2], src[pos3])};
        const float32x4_t val2{set_f4(src[pos0+1_uz], src[pos1+1_uz], src[pos2+1_uz], src[pos3+1_uz])};

        /* val1 + (val2-val1)*mu */
        const float32x4_t r0{vsubq_f32(val2, val1)};
        const float32x4_t mu{vmulq_f32(vcvtq_f32_u32(frac4), fracOne4)};
        const float32x4_t out{vmlaq_f32(val1, mu, r0)};

        frac4 = vaddq_u32(frac4, increment4);
        pos4 = vaddq_u32(pos4, vshrq_n_u32(frac4, MixerFracBits));
        frac4 = vandq_u32(frac4, fracMask4);
        return out;
    });

    if(size_t todo{dst.size()&3})
    {
        src += vgetq_lane_u32(pos4, 0);
        frac = vgetq_lane_u32(frac4, 0);

        std::generate(dst.end()-ptrdiff_t(todo), dst.end(), [&src,&frac,increment]
        {
            const float out{lerpf(src[0], src[1], static_cast<float>(frac) * (1.0f/MixerFracOne))};

            frac += increment;
            src  += frac>>MixerFracBits;
            frac &= MixerFracMask;
            return out;
        });
    }
}

template<>
void Resample_<CubicTag,NEONTag>(const InterpState *state, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{
    ASSUME(frac < MixerFracOne);

    const auto filter = std::get<CubicState>(*state).filter;

    const uint32x4_t increment4{vdupq_n_u32(increment*4u)};
    const uint32x4_t fracMask4{vdupq_n_u32(MixerFracMask)};
    const float32x4_t fracDiffOne4{vdupq_n_f32(1.0f/CubicPhaseDiffOne)};
    const uint32x4_t fracDiffMask4{vdupq_n_u32(CubicPhaseDiffMask)};

    alignas(16) std::array<uint,4> pos_, frac_;
    InitPosArrays(frac, increment, al::span{frac_}, al::span{pos_});
    uint32x4_t frac4{vld1q_u32(frac_.data())};
    uint32x4_t pos4{vld1q_u32(pos_.data())};

    src -= 1;
    auto vecout = al::span<float32x4_t>{reinterpret_cast<float32x4_t*>(dst.data()), dst.size()/4};
    std::generate(vecout.begin(), vecout.end(), [=,&pos4,&frac4]() -> float32x4_t
    {
        const uint pos0{vgetq_lane_u32(pos4, 0)};
        const uint pos1{vgetq_lane_u32(pos4, 1)};
        const uint pos2{vgetq_lane_u32(pos4, 2)};
        const uint pos3{vgetq_lane_u32(pos4, 3)};
        const float32x4_t val0{vld1q_f32(src+pos0)};
        const float32x4_t val1{vld1q_f32(src+pos1)};
        const float32x4_t val2{vld1q_f32(src+pos2)};
        const float32x4_t val3{vld1q_f32(src+pos3)};

        const uint32x4_t pi4{vshrq_n_u32(frac4, CubicPhaseDiffBits)};
        const uint pi0{vgetq_lane_u32(pi4, 0)}; ASSUME(pi0 < CubicPhaseCount);
        const uint pi1{vgetq_lane_u32(pi4, 1)}; ASSUME(pi1 < CubicPhaseCount);
        const uint pi2{vgetq_lane_u32(pi4, 2)}; ASSUME(pi2 < CubicPhaseCount);
        const uint pi3{vgetq_lane_u32(pi4, 3)}; ASSUME(pi3 < CubicPhaseCount);

        const float32x4_t pf4{vmulq_f32(vcvtq_f32_u32(vandq_u32(frac4, fracDiffMask4)),
            fracDiffOne4)};

        float32x4_t r0{vmulq_f32(val0,
            vmlaq_f32(vld1q_f32(filter[pi0].mCoeffs.data()), vdupq_lane_f32(vget_low_f32(pf4), 0),
                vld1q_f32(filter[pi0].mDeltas.data())))};
        float32x4_t r1{vmulq_f32(val1,
            vmlaq_f32(vld1q_f32(filter[pi1].mCoeffs.data()), vdupq_lane_f32(vget_low_f32(pf4), 1),
                vld1q_f32(filter[pi1].mDeltas.data())))};
        float32x4_t r2{vmulq_f32(val2,
            vmlaq_f32(vld1q_f32(filter[pi2].mCoeffs.data()), vdupq_lane_f32(vget_high_f32(pf4), 0),
                vld1q_f32(filter[pi2].mDeltas.data())))};
        float32x4_t r3{vmulq_f32(val3,
            vmlaq_f32(vld1q_f32(filter[pi3].mCoeffs.data()), vdupq_lane_f32(vget_high_f32(pf4), 1),
                vld1q_f32(filter[pi3].mDeltas.data())))};

        vtranspose4(r0, r1, r2, r3);
        r0 = vaddq_f32(vaddq_f32(r0, r1), vaddq_f32(r2, r3));

        frac4 = vaddq_u32(frac4, increment4);
        pos4 = vaddq_u32(pos4, vshrq_n_u32(frac4, MixerFracBits));
        frac4 = vandq_u32(frac4, fracMask4);
        return r0;
    });

    if(const size_t todo{dst.size()&3})
    {
        src += vgetq_lane_u32(pos4, 0);
        frac = vgetq_lane_u32(frac4, 0);

        std::generate(dst.end()-ptrdiff_t(todo), dst.end(), [&src,&frac,increment,filter]
        {
            const uint pi{frac >> CubicPhaseDiffBits}; ASSUME(pi < CubicPhaseCount);
            const float pf{static_cast<float>(frac&CubicPhaseDiffMask) * (1.0f/CubicPhaseDiffOne)};
            const float32x4_t pf4{vdupq_n_f32(pf)};

            const float32x4_t f4{vmlaq_f32(vld1q_f32(filter[pi].mCoeffs.data()), pf4,
                vld1q_f32(filter[pi].mDeltas.data()))};
            float32x4_t r4{vmulq_f32(f4, vld1q_f32(src))};

            r4 = vaddq_f32(r4, vrev64q_f32(r4));
            const float output{vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0)};

            frac += increment;
            src  += frac>>MixerFracBits;
            frac &= MixerFracMask;
            return output;
        });
    }
}

template<>
void Resample_<BSincTag,NEONTag>(const InterpState *state, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{
    const auto &bsinc = std::get<BsincState>(*state);
    const float *const filter{bsinc.filter};
    const float32x4_t sf4{vdupq_n_f32(bsinc.sf)};
    const size_t m{bsinc.m};
    ASSUME(m > 0);
    ASSUME(frac < MixerFracOne);

    src -= bsinc.l;
    std::generate(dst.begin(), dst.end(), [&src,&frac,increment,filter,sf4,m]() -> float
    {
        // Calculate the phase index and factor.
        const uint pi{frac >> BSincPhaseDiffBits};
        const float pf{static_cast<float>(frac&BSincPhaseDiffMask) * (1.0f/BSincPhaseDiffOne)};

        // Apply the scale and phase interpolated filter.
        float32x4_t r4{vdupq_n_f32(0.0f)};
        {
            const float32x4_t pf4{vdupq_n_f32(pf)};
            const float *fil{filter + m*pi*2_uz};
            const float *phd{fil + m};
            const float *scd{fil + BSincPhaseCount*2_uz*m};
            const float *spd{scd + m};
            size_t td{m >> 2};
            size_t j{0u};

            do {
                /* f = ((fil + sf*scd) + pf*(phd + sf*spd)) */
                const float32x4_t f4 = vmlaq_f32(
                    vmlaq_f32(vld1q_f32(&fil[j]), sf4, vld1q_f32(&scd[j])),
                    pf4, vmlaq_f32(vld1q_f32(&phd[j]), sf4, vld1q_f32(&spd[j])));
                /* r += f*src */
                r4 = vmlaq_f32(r4, f4, vld1q_f32(&src[j]));
                j += 4;
            } while(--td);
        }
        r4 = vaddq_f32(r4, vrev64q_f32(r4));
        const float output{vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0)};

        frac += increment;
        src  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

template<>
void Resample_<FastBSincTag,NEONTag>(const InterpState *state, const float *src,
    uint frac, const uint increment, const al::span<float> dst)
{
    const auto &bsinc = std::get<BsincState>(*state);
    const float *const filter{bsinc.filter};
    const size_t m{bsinc.m};
    ASSUME(m > 0);
    ASSUME(frac < MixerFracOne);

    src -= bsinc.l;
    std::generate(dst.begin(), dst.end(), [&src,&frac,increment,filter,m]() -> float
    {
        // Calculate the phase index and factor.
        const uint pi{frac >> BSincPhaseDiffBits};
        const float pf{static_cast<float>(frac&BSincPhaseDiffMask) * (1.0f/BSincPhaseDiffOne)};

        // Apply the phase interpolated filter.
        float32x4_t r4{vdupq_n_f32(0.0f)};
        {
            const float32x4_t pf4{vdupq_n_f32(pf)};
            const float *fil{filter + m*pi*2_uz};
            const float *phd{fil + m};
            size_t td{m >> 2};
            size_t j{0u};

            do {
                /* f = fil + pf*phd */
                const float32x4_t f4 = vmlaq_f32(vld1q_f32(&fil[j]), pf4, vld1q_f32(&phd[j]));
                /* r += f*src */
                r4 = vmlaq_f32(r4, f4, vld1q_f32(&src[j]));
                j += 4;
            } while(--td);
        }
        r4 = vaddq_f32(r4, vrev64q_f32(r4));
        const float output{vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0)};

        frac += increment;
        src  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}


template<>
void MixHrtf_<NEONTag>(const float *InSamples, float2 *AccumSamples, const uint IrSize,
    const MixHrtfFilter *hrtfparams, const size_t BufferSize)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, BufferSize); }

template<>
void MixHrtfBlend_<NEONTag>(const float *InSamples, float2 *AccumSamples, const uint IrSize,
    const HrtfFilter *oldparams, const MixHrtfFilter *newparams, const size_t BufferSize)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        BufferSize);
}

template<>
void MixDirectHrtf_<NEONTag>(const FloatBufferSpan LeftOut, const FloatBufferSpan RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples,
    const al::span<float,BufferLineSize> TempBuf, HrtfChannelState *ChanState, const size_t IrSize,
    const size_t BufferSize)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, TempBuf, ChanState,
        IrSize, BufferSize);
}


template<>
void Mix_<NEONTag>(const al::span<const float> InSamples, const al::span<FloatBufferLine> OutBuffer,
    float *CurrentGains, const float *TargetGains, const size_t Counter, const size_t OutPos)
{
    const float delta{(Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f};
    const auto min_len = std::min(Counter, InSamples.size());
    const auto aligned_len = std::min((min_len+3_uz) & ~3_uz, InSamples.size()) - min_len;

    for(FloatBufferLine &output : OutBuffer)
        MixLine(InSamples, al::assume_aligned<16>(output.data()+OutPos), *CurrentGains++,
            *TargetGains++, delta, min_len, aligned_len, Counter);
}

template<>
void Mix_<NEONTag>(const al::span<const float> InSamples, float *OutBuffer, float &CurrentGain,
    const float TargetGain, const size_t Counter)
{
    const float delta{(Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f};
    const auto min_len = std::min(Counter, InSamples.size());
    const auto aligned_len = std::min((min_len+3_uz) & ~3_uz, InSamples.size()) - min_len;

    MixLine(InSamples, al::assume_aligned<16>(OutBuffer), CurrentGain, TargetGain, delta, min_len,
        aligned_len, Counter);
}
