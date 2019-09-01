#include "config.h"

#include <arm_neon.h>

#include <limits>

#include "AL/al.h"
#include "AL/alc.h"
#include "alcmain.h"
#include "alu.h"
#include "hrtf.h"
#include "defs.h"
#include "hrtfbase.h"



template<>
const ALfloat *Resample_<LerpTag,NEONTag>(const InterpState*, const ALfloat *RESTRICT src,
    ALuint frac, ALint increment, const al::span<float> dst)
{
    const int32x4_t increment4 = vdupq_n_s32(increment*4);
    const float32x4_t fracOne4 = vdupq_n_f32(1.0f/FRACTIONONE);
    const int32x4_t fracMask4 = vdupq_n_s32(FRACTIONMASK);
    alignas(16) ALsizei pos_[4], frac_[4];
    int32x4_t pos4, frac4;

    ASSUME(increment > 0);

    InitiatePositionArrays(frac, increment, frac_, pos_, 4);
    frac4 = vld1q_s32(frac_);
    pos4 = vld1q_s32(pos_);

    auto dst_iter = dst.begin();
    const auto aligned_end = (dst.size()&~3) + dst_iter;
    while(dst_iter != aligned_end)
    {
        const int pos0{vgetq_lane_s32(pos4, 0)};
        const int pos1{vgetq_lane_s32(pos4, 1)};
        const int pos2{vgetq_lane_s32(pos4, 2)};
        const int pos3{vgetq_lane_s32(pos4, 3)};
        const float32x4_t val1{src[pos0], src[pos1], src[pos2], src[pos3]};
        const float32x4_t val2{src[pos0+1], src[pos1+1], src[pos2+1], src[pos3+1]};

        /* val1 + (val2-val1)*mu */
        const float32x4_t r0{vsubq_f32(val2, val1)};
        const float32x4_t mu{vmulq_f32(vcvtq_f32_s32(frac4), fracOne4)};
        const float32x4_t out{vmlaq_f32(val1, mu, r0)};

        vst1q_f32(dst_iter, out);
        dst_iter += 4;

        frac4 = vaddq_s32(frac4, increment4);
        pos4 = vaddq_s32(pos4, vshrq_n_s32(frac4, FRACTIONBITS));
        frac4 = vandq_s32(frac4, fracMask4);
    }

    /* NOTE: These four elements represent the position *after* the last four
     * samples, so the lowest element is the next position to resample.
     */
    src += static_cast<ALuint>(vgetq_lane_s32(pos4, 0));
    frac = vgetq_lane_s32(frac4, 0);

    while(dst_iter != dst.end())
    {
        *(dst_iter++) = lerp(src[0], src[1], frac * (1.0f/FRACTIONONE));

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
    return dst.begin();
}

template<>
const ALfloat *Resample_<BSincTag,NEONTag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALuint frac, ALint increment, const al::span<float> dst)
{
    const ALfloat *const filter{state->bsinc.filter};
    const float32x4_t sf4{vdupq_n_f32(state->bsinc.sf)};
    const ptrdiff_t m{state->bsinc.m};

    ASSUME(m > 0);
    ASSUME(increment > 0);

    src -= state->bsinc.l;
    for(float &out_sample : dst)
    {
        // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
        const ALuint pi{frac >> FRAC_PHASE_BITDIFF};
        const ALfloat pf{(frac & ((1<<FRAC_PHASE_BITDIFF)-1)) * (1.0f/(1<<FRAC_PHASE_BITDIFF))};
#undef FRAC_PHASE_BITDIFF

        // Apply the scale and phase interpolated filter.
        float32x4_t r4{vdupq_n_f32(0.0f)};
        {
            const float32x4_t pf4{vdupq_n_f32(pf)};
            const float *fil{filter + m*pi*4};
            const float *scd{fil + m};
            const float *phd{scd + m};
            const float *spd{phd + m};
            ptrdiff_t td{m >> 2};
            size_t j{0u};

            do {
                /* f = ((fil + sf*scd) + pf*(phd + sf*spd)) */
                const float32x4_t f4 = vmlaq_f32(
                    vmlaq_f32(vld1q_f32(fil), sf4, vld1q_f32(scd)),
                    pf4, vmlaq_f32(vld1q_f32(phd), sf4, vld1q_f32(spd)));
                fil += 4; scd += 4; phd += 4; spd += 4;
                /* r += f*src */
                r4 = vmlaq_f32(r4, f4, vld1q_f32(&src[j]));
                j += 4;
            } while(--td);
        }
        r4 = vaddq_f32(r4, vcombine_f32(vrev64_f32(vget_high_f32(r4)),
                                        vrev64_f32(vget_low_f32(r4))));
        out_sample = vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0);

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
    return dst.begin();
}


