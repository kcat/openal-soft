#include "config.h"

#include <xmmintrin.h>

#include <limits>

#include "AL/al.h"
#include "AL/alc.h"
#include "alcmain.h"

#include "alu.h"
#include "defs.h"
#include "hrtfbase.h"


namespace {

inline void ApplyCoeffs(float2 *RESTRICT Values, const ALuint IrSize, const HrirArray &Coeffs,
    const float left, const float right)
{
    const __m128 lrlr{_mm_setr_ps(left, right, left, right)};

    ASSUME(IrSize >= MIN_IR_LENGTH);
    /* This isn't technically correct to test alignment, but it's true for
     * systems that support SSE, which is the only one that needs to know the
     * alignment of Values (which alternates between 8- and 16-byte aligned).
     */
    if(reinterpret_cast<intptr_t>(Values)&0x8)
    {
        __m128 imp0, imp1;
        __m128 coeffs{_mm_load_ps(&Coeffs[0][0])};
        __m128 vals{_mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<__m64*>(&Values[0][0]))};
        imp0 = _mm_mul_ps(lrlr, coeffs);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi(reinterpret_cast<__m64*>(&Values[0][0]), vals);
        ALuint td{(IrSize>>1) - 1};
        size_t i{1};
        do {
            coeffs = _mm_load_ps(&Coeffs[i+1][0]);
            vals = _mm_load_ps(&Values[i][0]);
            imp1 = _mm_mul_ps(lrlr, coeffs);
            imp0 = _mm_shuffle_ps(imp0, imp1, _MM_SHUFFLE(1, 0, 3, 2));
            vals = _mm_add_ps(imp0, vals);
            _mm_store_ps(&Values[i][0], vals);
            imp0 = imp1;
            i += 2;
        } while(--td);
        vals = _mm_loadl_pi(vals, reinterpret_cast<__m64*>(&Values[i][0]));
        imp0 = _mm_movehl_ps(imp0, imp0);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi(reinterpret_cast<__m64*>(&Values[i][0]), vals);
    }
    else
    {
        for(size_t i{0};i < IrSize;i += 2)
        {
            const __m128 coeffs{_mm_load_ps(&Coeffs[i][0])};
            __m128 vals{_mm_load_ps(&Values[i][0])};
            vals = _mm_add_ps(vals, _mm_mul_ps(lrlr, coeffs));
            _mm_store_ps(&Values[i][0], vals);
        }
    }
}

} // namespace

template<>
const ALfloat *Resample_<BSincTag,SSETag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALuint frac, ALuint increment, const al::span<float> dst)
{
    const float *const filter{state->bsinc.filter};
    const __m128 sf4{_mm_set1_ps(state->bsinc.sf)};
    const size_t m{state->bsinc.m};

    src -= state->bsinc.l;
    for(float &out_sample : dst)
    {
        // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
        const ALuint pi{frac >> FRAC_PHASE_BITDIFF};
        const float pf{static_cast<float>(frac & ((1<<FRAC_PHASE_BITDIFF)-1)) *
            (1.0f/(1<<FRAC_PHASE_BITDIFF))};
#undef FRAC_PHASE_BITDIFF

        // Apply the scale and phase interpolated filter.
        __m128 r4{_mm_setzero_ps()};
        {
            const __m128 pf4{_mm_set1_ps(pf)};
            const float *fil{filter + m*pi*4};
            const float *phd{fil + m};
            const float *scd{phd + m};
            const float *spd{scd + m};
            size_t td{m >> 2};
            size_t j{0u};

#define MLA4(x, y, z) _mm_add_ps(x, _mm_mul_ps(y, z))
            do {
                /* f = ((fil + sf*scd) + pf*(phd + sf*spd)) */
                const __m128 f4 = MLA4(
                    MLA4(_mm_load_ps(fil), sf4, _mm_load_ps(scd)),
                    pf4, MLA4(_mm_load_ps(phd), sf4, _mm_load_ps(spd)));
                fil += 4; scd += 4; phd += 4; spd += 4;
                /* r += f*src */
                r4 = MLA4(r4, f4, _mm_loadu_ps(&src[j]));
                j += 4;
            } while(--td);
#undef MLA4
        }
        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
        out_sample = _mm_cvtss_f32(r4);

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
    return dst.begin();
}

template<>
const ALfloat *Resample_<FastBSincTag,SSETag>(const InterpState *state,
    const ALfloat *RESTRICT src, ALuint frac, ALuint increment, const al::span<float> dst)
{
    const float *const filter{state->bsinc.filter};
    const size_t m{state->bsinc.m};

    src -= state->bsinc.l;
    for(float &out_sample : dst)
    {
        // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
        const ALuint pi{frac >> FRAC_PHASE_BITDIFF};
        const float pf{static_cast<float>(frac & ((1<<FRAC_PHASE_BITDIFF)-1)) *
            (1.0f/(1<<FRAC_PHASE_BITDIFF))};
#undef FRAC_PHASE_BITDIFF

        // Apply the phase interpolated filter.
        __m128 r4{_mm_setzero_ps()};
        {
            const __m128 pf4{_mm_set1_ps(pf)};
            const float *fil{filter + m*pi*4};
            const float *phd{fil + m};
            size_t td{m >> 2};
            size_t j{0u};

#define MLA4(x, y, z) _mm_add_ps(x, _mm_mul_ps(y, z))
            do {
                /* f = fil + pf*phd */
                const __m128 f4 = MLA4(_mm_load_ps(fil), pf4, _mm_load_ps(phd));
                /* r += f*src */
                r4 = MLA4(r4, f4, _mm_loadu_ps(&src[j]));
                fil += 4; phd += 4; j += 4;
            } while(--td);
#undef MLA4
        }
        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
        out_sample = _mm_cvtss_f32(r4);

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
    return dst.begin();
}


template<>
void MixHrtf_<SSETag>(const float *InSamples, float2 *AccumSamples, const ALuint IrSize,
    const MixHrtfFilter *hrtfparams, const size_t BufferSize)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, BufferSize); }

