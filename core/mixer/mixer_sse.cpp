#include "config.h"

#include <mmintrin.h>
#include <xmmintrin.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

struct SSETag;
struct CubicTag;
struct BSincTag;
struct FastBSincTag;


#if defined(__GNUC__) && !defined(__clang__) && !defined(__SSE__)
#pragma GCC target("sse")
#endif

namespace {

constexpr uint BSincPhaseDiffBits{MixerFracBits - BSincPhaseBits};
constexpr uint BSincPhaseDiffOne{1 << BSincPhaseDiffBits};
constexpr uint BSincPhaseDiffMask{BSincPhaseDiffOne - 1u};

constexpr uint CubicPhaseDiffBits{MixerFracBits - CubicPhaseBits};
constexpr uint CubicPhaseDiffOne{1 << CubicPhaseDiffBits};
constexpr uint CubicPhaseDiffMask{CubicPhaseDiffOne - 1u};

force_inline __m128 vmadd(const __m128 x, const __m128 y, const __m128 z) noexcept
{ return _mm_add_ps(x, _mm_mul_ps(y, z)); }

inline void ApplyCoeffs(const al::span<float2> Values, const size_t IrSize,
    const ConstHrirSpan Coeffs, const float left, const float right)
{
    ASSUME(IrSize >= MinIrLength);
    ASSUME(IrSize <= HrirLength);
    const auto lrlr = _mm_setr_ps(left, right, left, right);

    /* This isn't technically correct to test alignment, but it's true for
     * systems that support SSE, which is the only one that needs to know the
     * alignment of Values (which alternates between 8- and 16-byte aligned).
     */
    if(!(reinterpret_cast<uintptr_t>(Values.data())&15))
    {
        const auto vals4 = al::span{reinterpret_cast<__m128*>(Values[0].data()), IrSize/2};
        const auto coeffs4 = al::span{reinterpret_cast<const __m128*>(Coeffs[0].data()), IrSize/2};
        std::transform(vals4.cbegin(), vals4.cend(), coeffs4.cbegin(), vals4.begin(),
            [lrlr](const __m128 &val, const __m128 &coeff) -> __m128
            { return vmadd(val, coeff, lrlr); });
    }
    else
    {
        auto coeffs = _mm_load_ps(Coeffs[0].data());
        auto vals = _mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<__m64*>(Values[0].data()));
        auto imp0 = _mm_mul_ps(lrlr, coeffs);
        vals = _mm_add_ps(imp0, vals);
        _mm_storel_pi(reinterpret_cast<__m64*>(Values[0].data()), vals);
        size_t td{((IrSize+1)>>1) - 1};
        size_t i{1};
        do {
            coeffs = _mm_load_ps(Coeffs[i+1].data());
            vals = _mm_load_ps(Values[i].data());
            const auto imp1 = _mm_mul_ps(lrlr, coeffs);
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
}

force_inline void MixLine(const al::span<const float> InSamples, const al::span<float> dst,
    float &CurrentGain, const float TargetGain, const float delta, const size_t min_len,
    const size_t aligned_len, size_t Counter)
{
    const float step{(TargetGain-CurrentGain) * delta};

    size_t pos{0};
    if(std::abs(step) > std::numeric_limits<float>::epsilon())
    {
        const float gain{CurrentGain};
        float step_count{0.0f};
        /* Mix with applying gain steps in aligned multiples of 4. */
        if(size_t todo{min_len >> 2})
        {
            const __m128 four4{_mm_set1_ps(4.0f)};
            const __m128 step4{_mm_set1_ps(step)};
            const __m128 gain4{_mm_set1_ps(gain)};
            __m128 step_count4{_mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f)};
            do {
                const __m128 val4{_mm_load_ps(&InSamples[pos])};
                __m128 dry4{_mm_load_ps(&dst[pos])};

                /* dry += val * (gain + step*step_count) */
                dry4 = vmadd(dry4, val4, vmadd(gain4, step4, step_count4));

                _mm_store_ps(&dst[pos], dry4);
                step_count4 = _mm_add_ps(step_count4, four4);
                pos += 4;
            } while(--todo);
            /* NOTE: step_count4 now represents the next four counts after the
             * last four mixed samples, so the lowest element represents the
             * next step count to apply.
             */
            step_count = _mm_cvtss_f32(step_count4);
        }
        /* Mix with applying left over gain steps that aren't aligned multiples of 4. */
        for(size_t leftover{min_len&3};leftover;++pos,--leftover)
        {
            dst[pos] += InSamples[pos] * (gain + step*step_count);
            step_count += 1.0f;
        }
        if(min_len < Counter)
        {
            CurrentGain = gain + step*step_count;
            return;
        }

        /* Mix until pos is aligned with 4 or the mix is done. */
        for(size_t leftover{aligned_len&3};leftover;++pos,--leftover)
            dst[pos] += InSamples[pos] * TargetGain;
    }
    CurrentGain = TargetGain;

    if(!(std::abs(TargetGain) > GainSilenceThreshold))
        return;
    if(size_t todo{(InSamples.size()-pos) >> 2})
    {
        const __m128 gain4{_mm_set1_ps(TargetGain)};
        do {
            const __m128 val4{_mm_load_ps(&InSamples[pos])};
            __m128 dry4{_mm_load_ps(&dst[pos])};
            dry4 = _mm_add_ps(dry4, _mm_mul_ps(val4, gain4));
            _mm_store_ps(&dst[pos], dry4);
            pos += 4;
        } while(--todo);
    }
    for(size_t leftover{InSamples.size()&3};leftover;++pos,--leftover)
        dst[pos] += InSamples[pos] * TargetGain;
}

} // namespace

