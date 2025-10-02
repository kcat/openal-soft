/**
 * OpenAL cross platform audio library
 * Copyright (C) 2014 by Timothy Arceri <t_arceri@yahoo.com.au>.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <xmmintrin.h>
#include <emmintrin.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <variant>

#include "alnumeric.h"
#include "core/cubic_defs.h"
#include "core/resampler_limits.h"
#include "defs.h"
#include "gsl/gsl"
#include "opthelpers.h"

struct SSE2Tag;
struct LerpTag;
struct CubicTag;


#if defined(__GNUC__) && !defined(__clang__) && !defined(__SSE2__)
#pragma GCC target("sse2")
#endif

using uint = unsigned int;

namespace {

constexpr auto CubicPhaseDiffBits = uint{MixerFracBits - CubicPhaseBits};
constexpr auto CubicPhaseDiffOne = 1u << CubicPhaseDiffBits;
constexpr auto CubicPhaseDiffMask = CubicPhaseDiffOne - 1u;

force_inline auto vmadd(const __m128 x, const __m128 y, const __m128 z) noexcept -> __m128
{ return _mm_add_ps(x, _mm_mul_ps(y, z)); }

} // namespace

template<>
void Resample_<LerpTag,SSE2Tag>(const InterpState*, const std::span<const float> src, uint frac,
    const uint increment, const std::span<float> dst)
{
    ASSUME(frac < MixerFracOne);

    const auto increment4 = _mm_set1_epi32(as_signed(increment*4));
    const auto fracMask4 = _mm_set1_epi32(MixerFracMask);
    const auto fracOne4 = _mm_set1_ps(1.0f/MixerFracOne);

    auto pos_ = std::array<uint,4>{};
    auto frac_ = std::array<uint,4>{};
    InitPosArrays(MaxResamplerEdge, frac, increment, std::span{frac_}, std::span{pos_});
    auto pos4 = _mm_setr_epi32(as_signed(pos_[0]), as_signed(pos_[1]), as_signed(pos_[2]),
        as_signed(pos_[3]));
    auto frac4 = _mm_setr_epi32(as_signed(frac_[0]), as_signed(frac_[1]), as_signed(frac_[2]),
        as_signed(frac_[3]));

    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    std::ranges::generate(std::span{reinterpret_cast<__m128*>(dst.data()), dst.size()/4},
        [src,increment4,fracMask4,fracOne4,&pos4,&frac4]
    {
        const auto pos0 = as_unsigned(_mm_cvtsi128_si32(pos4));
        const auto pos1 = as_unsigned(_mm_cvtsi128_si32(_mm_srli_si128(pos4, 4)));
        const auto pos2 = as_unsigned(_mm_cvtsi128_si32(_mm_srli_si128(pos4, 8)));
        const auto pos3 = as_unsigned(_mm_cvtsi128_si32(_mm_srli_si128(pos4, 12)));
        ASSUME(pos0 <= pos1); ASSUME(pos1 <= pos2); ASSUME(pos2 <= pos3);
        const auto val1 = _mm_setr_ps(src[pos0], src[pos1], src[pos2], src[pos3]);
        const auto val2 = _mm_setr_ps(src[pos0+1_uz], src[pos1+1_uz], src[pos2+1_uz], src[pos3+1_uz]);

        /* val1 + (val2-val1)*mu */
        const auto r0 = _mm_sub_ps(val2, val1);
        const auto mu = _mm_mul_ps(_mm_cvtepi32_ps(frac4), fracOne4);
        const auto out = _mm_add_ps(val1, _mm_mul_ps(mu, r0));

        frac4 = _mm_add_epi32(frac4, increment4);
        pos4 = _mm_add_epi32(pos4, _mm_srli_epi32(frac4, MixerFracBits));
        frac4 = _mm_and_si128(frac4, fracMask4);
        return out;
    });

    if(const auto todo = dst.size()&3)
    {
        auto pos = size_t{as_unsigned(_mm_cvtsi128_si32(pos4))};
        frac = as_unsigned(_mm_cvtsi128_si32(frac4));

        std::ranges::generate(dst.last(todo), [src,increment,&pos,&frac]
        {
            const auto smp = lerpf(src[pos+0], src[pos+1],
                gsl::narrow_cast<float>(frac) * (1.0f/MixerFracOne));

            frac += increment;
            pos  += frac>>MixerFracBits;
            frac &= MixerFracMask;
            return smp;
        });
    }
}

