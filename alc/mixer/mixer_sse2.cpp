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

#include "alu.h"
#include "defs.h"


template<>
const ALfloat *Resample_<LerpTag,SSE2Tag>(const InterpState*, const ALfloat *RESTRICT src,
    ALuint frac, ALuint increment, const al::span<float> dst)
{
    const __m128i increment4{_mm_set1_epi32(static_cast<int>(increment*4))};
    const __m128 fracOne4{_mm_set1_ps(1.0f/FRACTIONONE)};
    const __m128i fracMask4{_mm_set1_epi32(FRACTIONMASK)};

    alignas(16) ALuint pos_[4], frac_[4];
    InitPosArrays(frac, increment, frac_, pos_, 4);
    __m128i frac4{_mm_setr_epi32(static_cast<int>(frac_[0]), static_cast<int>(frac_[1]),
        static_cast<int>(frac_[2]), static_cast<int>(frac_[3]))};
    __m128i pos4{_mm_setr_epi32(static_cast<int>(pos_[0]), static_cast<int>(pos_[1]),
        static_cast<int>(pos_[2]), static_cast<int>(pos_[3]))};

    auto dst_iter = dst.begin();
    const auto aligned_end = (dst.size()&~3u) + dst_iter;
    while(dst_iter != aligned_end)
    {
        const int pos0{_mm_cvtsi128_si32(_mm_shuffle_epi32(pos4, _MM_SHUFFLE(0, 0, 0, 0)))};
        const int pos1{_mm_cvtsi128_si32(_mm_shuffle_epi32(pos4, _MM_SHUFFLE(1, 1, 1, 1)))};
        const int pos2{_mm_cvtsi128_si32(_mm_shuffle_epi32(pos4, _MM_SHUFFLE(2, 2, 2, 2)))};
        const int pos3{_mm_cvtsi128_si32(_mm_shuffle_epi32(pos4, _MM_SHUFFLE(3, 3, 3, 3)))};
        const __m128 val1{_mm_setr_ps(src[pos0  ], src[pos1  ], src[pos2  ], src[pos3  ])};
        const __m128 val2{_mm_setr_ps(src[pos0+1], src[pos1+1], src[pos2+1], src[pos3+1])};

        /* val1 + (val2-val1)*mu */
        const __m128 r0{_mm_sub_ps(val2, val1)};
        const __m128 mu{_mm_mul_ps(_mm_cvtepi32_ps(frac4), fracOne4)};
        const __m128 out{_mm_add_ps(val1, _mm_mul_ps(mu, r0))};

        _mm_store_ps(dst_iter, out);
        dst_iter += 4;

        frac4 = _mm_add_epi32(frac4, increment4);
        pos4 = _mm_add_epi32(pos4, _mm_srli_epi32(frac4, FRACTIONBITS));
        frac4 = _mm_and_si128(frac4, fracMask4);
    }

    if(dst_iter != dst.end())
    {
        src += static_cast<ALuint>(_mm_cvtsi128_si32(pos4));
        frac = static_cast<ALuint>(_mm_cvtsi128_si32(frac4));

        do {
            *(dst_iter++) = lerp(src[0], src[1], static_cast<float>(frac) * (1.0f/FRACTIONONE));

            frac += increment;
            src  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
        } while(dst_iter != dst.end());
    }
    return dst.begin();
}
