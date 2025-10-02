#ifndef PHASE_SHIFTER_H
#define PHASE_SHIFTER_H

#include "config_simd.h"

#if HAVE_SSE_INTRINSICS
#include <xmmintrin.h>
#elif HAVE_NEON
#include <arm_neon.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <ranges>
#include <span>

#include "alnumeric.h"
#include "gsl/gsl"
#include "opthelpers.h"


/* Implements a wide-band +90 degree phase-shift. Note that this should be
 * given one sample less of a delay (FilterSize/2 - 1) compared to the direct
 * signal delay (FilterSize/2) to properly align.
 */
template<std::size_t FilterSize>
class PhaseShifterT {
    static_assert(FilterSize >= 16, "FilterSize needs to be at least 16");
    static_assert((FilterSize&(FilterSize-1)) == 0, "FilterSize needs to be power-of-two");

    alignas(16) std::array<float,FilterSize/2> mCoeffs{};

#if HAVE_NEON
    static auto load4(float32_t a, float32_t b, float32_t c, float32_t d) -> float32x4_t
    {
        auto ret = vmovq_n_f32(a);
        ret = vsetq_lane_f32(b, ret, 1);
        ret = vsetq_lane_f32(c, ret, 2);
        ret = vsetq_lane_f32(d, ret, 3);
        return ret;
    }
    static void vtranspose4(float32x4_t &x0, float32x4_t &x1, float32x4_t &x2, float32x4_t &x3)
    {
        const auto t0_ = vzipq_f32(x0, x2);
        const auto t1_ = vzipq_f32(x1, x3);
        const auto u0_ = vzipq_f32(t0_.val[0], t1_.val[0]);
        const auto u1_ = vzipq_f32(t0_.val[1], t1_.val[1]);
        x0 = u0_.val[0];
        x1 = u0_.val[1];
        x2 = u1_.val[0];
        x3 = u1_.val[1];
    }
#endif

public:
    PhaseShifterT() noexcept
    {
        /* Every other coefficient is 0, so we only need to calculate and store
         * the non-0 terms and double-step over the input to apply it. The
         * calculated coefficients are in reverse to make applying in the time-
         * domain more efficient.
         */
        for(const auto i : std::views::iota(0_uz, FilterSize/2))
        {
            const auto k = gsl::narrow_cast<int>(i*2 + 1) - int{FilterSize/2};

            /* Calculate the Blackman window value for this coefficient. */
            const auto w = 2.0*std::numbers::pi/double{FilterSize}
                * gsl::narrow_cast<double>(i*2 + 1);
            const auto window = 0.3635819 - 0.4891775*std::cos(w) + 0.1365995*std::cos(2.0*w)
                - 0.0106411*std::cos(3.0*w);

            const auto pk = std::numbers::pi * gsl::narrow_cast<double>(k);
            mCoeffs[i] = gsl::narrow_cast<float>(window * (1.0-std::cos(pk)) / pk);
        }
    }