template<>
void Resample_<CubicTag,SSE2Tag>(const InterpState *state, const std::span<const float> src,
    uint frac, const uint increment, const std::span<float> dst)
{
    ASSUME(frac < MixerFracOne);

    const auto filter = std::get<CubicState>(*state).filter;

    const auto increment4 = _mm_set1_epi32(as_signed(increment*4));
    const auto fracMask4 = _mm_set1_epi32(MixerFracMask);
    const auto fracDiffOne4 = _mm_set1_ps(1.0f/CubicPhaseDiffOne);
    const auto fracDiffMask4 = _mm_set1_epi32(CubicPhaseDiffMask);

    auto pos_ = std::array<uint,4>{};
    auto frac_ = std::array<uint,4>{};
    InitPosArrays(MaxResamplerEdge-1, frac, increment, std::span{frac_}, std::span{pos_});
    auto pos4 = _mm_setr_epi32(as_signed(pos_[0]), as_signed(pos_[1]), as_signed(pos_[2]),
        as_signed(pos_[3]));
    auto frac4 = _mm_setr_epi32(as_signed(frac_[0]), as_signed(frac_[1]), as_signed(frac_[2]),
        as_signed(frac_[3]));

    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    std::ranges::generate(std::span{reinterpret_cast<__m128*>(dst.data()), dst.size()/4},
        [src,filter,increment4,fracMask4,fracDiffOne4,fracDiffMask4,&pos4,&frac4]
    {
        const auto pos0 = as_unsigned(_mm_cvtsi128_si32(pos4));
        const auto pos1 = as_unsigned(_mm_cvtsi128_si32(_mm_srli_si128(pos4, 4)));
        const auto pos2 = as_unsigned(_mm_cvtsi128_si32(_mm_srli_si128(pos4, 8)));
        const auto pos3 = as_unsigned(_mm_cvtsi128_si32(_mm_srli_si128(pos4, 12)));
        ASSUME(pos0 <= pos1); ASSUME(pos1 <= pos2); ASSUME(pos2 <= pos3);
        const auto val0 = _mm_loadu_ps(&src[pos0]);
        const auto val1 = _mm_loadu_ps(&src[pos1]);
        const auto val2 = _mm_loadu_ps(&src[pos2]);
        const auto val3 = _mm_loadu_ps(&src[pos3]);

        const auto pi4 = _mm_srli_epi32(frac4, CubicPhaseDiffBits);
        const auto pi0 = as_unsigned(_mm_cvtsi128_si32(pi4));
        const auto pi1 = as_unsigned(_mm_cvtsi128_si32(_mm_srli_si128(pi4, 4)));
        const auto pi2 = as_unsigned(_mm_cvtsi128_si32(_mm_srli_si128(pi4, 8)));
        const auto pi3 = as_unsigned(_mm_cvtsi128_si32(_mm_srli_si128(pi4, 12)));
        ASSUME(pi0 < CubicPhaseCount); ASSUME(pi1 < CubicPhaseCount);
        ASSUME(pi2 < CubicPhaseCount); ASSUME(pi3 < CubicPhaseCount);

        const auto pf4 = _mm_mul_ps(_mm_cvtepi32_ps(_mm_and_si128(frac4, fracDiffMask4)),
            fracDiffOne4);

        auto r0 = _mm_mul_ps(val0,
            vmadd(_mm_load_ps(filter[pi0].mCoeffs.data()),
                _mm_shuffle_ps(pf4, pf4, _MM_SHUFFLE(0, 0, 0, 0)),
                _mm_load_ps(filter[pi0].mDeltas.data())));
        auto r1 = _mm_mul_ps(val1,
            vmadd(_mm_load_ps(filter[pi1].mCoeffs.data()),
                _mm_shuffle_ps(pf4, pf4, _MM_SHUFFLE(1, 1, 1, 1)),
                _mm_load_ps(filter[pi1].mDeltas.data())));
        auto r2 = _mm_mul_ps(val2,
            vmadd(_mm_load_ps(filter[pi2].mCoeffs.data()),
                _mm_shuffle_ps(pf4, pf4, _MM_SHUFFLE(2, 2, 2, 2)),
                _mm_load_ps(filter[pi2].mDeltas.data())));
        auto r3 = _mm_mul_ps(val3,
            vmadd(_mm_load_ps(filter[pi3].mCoeffs.data()),
                _mm_shuffle_ps(pf4, pf4, _MM_SHUFFLE(3, 3, 3, 3)),
                _mm_load_ps(filter[pi3].mDeltas.data())));

        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
        r0 = _mm_add_ps(_mm_add_ps(r0, r1), _mm_add_ps(r2, r3));

        frac4 = _mm_add_epi32(frac4, increment4);
        pos4 = _mm_add_epi32(pos4, _mm_srli_epi32(frac4, MixerFracBits));
        frac4 = _mm_and_si128(frac4, fracMask4);
        return r0;
    });

    if(const auto todo = dst.size()&3)
    {
        auto pos = size_t{as_unsigned(_mm_cvtsi128_si32(pos4))};
        frac = as_unsigned(_mm_cvtsi128_si32(frac4));

        std::ranges::generate(dst.last(todo), [src,filter,increment,&pos,&frac]
        {
            const auto pi = size_t{frac >> CubicPhaseDiffBits}; ASSUME(pi < CubicPhaseCount);
            const auto pf = gsl::narrow_cast<float>(frac&CubicPhaseDiffMask)
                * (1.0f/CubicPhaseDiffOne);
            const auto pf4 = _mm_set1_ps(pf);

            const auto f4 = vmadd(_mm_load_ps(filter[pi].mCoeffs.data()), pf4,
                _mm_load_ps(filter[pi].mDeltas.data()));
            auto r4 = _mm_mul_ps(f4, _mm_loadu_ps(&src[pos]));

            r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
            r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
            const auto output = _mm_cvtss_f32(r4);

            frac += increment;
            pos  += frac>>MixerFracBits;
            frac &= MixerFracMask;
            return output;
        });
    }
}