template<>
void MixHrtfBlend_<SSETag>(const float *InSamples, float2 *AccumSamples, const ALuint IrSize,
    const HrtfFilter *oldparams, const MixHrtfFilter *newparams, const size_t BufferSize)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        BufferSize);
}

template<>
void MixDirectHrtf_<SSETag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State,
    const size_t BufferSize)
{ MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, State, BufferSize); }


template<>
void Mix_<SSETag>(const al::span<const float> InSamples, const al::span<FloatBufferLine> OutBuffer,
    float *CurrentGains, const float *TargetGains, const size_t Counter, const size_t OutPos)
{
    const ALfloat delta{(Counter > 0) ? 1.0f / static_cast<ALfloat>(Counter) : 0.0f};
    const bool reached_target{InSamples.size() >= Counter};
    const auto min_end = reached_target ? InSamples.begin() + Counter : InSamples.end();
    const auto aligned_end = minz(static_cast<uintptr_t>(min_end-InSamples.begin()+3) & ~3u,
        InSamples.size()) + InSamples.begin();
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
                const __m128 four4{_mm_set1_ps(4.0f)};
                const __m128 step4{_mm_set1_ps(step)};
                const __m128 gain4{_mm_set1_ps(gain)};
                __m128 step_count4{_mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f)};
                do {
                    const __m128 val4{_mm_load_ps(in_iter)};
                    __m128 dry4{_mm_load_ps(dst)};
#define MLA4(x, y, z) _mm_add_ps(x, _mm_mul_ps(y, z))
                    /* dry += val * (gain + step*step_count) */
                    dry4 = MLA4(dry4, val4, MLA4(gain4, step4, step_count4));
#undef MLA4
                    _mm_store_ps(dst, dry4);
                    step_count4 = _mm_add_ps(step_count4, four4);
                    in_iter += 4; dst += 4;
                } while(--todo);
                /* NOTE: step_count4 now represents the next four counts after
                 * the last four mixed samples, so the lowest element
                 * represents the next step count to apply.
                 */
                step_count = _mm_cvtss_f32(step_count4);
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
            const __m128 gain4{_mm_set1_ps(gain)};
            do {
                const __m128 val4{_mm_load_ps(in_iter)};
                __m128 dry4{_mm_load_ps(dst)};
                dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
                _mm_store_ps(dst, dry4);
                in_iter += 4; dst += 4;
            } while(--todo);
        }
        while(in_iter != InSamples.end())
            *(dst++) += *(in_iter++) * gain;
    }
}

template<>
void MixRow_<SSETag>(const al::span<float> OutBuffer, const al::span<const float> Gains,
    const float *InSamples, const size_t InStride)
{
    for(const float gain : Gains)
    {
        const float *RESTRICT input{InSamples};
        InSamples += InStride;

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        auto out_iter = OutBuffer.begin();
        if(size_t todo{OutBuffer.size() >> 2})
        {
            const __m128 gain4 = _mm_set1_ps(gain);
            do {
                const __m128 val4{_mm_load_ps(input)};
                __m128 dry4{_mm_load_ps(out_iter)};
                dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
                _mm_store_ps(out_iter, dry4);
                out_iter += 4; input += 4;
            } while(--todo);
        }

        auto do_mix = [gain](const float cur, const float src) noexcept -> float
        { return cur + src*gain; };
        std::transform(out_iter, OutBuffer.end(), input, out_iter, do_mix);
    }
}