    NOINLINE void process(const std::span<float> dst, std::span<const float> src) const
    {
#if HAVE_SSE_INTRINSICS
        /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
         * Need to be able to cast floats to SIMD float types.
         */
        if(const auto todo = dst.size()>>2_uz)
        {
            auto out = std::span{reinterpret_cast<__m128*>(dst.data()), todo};
            std::ranges::generate(out, [&src,this]
            {
                auto r0 = _mm_setzero_ps();
                auto r1 = _mm_setzero_ps();
                auto r2 = _mm_setzero_ps();
                auto r3 = _mm_setzero_ps();
                for(auto j = 0_uz;j < mCoeffs.size();j+=4)
                {
                    const auto coeffs = _mm_load_ps(&mCoeffs[j]);
                    const auto s0 = _mm_loadu_ps(&src[j*2]);
                    const auto s1 = _mm_loadu_ps(&src[j*2 + 4]);
                    const auto s2 = _mm_movehl_ps(_mm_movelh_ps(s1, s1), s0);
                    const auto s3 = _mm_loadh_pi(_mm_movehl_ps(s1, s1),
                        reinterpret_cast<const __m64*>(&src[j*2 + 8]));

                    auto s = _mm_shuffle_ps(s0, s1, _MM_SHUFFLE(2, 0, 2, 0));
                    r0 = _mm_add_ps(r0, _mm_mul_ps(s, coeffs));

                    s = _mm_shuffle_ps(s0, s1, _MM_SHUFFLE(3, 1, 3, 1));
                    r1 = _mm_add_ps(r1, _mm_mul_ps(s, coeffs));

                    s = _mm_shuffle_ps(s2, s3, _MM_SHUFFLE(2, 0, 2, 0));
                    r2 = _mm_add_ps(r2, _mm_mul_ps(s, coeffs));

                    s = _mm_shuffle_ps(s2, s3, _MM_SHUFFLE(3, 1, 3, 1));
                    r3 = _mm_add_ps(r3, _mm_mul_ps(s, coeffs));
                }
                src = src.subspan(4);

                _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
                return _mm_add_ps(_mm_add_ps(r0, r1), _mm_add_ps(r2, r3));
            });
        }
        if(const auto todo = dst.size()&3)
        {
            std::ranges::generate(dst.last(todo), [&src,this]
            {
                auto r4 = _mm_setzero_ps();
                for(auto j = 0_uz;j < mCoeffs.size();j+=4)
                {
                    const auto coeffs = _mm_load_ps(&mCoeffs[j]);
                    const auto s = _mm_setr_ps(src[j*2], src[j*2 + 2], src[j*2 + 4], src[j*2 + 6]);
                    r4 = _mm_add_ps(r4, _mm_mul_ps(s, coeffs));
                }
                src = src.subspan(1);

                r4 = _mm_add_ps(r4, _mm_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3)));
                r4 = _mm_add_ps(r4, _mm_movehl_ps(r4, r4));
                return _mm_cvtss_f32(r4);
            });
        }

#elif HAVE_NEON

        if(const std::size_t todo{dst.size()>>2})
        {
            auto out = std::span{reinterpret_cast<float32x4_t*>(dst.data()), todo};
            std::generate(out.begin(), out.end(), [&src,this]
            {
                auto r0 = vdupq_n_f32(0.0f);
                auto r1 = vdupq_n_f32(0.0f);
                auto r2 = vdupq_n_f32(0.0f);
                auto r3 = vdupq_n_f32(0.0f);
                for(auto j = 0_uz;j < mCoeffs.size();j+=4)
                {
                    const auto coeffs = vld1q_f32(&mCoeffs[j]);
                    const auto s0 = vld1q_f32(&src[j*2]);
                    const auto s1 = vld1q_f32(&src[j*2 + 4]);
                    const auto s2 = vcombine_f32(vget_high_f32(s0), vget_low_f32(s1));
                    const auto s3 = vcombine_f32(vget_high_f32(s1), vld1_f32(&src[j*2 + 8]));
                    const auto values0 = vuzpq_f32(s0, s1);
                    const auto values1 = vuzpq_f32(s2, s3);

                    r0 = vmlaq_f32(r0, values0.val[0], coeffs);
                    r1 = vmlaq_f32(r1, values0.val[1], coeffs);
                    r2 = vmlaq_f32(r2, values1.val[0], coeffs);
                    r3 = vmlaq_f32(r3, values1.val[1], coeffs);
                }
                src = src.subspan(4);

                vtranspose4(r0, r1, r2, r3);
                return vaddq_f32(vaddq_f32(r0, r1), vaddq_f32(r2, r3));
            });
        }
        if(const auto todo = dst.size()&3)
        {
            std::ranges::generate(dst.last(todo), [&src,this]
            {
                auto r4 = vdupq_n_f32(0.0f);
                for(auto j = 0_uz;j < mCoeffs.size();j+=4)
                {
                    const auto coeffs = vld1q_f32(&mCoeffs[j]);
                    const auto s = load4(src[j*2], src[j*2 + 2], src[j*2 + 4], src[j*2 + 6]);
                    r4 = vmlaq_f32(r4, s, coeffs);
                }
                src = src.subspan(1);
                r4 = vaddq_f32(r4, vrev64q_f32(r4));
                return vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0);
            });
        }
        /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

#else

        std::ranges::generate(dst, [&src,this]
        {
            auto ret = 0.0f;
            for(auto j = 0_uz;j < mCoeffs.size();++j)
                ret += src[j*2] * mCoeffs[j];
            src = src.subspan(1);
            return ret;
        });
#endif
    }
};

#endif /* PHASE_SHIFTER_H */