static inline void ApplyCoeffs(size_t /*Offset*/, float2 *RESTRICT Values, const ALsizei IrSize,
    const HrirArray &Coeffs, const ALfloat left, const ALfloat right)
{
    ASSUME(IrSize >= 2);

    float32x4_t leftright4;
    {
        float32x2_t leftright2 = vdup_n_f32(0.0);
        leftright2 = vset_lane_f32(left, leftright2, 0);
        leftright2 = vset_lane_f32(right, leftright2, 1);
        leftright4 = vcombine_f32(leftright2, leftright2);
    }

    for(ALsizei c{0};c < IrSize;c += 2)
    {
        float32x4_t vals = vld1q_f32((float32_t*)&Values[c][0]);
        float32x4_t coefs = vld1q_f32((float32_t*)&Coeffs[c][0]);

        vals = vmlaq_f32(vals, coefs, leftright4);

        vst1q_f32((float32_t*)&Values[c][0], vals);
    }
}

template<>
void MixHrtf_<NEONTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const size_t OutPos, const ALsizei IrSize,
    MixHrtfFilter *hrtfparams, const size_t BufferSize)
{
    MixHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        hrtfparams, BufferSize);
}

template<>
void MixHrtfBlend_<NEONTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const size_t OutPos, const ALsizei IrSize,
    const HrtfFilter *oldparams, MixHrtfFilter *newparams, const size_t BufferSize)
{
    MixHrtfBlendBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        oldparams, newparams, BufferSize);
}

template<>
void MixDirectHrtf_<NEONTag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State,
    const size_t BufferSize)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, State, BufferSize);
}


template<>
void Mix_<NEONTag>(const al::span<const float> InSamples, const al::span<FloatBufferLine> OutBuffer,
    float *CurrentGains, const float *TargetGains, const size_t Counter, const size_t OutPos)
{
    const ALfloat delta{(Counter > 0) ? 1.0f / static_cast<ALfloat>(Counter) : 0.0f};
    const bool reached_target{InSamples.size() >= Counter};
    const auto min_end = reached_target ? InSamples.begin() + Counter : InSamples.end();
    const auto aligned_end = minz(InSamples.size(), (min_end-InSamples.begin()+3) & ~3) +
        InSamples.begin();
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
            /* Mix with applying gain steps in aligned multiples of 4. */
            if(ptrdiff_t todo{(min_end-in_iter) >> 2})
            {
                const float32x4_t four4{vdupq_n_f32(4.0f)};
                const float32x4_t step4{vdupq_n_f32(step)};
                const float32x4_t gain4{vdupq_n_f32(gain)};
                float32x4_t step_count4{vsetq_lane_f32(0.0f,
                    vsetq_lane_f32(1.0f,
                    vsetq_lane_f32(2.0f,
                    vsetq_lane_f32(3.0f, vdupq_n_f32(0.0f), 3),
                    2), 1), 0
                )};
                do {
                    const float32x4_t val4 = vld1q_f32(in_iter);
                    float32x4_t dry4 = vld1q_f32(dst);
                    dry4 = vmlaq_f32(dry4, val4, vmlaq_f32(gain4, step4, step_count4));
                    step_count4 = vaddq_f32(step_count4, four4);
                    vst1q_f32(dst, dry4);
                    in_iter += 4; dst += 4;
                } while(--todo);
                /* NOTE: step_count4 now represents the next four counts after
                 * the last four mixed samples, so the lowest element
                 * represents the next step count to apply.
                 */
                step_count = vgetq_lane_f32(step_count4, 0);
            }
            /* Mix with applying left over gain steps that aren't aligned multiples of 4. */
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

            /* Mix until pos is aligned with 4 or the mix is done. */
            while(in_iter != aligned_end)
                *(dst++) += *(in_iter++) * gain;
        }
        ++CurrentGains;
        ++TargetGains;

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;
        if(ptrdiff_t todo{(InSamples.end()-in_iter) >> 2})
        {
            const float32x4_t gain4 = vdupq_n_f32(gain);
            do {
                const float32x4_t val4 = vld1q_f32(in_iter);
                float32x4_t dry4 = vld1q_f32(dst);
                dry4 = vmlaq_f32(dry4, val4, gain4);
                vst1q_f32(dst, dry4);
                in_iter += 4; dst += 4;
            } while(--todo);
        }
        while(in_iter != InSamples.end())
            *(dst++) += *(in_iter++) * gain;
    }
}

template<>
void MixRow_<NEONTag>(const al::span<float> OutBuffer, const al::span<const float> Gains,
    const float *InSamples, const size_t InStride)
{
    for(const ALfloat gain : Gains)
    {
        const ALfloat *RESTRICT src{InSamples};
        InSamples += InStride;

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        auto out_iter = OutBuffer.begin();
        if(size_t todo{OutBuffer.size() >> 2})
        {
            const float32x4_t gain4{vdupq_n_f32(gain)};
            do {
                const float32x4_t val4 = vld1q_f32(src);
                float32x4_t dry4 = vld1q_f32(out_iter);
                dry4 = vmlaq_f32(dry4, val4, gain4);
                vst1q_f32(out_iter, dry4);
                out_iter += 4; src += 4;
            } while(--todo);
        }
        std::transform(out_iter, OutBuffer.end(), src, out_iter,
            [gain](const ALfloat cur, const ALfloat src) -> ALfloat { return cur + src*gain; });
    }
}
