#include "config.h"

#include <xmmintrin.h>

#include <limits>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"

#include "alSource.h"
#include "alAuxEffectSlot.h"
#include "defs.h"
#include "hrtfbase.h"


template<>
const ALfloat *Resample_<BSincTag,SSETag>(const InterpState *state, const ALfloat *RESTRICT src,
    ALsizei frac, ALint increment, ALfloat *RESTRICT dst, ALsizei dstlen)
{
    const ALfloat *const filter{state->bsinc.filter};
    const __m128 sf4{_mm_set1_ps(state->bsinc.sf)};
    const ALsizei m{state->bsinc.m};

    ASSUME(m > 0);
    ASSUME(dstlen > 0);
    ASSUME(increment > 0);
    ASSUME(frac >= 0);

    src -= state->bsinc.l;
    for(ALsizei i{0};i < dstlen;i++)
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
        dst[i] = _mm_cvtss_f32(r4);

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
    return dst;
}


static inline void ApplyCoeffs(ALsizei Offset, float2 *RESTRICT Values, const ALsizei IrSize,
    const HrirArray<ALfloat> &Coeffs, const ALfloat left, const ALfloat right)
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
    const ALfloat *InSamples, float2 *AccumSamples, const ALsizei OutPos, const ALsizei IrSize,
    MixHrtfFilter *hrtfparams, const ALsizei BufferSize)
{
    MixHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        hrtfparams, BufferSize);
}

template<>
void MixHrtfBlend_<SSETag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const ALsizei OutPos, const ALsizei IrSize,
    const HrtfFilter *oldparams, MixHrtfFilter *newparams, const ALsizei BufferSize)
{
    MixHrtfBlendBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, OutPos, IrSize,
        oldparams, newparams, BufferSize);
}

template<>
void MixDirectHrtf_<SSETag>(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State,
    const ALsizei BufferSize)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, State, BufferSize);
}


template<>
void Mix_<SSETag>(const ALfloat *data, const al::span<FloatBufferLine> OutBuffer,
    ALfloat *CurrentGains, const ALfloat *TargetGains, const ALsizei Counter, const ALsizei OutPos,
    const ALsizei BufferSize)
{
    ASSUME(BufferSize > 0);

    const ALfloat delta{(Counter > 0) ? 1.0f / static_cast<ALfloat>(Counter) : 0.0f};
    for(FloatBufferLine &output : OutBuffer)
    {
        ALfloat *RESTRICT dst{al::assume_aligned<16>(output.data()+OutPos)};
        ALfloat gain{*CurrentGains};
        const ALfloat diff{*TargetGains - gain};

        ALsizei pos{0};
        if(std::fabs(diff) > std::numeric_limits<float>::epsilon())
        {
            ALsizei minsize{mini(BufferSize, Counter)};
            const ALfloat step{diff * delta};
            ALfloat step_count{0.0f};
            /* Mix with applying gain steps in aligned multiples of 4. */
            if(LIKELY(minsize > 3))
            {
                const __m128 four4{_mm_set1_ps(4.0f)};
                const __m128 step4{_mm_set1_ps(step)};
                const __m128 gain4{_mm_set1_ps(gain)};
                __m128 step_count4{_mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f)};
                ALsizei todo{minsize >> 2};
                do {
                    const __m128 val4{_mm_load_ps(&data[pos])};
                    __m128 dry4{_mm_load_ps(&dst[pos])};
#define MLA4(x, y, z) _mm_add_ps(x, _mm_mul_ps(y, z))
                    /* dry += val * (gain + step*step_count) */
                    dry4 = MLA4(dry4, val4, MLA4(gain4, step4, step_count4));
#undef MLA4
                    _mm_store_ps(&dst[pos], dry4);
                    step_count4 = _mm_add_ps(step_count4, four4);
                    pos += 4;
                } while(--todo);
                /* NOTE: step_count4 now represents the next four counts after
                 * the last four mixed samples, so the lowest element
                 * represents the next step count to apply.
                 */
                step_count = _mm_cvtss_f32(step_count4);
            }
            /* Mix with applying left over gain steps that aren't aligned multiples of 4. */
            for(;pos < minsize;pos++)
            {
                dst[pos] += data[pos]*(gain + step*step_count);
                step_count += 1.0f;
            }
            if(pos == Counter)
                gain = *TargetGains;
            else
                gain += step*step_count;
            *CurrentGains = gain;

            /* Mix until pos is aligned with 4 or the mix is done. */
            minsize = mini(BufferSize, (pos+3)&~3);
            for(;pos < minsize;pos++)
                dst[pos] += data[pos]*gain;
        }
        ++CurrentGains;
        ++TargetGains;

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;
        if(LIKELY(BufferSize-pos > 3))
        {
            ALsizei todo{(BufferSize-pos) >> 2};
            const __m128 gain4{_mm_set1_ps(gain)};
            do {
                const __m128 val4{_mm_load_ps(&data[pos])};
                __m128 dry4{_mm_load_ps(&dst[pos])};
                dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
                _mm_store_ps(&dst[pos], dry4);
                pos += 4;
            } while(--todo);
        }
        for(;pos < BufferSize;pos++)
            dst[pos] += data[pos]*gain;
    }
}

template<>
void MixRow_<SSETag>(FloatBufferLine &OutBuffer, const ALfloat *Gains,
    const al::span<const FloatBufferLine> InSamples, const ALsizei InPos, const ALsizei BufferSize)
{
    ASSUME(BufferSize > 0);

    for(const FloatBufferLine &input : InSamples)
    {
        const ALfloat *RESTRICT src{al::assume_aligned<16>(input.data()+InPos)};
        const ALfloat gain{*(Gains++)};
        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        ALsizei pos{0};
        if(LIKELY(BufferSize > 3))
        {
            ALsizei todo{BufferSize >> 2};
            const __m128 gain4 = _mm_set1_ps(gain);
            do {
                const __m128 val4{_mm_load_ps(&src[pos])};
                __m128 dry4{_mm_load_ps(&OutBuffer[pos])};
                dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
                _mm_store_ps(&OutBuffer[pos], dry4);
                pos += 4;
            } while(--todo);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[pos] += src[pos]*gain;
    }
}
