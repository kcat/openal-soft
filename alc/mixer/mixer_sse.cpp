#include "config.h"

#include <xmmintrin.h>

#include <limits>

#include "AL/al.h"
#include "AL/alc.h"
#include "alcmain.h"
#include "alu.h"

#include "defs.h"
#include "hrtfbase.h"


template<>
const ALfloat *Resample_<BSincTag,SSETag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALsizei frac, ALint increment, const al::span<float> dst)
{
    const ALfloat *const filter{state->bsinc.filter};
    const __m128 sf4{_mm_set1_ps(state->bsinc.sf)};
    const ALsizei m{state->bsinc.m};

    ASSUME(m > 0);
    ASSUME(increment > 0);
    ASSUME(frac >= 0);

    src -= state->bsinc.l;
    for(float &out_sample : dst)
    {
        // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
        const ALsizei pi{frac >> FRAC_PHASE_BITDIFF};
        const ALfloat pf{(frac & ((1<<FRAC_PHASE_BITDIFF)-1)) * (1.0f/(1<<FRAC_PHASE_BITDIFF))};
#undef FRAC_PHASE_BITDIFF

        ALsizei offset{m*pi*4};
        const __m128 *fil{reinterpret_cast<const __m128*>(filter + offset)}; offset += m;
        const __m128 *scd{reinterpret_cast<const __m128*>(filter + offset)}; offset += m;
        const __m128 *phd{reinterpret_cast<const __m128*>(filter + offset)}; offset += m;
        const __m128 *spd{reinterpret_cast<const __m128*>(filter + offset)};

        // Apply the scale and phase interpolated filter.
        __m128 r4{_mm_setzero_ps()};
        {
            const ALsizei count{m >> 2};
            const __m128 pf4{_mm_set1_ps(pf)};

            ASSUME(count > 0);

#define MLA4(x, y, z) _mm_add_ps(x, _mm_mul_ps(y, z))
            for(ALsizei j{0};j < count;j++)
            {
                /* f = ((fil + sf*scd) + pf*(phd + sf*spd)) */
                const __m128 f4 = MLA4(
                    MLA4(fil[j], sf4, scd[j]),
                    pf4, MLA4(phd[j], sf4, spd[j])
                );
                /* r += f*src */
                r4 = MLA4(r4, f4, _mm_loadu_ps(&src[j*4]));
            }
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


static inline void ApplyCoeffs(size_t Offset, float2 *RESTRICT Values, const ALsizei IrSize,
    const HrirArray &Coeffs, const ALfloat left, const ALfloat right)
{
    const __m128 lrlr{_mm_setr_ps(left, right, left, right)};

    ASSUME(IrSize >= 2);

    if((Offset&1))
    {
        __m128 imp0, imp1;
        __m128 coeffs{_mm_load_ps(&Coeffs[0][0])};
        __m128 vals{_mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<__m64*>(&Values[0][0]))};
        imp0 = _mm_mul_ps(lrlr, coeffs);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi(reinterpret_cast<__m64*>(&Values[0][0]), vals);
        ALsizei i{1};
        for(;i < IrSize-1;i += 2)
        {
            coeffs = _mm_load_ps(&Coeffs[i+1][0]);
            vals = _mm_load_ps(&Values[i][0]);
            imp1 = _mm_mul_ps(lrlr, coeffs);
            imp0 = _mm_shuffle_ps(imp0, imp1, _MM_SHUFFLE(1, 0, 3, 2));
            vals = _mm_add_ps(imp0, vals);
            _mm_store_ps(&Values[i][0], vals);
            imp0 = imp1;
        }
        vals = _mm_loadl_pi(vals, reinterpret_cast<__m64*>(&Values[i][0]));
        imp0 = _mm_movehl_ps(imp0, imp0);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi(reinterpret_cast<__m64*>(&Values[i][0]), vals);
    }
    else
    {
        for(ALsizei i{0};i < IrSize;i += 2)
        {
            __m128 coeffs{_mm_load_ps(&Coeffs[i][0])};
            __m128 vals{_mm_load_ps(&Values[i][0])};
            vals = _mm_add_ps(vals, _mm_mul_ps(lrlr, coeffs));
            _mm_store_ps(&Values[i][0], vals);
        }
    }
}

template<>
void MixHrtf_<SSETag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const size_t OutPos, const ALsizei IrSize,
    MixHrtfFilter *hrtfparams, const size_t BufferSize)
{
    MixHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        hrtfparams, BufferSize);
}

template<>
void MixHrtfBlend_<SSETag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const size_t OutPos, const ALsizei IrSize,
    const HrtfFilter *oldparams, MixHrtfFilter *newparams, const size_t BufferSize)
{
    MixHrtfBlendBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        oldparams, newparams, BufferSize);
}

template<>
void MixDirectHrtf_<SSETag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State,
    const size_t BufferSize)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, State, BufferSize);
}


template<>
void Mix_<SSETag>(const al::span<const float> InSamples, const al::span<FloatBufferLine> OutBuffer,
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
        const float *RESTRICT src{InSamples};
        InSamples += InStride;

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        auto out_iter = OutBuffer.begin();
        if(size_t todo{OutBuffer.size() >> 2})
        {
            const __m128 gain4 = _mm_set1_ps(gain);
            do {
                const __m128 val4{_mm_load_ps(src)};
                __m128 dry4{_mm_load_ps(out_iter)};
                dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
                _mm_store_ps(out_iter, dry4);
                out_iter += 4; src += 4;
            } while(--todo);
        }
        std::transform(out_iter, OutBuffer.end(), src, out_iter,
            [gain](const ALfloat cur, const ALfloat src) -> ALfloat { return cur + src*gain; });
    }
}