template<>
void Resample_<CubicTag,SSETag>(const InterpState *state, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{
    ASSUME(frac < MixerFracOne);

    const auto filter = std::get<CubicState>(*state).filter;

    src -= 1;
    std::generate(dst.begin(), dst.end(), [&src,&frac,increment,filter]() -> float
    {
        const uint pi{frac >> CubicPhaseDiffBits}; ASSUME(pi < CubicPhaseCount);
        const float pf{static_cast<float>(frac&CubicPhaseDiffMask) * (1.0f/CubicPhaseDiffOne)};
        const __m128 pf4{_mm_set1_ps(pf)};

        /* Apply the phase interpolated filter. */

        /* f = fil + pf*phd */
        const __m128 f4 = vmadd(_mm_load_ps(filter[pi].mCoeffs.data()), pf4,
            _mm_load_ps(filter[pi].mDeltas.data()));
        /* r = f*src */
        __m128 r4{_mm_mul_ps(f4, _mm_loadu_ps(src))};

        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
        const float output{_mm_cvtss_f32(r4)};

        frac += increment;
        src  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

template<>
void Resample_<BSincTag,SSETag>(const InterpState *state, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{
    const auto &bsinc = std::get<BsincState>(*state);
    const float *const filter{bsinc.filter.data()};
    const __m128 sf4{_mm_set1_ps(bsinc.sf)};
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
        __m128 r4{_mm_setzero_ps()};
        {
            const __m128 pf4{_mm_set1_ps(pf)};
            const float *fil{filter + m*pi*2_uz};
            const float *phd{fil + m};
            const float *scd{fil + BSincPhaseCount*2_uz*m};
            const float *spd{scd + m};
            size_t td{m >> 2};
            size_t j{0u};

            do {
                /* f = ((fil + sf*scd) + pf*(phd + sf*spd)) */
                const __m128 f4 = vmadd(
                    vmadd(_mm_load_ps(&fil[j]), sf4, _mm_load_ps(&scd[j])),
                    pf4, vmadd(_mm_load_ps(&phd[j]), sf4, _mm_load_ps(&spd[j])));
                /* r += f*src */
                r4 = vmadd(r4, f4, _mm_loadu_ps(&src[j]));
                j += 4;
            } while(--td);
        }
        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
        const float output{_mm_cvtss_f32(r4)};

        frac += increment;
        src  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}

template<>
void Resample_<FastBSincTag,SSETag>(const InterpState *state, const float *src, uint frac,
    const uint increment, const al::span<float> dst)
{
    const auto &bsinc = std::get<BsincState>(*state);
    const float *const filter{bsinc.filter.data()};
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
        __m128 r4{_mm_setzero_ps()};
        {
            const __m128 pf4{_mm_set1_ps(pf)};
            const float *fil{filter + m*pi*2_uz};
            const float *phd{fil + m};
            size_t td{m >> 2};
            size_t j{0u};

            do {
                /* f = fil + pf*phd */
                const __m128 f4 = vmadd(_mm_load_ps(&fil[j]), pf4, _mm_load_ps(&phd[j]));
                /* r += f*src */
                r4 = vmadd(r4, f4, _mm_loadu_ps(&src[j]));
                j += 4;
            } while(--td);
        }
        r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
        r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
        const float output{_mm_cvtss_f32(r4)};

        frac += increment;
        src  += frac>>MixerFracBits;
        frac &= MixerFracMask;
        return output;
    });
}


template<>
void MixHrtf_<SSETag>(const al::span<const float> InSamples, const al::span<float2> AccumSamples,
    const uint IrSize, const MixHrtfFilter *hrtfparams, const size_t SamplesToDo)
{ MixHrtfBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, hrtfparams, SamplesToDo); }

template<>
void MixHrtfBlend_<SSETag>(const al::span<const float> InSamples,
    const al::span<float2> AccumSamples, const uint IrSize, const HrtfFilter *oldparams,
    const MixHrtfFilter *newparams, const size_t SamplesToDo)
{
    MixHrtfBlendBase<ApplyCoeffs>(InSamples, AccumSamples, IrSize, oldparams, newparams,
        SamplesToDo);
}

template<>
void MixDirectHrtf_<SSETag>(const FloatBufferSpan LeftOut, const FloatBufferSpan RightOut,
    const al::span<const FloatBufferLine> InSamples, const al::span<float2> AccumSamples,
    const al::span<float,BufferLineSize> TempBuf, const al::span<HrtfChannelState> ChanState,
    const size_t IrSize, const size_t SamplesToDo)
{
    MixDirectHrtfBase<ApplyCoeffs>(LeftOut, RightOut, InSamples, AccumSamples, TempBuf, ChanState,
        IrSize, SamplesToDo);
}


template<>
void Mix_<SSETag>(const al::span<const float> InSamples, const al::span<FloatBufferLine> OutBuffer,
    const al::span<float> CurrentGains, const al::span<const float> TargetGains,
    const size_t Counter, const size_t OutPos)
{
    const float delta{(Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f};
    const auto min_len = std::min(Counter, InSamples.size());
    const auto aligned_len = std::min((min_len+3_uz) & ~3_uz, InSamples.size()) - min_len;

    auto curgains = CurrentGains.begin();
    auto targetgains = TargetGains.cbegin();
    for(FloatBufferLine &output : OutBuffer)
        MixLine(InSamples, al::span{output}.subspan(OutPos), *curgains++, *targetgains++, delta,
            min_len, aligned_len, Counter);
}

template<>
void Mix_<SSETag>(const al::span<const float> InSamples, const al::span<float> OutBuffer,
    float &CurrentGain, const float TargetGain, const size_t Counter)
{
    const float delta{(Counter > 0) ? 1.0f / static_cast<float>(Counter) : 0.0f};
    const auto min_len = std::min(Counter, InSamples.size());
    const auto aligned_len = std::min((min_len+3_uz) & ~3_uz, InSamples.size()) - min_len;

    MixLine(InSamples, OutBuffer, CurrentGain, TargetGain, delta, min_len, aligned_len, Counter);
}
