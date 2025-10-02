//$ nobt

/* Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )
 * Copyright (c) 2023  Christopher Robinson
 *
 * Based on original fortran 77 code from FFTPACKv4 from NETLIB
 * (http://www.netlib.org/fftpack), authored by Dr Paul Swarztrauber
 * of NCAR, in 1985.
 *
 * As confirmed by the NCAR fftpack software curators, the following
 * FFTPACKv5 license applies to FFTPACKv4 sources. My changes are
 * released under the same terms.
 *
 * FFTPACK license:
 *
 * http://www.cisl.ucar.edu/css/software/fftpack5/ftpk.html
 *
 * Copyright (c) 2004 the University Corporation for Atmospheric
 * Research ("UCAR"). All rights reserved. Developed by NCAR's
 * Computational and Information Systems Laboratory, UCAR,
 * www.cisl.ucar.edu.
 *
 * Redistribution and use of the Software in source and binary forms,
 * with or without modification, is permitted provided that the
 * following conditions are met:
 *
 * - Neither the names of NCAR's Computational and Information Systems
 * Laboratory, the University Corporation for Atmospheric Research,
 * nor the names of its sponsors or contributors may be used to
 * endorse or promote products derived from this Software without
 * specific prior written permission.
 *
 * - Redistributions of source code must retain the above copyright
 * notices, this list of conditions, and the disclaimer below.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions, and the disclaimer below in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
 * SOFTWARE.
 *
 *
 * PFFFT : a Pretty Fast FFT.
 *
 * This file is largerly based on the original FFTPACK implementation, modified
 * in order to take advantage of SIMD instructions of modern CPUs.
 */

#include "pffft.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include "almalloc.h"
#include "alnumeric.h"
#include "fmt/core.h"
#include "fmt/ranges.h"
#include "gsl/gsl"
#include "opthelpers.h"
#include "pragmadefs.h"


using uint = unsigned int;

namespace {

#if defined(__GNUC__) || defined(_MSC_VER)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

/* Vector support macros: the rest of the code is independent of
 * SSE/Altivec/NEON -- adding support for other platforms with 4-element
 * vectors should be limited to these macros
 */

/* Define PFFFT_SIMD_DISABLE if you want to use scalar code instead of SIMD code */
//#define PFFFT_SIMD_DISABLE

#ifndef PFFFT_SIMD_DISABLE
/*
 * Altivec support macros
 */
#if (defined(__ppc__) || defined(__ppc64__) || defined(__powerpc__) || defined(__powerpc64__)) \
    && (defined(__VEC__) || defined(__ALTIVEC__))
#include <altivec.h>
using v4sf = vector float;
constexpr auto SimdSize = 4u;
force_inline auto vzero() noexcept -> v4sf { return (vector float)vec_splat_u8(0); }
force_inline auto vmul(v4sf a, v4sf b) noexcept -> v4sf { return vec_madd(a, b, vzero()); }
force_inline auto vadd(v4sf a, v4sf b) noexcept -> v4sf { return vec_add(a, b); }
force_inline auto vmadd(v4sf a, v4sf b, v4sf c) noexcept -> v4sf { return vec_madd(a, b, c); }
force_inline auto vsub(v4sf a, v4sf b) noexcept -> v4sf { return vec_sub(a, b); }
force_inline auto ld_ps1(float a) noexcept -> v4sf { return vec_splats(a); }

force_inline auto vset4(float a, float b, float c, float d) noexcept -> v4sf
{
    /* There a more efficient way to do this? */
    alignas(16) auto vals = std::array{a, b, c, d};
    return vec_ld(0, vals.data());
}
force_inline auto vinsert0(v4sf v, float a) noexcept -> v4sf { return vec_insert(a, v, 0); }
force_inline auto vextract0(v4sf v) noexcept -> float { return vec_extract(v, 0); }

force_inline auto vswaphl(v4sf a, v4sf b) noexcept -> v4sf
{ return vec_perm(a,b, (vector unsigned char){16,17,18,19,20,21,22,23,8,9,10,11,12,13,14,15}); }

force_inline void interleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    out1 = vec_mergeh(in1, in2);
    out2 = vec_mergel(in1, in2);
}
force_inline void uninterleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    out1 = vec_perm(in1, in2, (vector unsigned char){0,1,2,3,8,9,10,11,16,17,18,19,24,25,26,27});
    out2 = vec_perm(in1, in2, (vector unsigned char){4,5,6,7,12,13,14,15,20,21,22,23,28,29,30,31});
}

force_inline void vtranspose4(v4sf &x0, v4sf &x1, v4sf &x2, v4sf &x3) noexcept
{
    auto y0 = vec_mergeh(x0, x2);
    auto y1 = vec_mergel(x0, x2);
    auto y2 = vec_mergeh(x1, x3);
    auto y3 = vec_mergel(x1, x3);
    x0 = vec_mergeh(y0, y2);
    x1 = vec_mergel(y0, y2);
    x2 = vec_mergeh(y1, y3);
    x3 = vec_mergel(y1, y3);
}

/*
 * SSE1 support macros
 */
#elif defined(__x86_64__) || defined(__SSE__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 1)

#include <xmmintrin.h>
using v4sf = __m128;
/* 4 floats by simd vector -- this is pretty much hardcoded in the preprocess/
 * finalize functions anyway so you will have to work if you want to enable AVX
 * with its 256-bit vectors.
 */
constexpr auto SimdSize = 4u;
force_inline auto vzero() noexcept -> v4sf { return _mm_setzero_ps(); }
force_inline auto vmul(v4sf a, v4sf b) noexcept -> v4sf { return _mm_mul_ps(a, b); }
force_inline auto vadd(v4sf a, v4sf b) noexcept -> v4sf { return _mm_add_ps(a, b); }
force_inline auto vmadd(v4sf a, v4sf b, v4sf c) noexcept -> v4sf
{ return _mm_add_ps(_mm_mul_ps(a,b), c); }
force_inline auto vsub(v4sf a, v4sf b) noexcept -> v4sf { return _mm_sub_ps(a, b); }
force_inline auto ld_ps1(float a) noexcept -> v4sf { return _mm_set1_ps(a); }

force_inline auto vset4(float a, float b, float c, float d) noexcept -> v4sf
{ return _mm_setr_ps(a, b, c, d); }
force_inline auto vinsert0(const v4sf v, const float a) noexcept -> v4sf
{ return _mm_move_ss(v, _mm_set_ss(a)); }
force_inline auto vextract0(v4sf v) noexcept -> float
{ return _mm_cvtss_f32(v); }

force_inline auto vswaphl(v4sf a, v4sf b) noexcept -> v4sf
{ return _mm_shuffle_ps(b, a, _MM_SHUFFLE(3,2,1,0)); }

force_inline void interleave2(const v4sf in1, const v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    out1 = _mm_unpacklo_ps(in1, in2);
    out2 = _mm_unpackhi_ps(in1, in2);
}
force_inline void uninterleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    out1 = _mm_shuffle_ps(in1, in2, _MM_SHUFFLE(2,0,2,0));
    out2 = _mm_shuffle_ps(in1, in2, _MM_SHUFFLE(3,1,3,1));
}

force_inline void vtranspose4(v4sf &x0, v4sf &x1, v4sf &x2, v4sf &x3) noexcept
{ _MM_TRANSPOSE4_PS(x0, x1, x2, x3); }

/*
 * ARM NEON support macros
 */
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(__arm64) || defined(_M_ARM64)

#include <arm_neon.h>
using v4sf = float32x4_t;
constexpr auto SimdSize = 4u;
force_inline auto vzero() noexcept -> v4sf { return vdupq_n_f32(0.0f); }
force_inline auto vmul(v4sf a, v4sf b) noexcept -> v4sf { return vmulq_f32(a, b); }
force_inline auto vadd(v4sf a, v4sf b) noexcept -> v4sf { return vaddq_f32(a, b); }
force_inline auto vmadd(v4sf a, v4sf b, v4sf c) noexcept -> v4sf { return vmlaq_f32(c, a, b); }
force_inline auto vsub(v4sf a, v4sf b) noexcept -> v4sf { return vsubq_f32(a, b); }
force_inline auto ld_ps1(float a) noexcept -> v4sf { return vdupq_n_f32(a); }

force_inline auto vset4(float a, float b, float c, float d) noexcept -> v4sf
{
    auto ret = vmovq_n_f32(a);
    ret = vsetq_lane_f32(b, ret, 1);
    ret = vsetq_lane_f32(c, ret, 2);
    ret = vsetq_lane_f32(d, ret, 3);
    return ret;
}
force_inline auto vinsert0(v4sf v, float a) noexcept -> v4sf
{ return vsetq_lane_f32(a, v, 0); }
force_inline auto vextract0(v4sf v) noexcept -> float
{ return vgetq_lane_f32(v, 0); }

force_inline auto vswaphl(v4sf a, v4sf b) noexcept -> v4sf
{ return vcombine_f32(vget_low_f32(b), vget_high_f32(a)); }

force_inline void interleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    auto tmp = vzipq_f32(in1, in2);
    out1 = tmp.val[0];
    out2 = tmp.val[1];
}
force_inline void uninterleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    auto tmp = vuzpq_f32(in1, in2);
    out1 = tmp.val[0];
    out2 = tmp.val[1];
}

force_inline void vtranspose4(v4sf &x0, v4sf &x1, v4sf &x2, v4sf &x3) noexcept
{
    /* marginally faster version:
     * asm("vtrn.32 %q0, %q1;\n"
     *     "vtrn.32 %q2, %q3\n
     *     "vswp %f0, %e2\n
     *     "vswp %f1, %e3"
     *     : "+w"(x0), "+w"(x1), "+w"(x2), "+w"(x3)::);
     */
    auto t0_ = vzipq_f32(x0, x2);
    auto t1_ = vzipq_f32(x1, x3);
    auto u0_ = vzipq_f32(t0_.val[0], t1_.val[0]);
    auto u1_ = vzipq_f32(t0_.val[1], t1_.val[1]);
    x0 = u0_.val[0];
    x1 = u0_.val[1];
    x2 = u1_.val[0];
    x3 = u1_.val[1];
}

/*
 * Generic GCC vector macros
 */
#elif defined(__GNUC__)

using v4sf [[gnu::vector_size(16), gnu::aligned(16)]] = float;
constexpr auto SimdSize = 4u;
force_inline constexpr auto vzero() noexcept -> v4sf { return v4sf{0.0f, 0.0f, 0.0f, 0.0f}; }
force_inline constexpr auto vmul(v4sf a, v4sf b) noexcept -> v4sf { return a * b; }
force_inline constexpr auto vadd(v4sf a, v4sf b) noexcept -> v4sf { return a + b; }
force_inline constexpr auto vmadd(v4sf a, v4sf b, v4sf c) noexcept -> v4sf { return a*b + c; }
force_inline constexpr auto vsub(v4sf a, v4sf b) noexcept -> v4sf { return a - b; }
force_inline constexpr auto ld_ps1(float a) noexcept -> v4sf { return v4sf{a, a, a, a}; }

force_inline constexpr auto vset4(float a, float b, float c, float d) noexcept -> v4sf
{ return v4sf{a, b, c, d}; }
force_inline constexpr auto vinsert0(v4sf v, float a) noexcept -> v4sf
{ return v4sf{a, v[1], v[2], v[3]}; }
force_inline auto vextract0(v4sf v) noexcept -> float { return v[0]; }

force_inline auto unpacklo(v4sf a, v4sf b) noexcept -> v4sf
{ return v4sf{a[0], b[0], a[1], b[1]}; }
force_inline auto unpackhi(v4sf a, v4sf b) noexcept -> v4sf
{ return v4sf{a[2], b[2], a[3], b[3]}; }

force_inline auto vswaphl(v4sf a, v4sf b) noexcept -> v4sf
{ return v4sf{b[0], b[1], a[2], a[3]}; }

force_inline void interleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    out1 = unpacklo(in1, in2);
    out2 = unpackhi(in1, in2);
}
force_inline void uninterleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    out1 = v4sf{in1[0], in1[2], in2[0], in2[2]};
    out2 = v4sf{in1[1], in1[3], in2[1], in2[3]};
}

force_inline void vtranspose4(v4sf &x0, v4sf &x1, v4sf &x2, v4sf &x3) noexcept
{
    auto tmp0 = unpacklo(x0, x1);
    auto tmp2 = unpacklo(x2, x3);
    auto tmp1 = unpackhi(x0, x1);
    auto tmp3 = unpackhi(x2, x3);
    x0 = v4sf{tmp0[0], tmp0[1], tmp2[0], tmp2[1]};
    x1 = v4sf{tmp0[2], tmp0[3], tmp2[2], tmp2[3]};
    x2 = v4sf{tmp1[0], tmp1[1], tmp3[0], tmp3[1]};
    x3 = v4sf{tmp1[2], tmp1[3], tmp3[2], tmp3[3]};
}

#else

#warning "building with simd disabled !\n";
#define PFFFT_SIMD_DISABLE // fallback to scalar code
#endif

#endif /* PFFFT_SIMD_DISABLE */

// fallback mode for situations where SIMD is not available, use scalar mode instead
#ifdef PFFFT_SIMD_DISABLE
using v4sf = float;
constexpr auto SimdSize = 1u;
force_inline constexpr auto vmul(v4sf a, v4sf b) noexcept -> v4sf { return a * b; }
force_inline constexpr auto vadd(v4sf a, v4sf b) noexcept -> v4sf { return a + b; }
force_inline constexpr auto vmadd(v4sf a, v4sf b, v4sf c) noexcept -> v4sf { return a*b + c; }
force_inline constexpr auto vsub(v4sf a, v4sf b) noexcept -> v4sf { return a - b; }
force_inline constexpr auto ld_ps1(float a) noexcept -> v4sf { return a; }

#else

[[maybe_unused, nodiscard]] inline
auto valigned(const float *ptr) noexcept -> bool
{
    static constexpr auto alignmask = uintptr_t{SimdSize*sizeof(float) - 1};
    return (std::bit_cast<uintptr_t>(ptr) & alignmask) == 0;
}
#endif

// shortcuts for complex multiplications
force_inline void vcplxmul(v4sf &ar, v4sf &ai, v4sf br, v4sf bi) noexcept
{
    auto tmp = vmul(ar, bi);
    ar = vsub(vmul(ar, br), vmul(ai, bi));
    ai = vmadd(ai, br, tmp);
}
force_inline void vcplxmulconj(v4sf &ar, v4sf &ai, v4sf br, v4sf bi) noexcept
{
    auto tmp = vmul(ar, bi);
    ar = vmadd(ai, bi, vmul(ar, br));
    ai = vsub(vmul(ai, br), tmp);
}

#if !defined(PFFFT_SIMD_DISABLE)

inline void assertv4(const std::span<const float,4> v_f [[maybe_unused]],
    const float f0 [[maybe_unused]], const float f1 [[maybe_unused]],
    const float f2 [[maybe_unused]], const float f3 [[maybe_unused]])
{ Expects(v_f[0] == f0 && v_f[1] == f1 && v_f[2] == f2 && v_f[3] == f3); }

template<typename T, T ...N>
constexpr auto make_float_array(std::integer_sequence<T,N...>)
{ return std::array{gsl::narrow<float>(N)...}; }

/* detect bugs with the vector support macros */
[[maybe_unused]] auto validate_pffft_simd() -> bool
{
    using float4 = std::array<float,4>;
    static constexpr auto f = make_float_array(std::make_index_sequence<16>{});

    auto a0_v = vset4(f[ 0], f[ 1], f[ 2], f[ 3]);
    auto a1_v = vset4(f[ 4], f[ 5], f[ 6], f[ 7]);
    auto a2_v = vset4(f[ 8], f[ 9], f[10], f[11]);
    auto a3_v = vset4(f[12], f[13], f[14], f[15]);

    auto t_v = vzero();
    auto t_f = std::bit_cast<float4>(t_v);
    fmt::println("VZERO={}", t_f);
    assertv4(t_f, 0, 0, 0, 0);

    t_v = vadd(a1_v, a2_v);
    t_f = std::bit_cast<float4>(t_v);
    fmt::println("VADD(4:7,8:11)={}", t_f);
    assertv4(t_f, 12, 14, 16, 18);

    t_v = vmul(a1_v, a2_v);
    t_f = std::bit_cast<float4>(t_v);
    fmt::println("VMUL(4:7,8:11)={}", t_f);
    assertv4(t_f, 32, 45, 60, 77);

    t_v = vmadd(a1_v, a2_v, a0_v);
    t_f = std::bit_cast<float4>(t_v);
    fmt::println("VMADD(4:7,8:11,0:3)={}", t_f);
    assertv4(t_f, 32, 46, 62, 80);

    auto u_v = v4sf{};
    interleave2(a1_v, a2_v, t_v, u_v);
    t_f = std::bit_cast<float4>(t_v);
    auto u_f = std::bit_cast<float4>(u_v);
    fmt::println("INTERLEAVE2(4:7,8:11)={} {}", t_f, u_f);
    assertv4(t_f, 4, 8, 5, 9);
    assertv4(u_f, 6, 10, 7, 11);

    uninterleave2(a1_v, a2_v, t_v, u_v);
    t_f = std::bit_cast<float4>(t_v);
    u_f = std::bit_cast<float4>(u_v);
    fmt::println("UNINTERLEAVE2(4:7,8:11)={} {}", t_f, u_f);
    assertv4(t_f, 4, 6, 8, 10);
    assertv4(u_f, 5, 7, 9, 11);

    t_v = ld_ps1(f[15]);
    t_f = std::bit_cast<float4>(t_v);
    fmt::println("LD_PS1(15)={}", t_f);
    assertv4(t_f, 15, 15, 15, 15);

    t_v = vswaphl(a1_v, a2_v);
    t_f = std::bit_cast<float4>(t_v);
    fmt::println("VSWAPHL(4:7,8:11)={}", t_f);
    assertv4(t_f, 8, 9, 6, 7);

    vtranspose4(a0_v, a1_v, a2_v, a3_v);
    auto a0_f = std::bit_cast<float4>(a0_v);
    auto a1_f = std::bit_cast<float4>(a1_v);
    auto a2_f = std::bit_cast<float4>(a2_v);
    auto a3_f = std::bit_cast<float4>(a3_v);
    fmt::println("VTRANSPOSE4(0:3,4:7,8:11,12:15)={} {} {} {}", a0_f, a1_f, a2_f, a3_f);
    assertv4(a0_f, 0, 4, 8, 12);
    assertv4(a1_f, 1, 5, 9, 13);
    assertv4(a2_f, 2, 6, 10, 14);
    assertv4(a3_f, 3, 7, 11, 15);

    return true;
}
#endif //!PFFFT_SIMD_DISABLE

/* SSE and co like 16-bytes aligned pointers */
/* with a 64-byte alignment, we are even aligned on L2 cache lines... */
constexpr auto V4sfAlignment = 64_uz;
constexpr auto V4sfAlignVal = gsl::narrow<std::align_val_t>(V4sfAlignment);

/* NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
 * FIXME: Converting this from raw pointers to spans or something will probably
 * need significant work to maintain performance, given non-sequential range-
 * checked accesses and lack of 'restrict' to indicate non-aliased memory. At
 * least, some tests should be done to check the impact of using range-checked
 * spans here before blindly switching.
 */
/*
  passf2 and passb2 has been merged here, fsign = -1 for passf2, +1 for passb2
*/
NOINLINE void passf2_ps(const size_t ido, const size_t l1, const v4sf *cc, v4sf *RESTRICT ch,
    const float *const wa1, const float fsign)
{
    const auto l1ido = l1*ido;
    if(ido <= 2)
    {
        for(auto k = 0_uz;k < l1ido;k += ido, ch += ido, cc += 2*ido)
        {
            ch[0]         = vadd(cc[0], cc[ido+0]);
            ch[l1ido]     = vsub(cc[0], cc[ido+0]);
            ch[1]         = vadd(cc[1], cc[ido+1]);
            ch[l1ido + 1] = vsub(cc[1], cc[ido+1]);
        }
    }
    else
    {
        for(auto k = 0_uz;k < l1ido;k += ido, ch += ido, cc += 2*ido)
        {
            for(auto i = 0_uz;i < ido-1;i += 2)
            {
                auto tr2 = vsub(cc[i+0], cc[i+ido+0]);
                auto ti2 = vsub(cc[i+1], cc[i+ido+1]);
                auto wr = ld_ps1(wa1[i]);
                auto wi = ld_ps1(wa1[i+1]*fsign);
                ch[i]   = vadd(cc[i+0], cc[i+ido+0]);
                ch[i+1] = vadd(cc[i+1], cc[i+ido+1]);
                vcplxmul(tr2, ti2, wr, wi);
                ch[i+l1ido]   = tr2;
                ch[i+l1ido+1] = ti2;
            }
        }
    }
}

/*
  passf3 and passb3 has been merged here, fsign = -1 for passf3, +1 for passb3
*/
NOINLINE void passf3_ps(const size_t ido, const size_t l1, const v4sf *cc, v4sf *RESTRICT ch,
    const float *const wa1, const float fsign)
{
    Expects(ido > 2);

    const auto taur = ld_ps1(-0.5f);
    const auto taui = ld_ps1(0.866025403784439f*fsign);
    const auto l1ido = l1*ido;
    const auto wa2 = wa1 + ido;
    for(auto k = 0_uz;k < l1ido;k += ido, cc += 3*ido, ch +=ido)
    {
        for(auto i = 0_uz;i < ido-1;i += 2)
        {
            auto tr2 = vadd(cc[i+ido], cc[i+2*ido]);
            auto cr2 = vmadd(taur, tr2, cc[i]);
            ch[i]  = vadd(tr2, cc[i]);
            auto ti2 = vadd(cc[i+ido+1], cc[i+2*ido+1]);
            auto ci2 = vmadd(taur, ti2, cc[i+1]);
            ch[i+1] = vadd(cc[i+1], ti2);
            auto cr3 = vmul(taui, vsub(cc[i+ido], cc[i+2*ido]));
            auto ci3 = vmul(taui, vsub(cc[i+ido+1], cc[i+2*ido+1]));
            auto dr2 = vsub(cr2, ci3);
            auto dr3 = vadd(cr2, ci3);
            auto di2 = vadd(ci2, cr3);
            auto di3 = vsub(ci2, cr3);
            auto wr1 = wa1[i];
            auto wi1 = wa1[i+1]*fsign;
            auto wr2 = wa2[i];
            auto wi2 = wa2[i+1]*fsign;
            vcplxmul(dr2, di2, ld_ps1(wr1), ld_ps1(wi1));
            ch[i+l1ido] = dr2;
            ch[i+l1ido + 1] = di2;
            vcplxmul(dr3, di3, ld_ps1(wr2), ld_ps1(wi2));
            ch[i+2*l1ido] = dr3;
            ch[i+2*l1ido+1] = di3;
        }
    }
} /* passf3 */

NOINLINE void passf4_ps(const size_t ido, const size_t l1, const v4sf *cc, v4sf *RESTRICT ch,
    const float *const wa1, const float fsign)
{
    /* fsign == -1 for forward transform and +1 for backward transform */
    const auto vsign = ld_ps1(fsign);
    const auto l1ido = l1*ido;
    if(ido == 2)
    {
        for(auto k = 0_uz;k < l1ido;k += ido, ch += ido, cc += 4*ido)
        {
            auto tr1 = vsub(cc[0], cc[2*ido + 0]);
            auto tr2 = vadd(cc[0], cc[2*ido + 0]);
            auto ti1 = vsub(cc[1], cc[2*ido + 1]);
            auto ti2 = vadd(cc[1], cc[2*ido + 1]);
            auto ti4 = vmul(vsub(cc[1*ido + 0], cc[3*ido + 0]), vsign);
            auto tr4 = vmul(vsub(cc[3*ido + 1], cc[1*ido + 1]), vsign);
            auto tr3 = vadd(cc[ido + 0], cc[3*ido + 0]);
            auto ti3 = vadd(cc[ido + 1], cc[3*ido + 1]);

            ch[0*l1ido + 0] = vadd(tr2, tr3);
            ch[0*l1ido + 1] = vadd(ti2, ti3);
            ch[1*l1ido + 0] = vadd(tr1, tr4);
            ch[1*l1ido + 1] = vadd(ti1, ti4);
            ch[2*l1ido + 0] = vsub(tr2, tr3);
            ch[2*l1ido + 1] = vsub(ti2, ti3);
            ch[3*l1ido + 0] = vsub(tr1, tr4);
            ch[3*l1ido + 1] = vsub(ti1, ti4);
        }
    }
    else
    {
        const auto wa2 = wa1 + ido;
        const auto wa3 = wa2 + ido;
        for(auto k = 0_uz;k < l1ido;k += ido, ch+=ido, cc += 4*ido)
        {
            for(auto i = 0_uz;i < ido-1;i+=2)
            {
                auto tr1 = vsub(cc[i + 0], cc[i + 2*ido + 0]);
                auto tr2 = vadd(cc[i + 0], cc[i + 2*ido + 0]);
                auto ti1 = vsub(cc[i + 1], cc[i + 2*ido + 1]);
                auto ti2 = vadd(cc[i + 1], cc[i + 2*ido + 1]);
                auto tr4 = vmul(vsub(cc[i + 3*ido + 1], cc[i + 1*ido + 1]), vsign);
                auto ti4 = vmul(vsub(cc[i + 1*ido + 0], cc[i + 3*ido + 0]), vsign);
                auto tr3 = vadd(cc[i + ido + 0], cc[i + 3*ido + 0]);
                auto ti3 = vadd(cc[i + ido + 1], cc[i + 3*ido + 1]);

                ch[i] = vadd(tr2, tr3);
                auto cr3 = vsub(tr2, tr3);
                ch[i + 1] = vadd(ti2, ti3);
                auto ci3 = vsub(ti2, ti3);

                auto cr2 = vadd(tr1, tr4);
                auto cr4 = vsub(tr1, tr4);
                auto ci2 = vadd(ti1, ti4);
                auto ci4 = vsub(ti1, ti4);
                auto wr1 = wa1[i];
                auto wi1 = fsign*wa1[i+1];
                vcplxmul(cr2, ci2, ld_ps1(wr1), ld_ps1(wi1));
                auto wr2 = wa2[i];
                auto wi2 = fsign*wa2[i+1];
                ch[i + l1ido] = cr2;
                ch[i + l1ido + 1] = ci2;

                vcplxmul(cr3, ci3, ld_ps1(wr2), ld_ps1(wi2));
                auto wr3 = wa3[i];
                auto wi3 = fsign*wa3[i+1];
                ch[i + 2*l1ido] = cr3;
                ch[i + 2*l1ido + 1] = ci3;

                vcplxmul(cr4, ci4, ld_ps1(wr3), ld_ps1(wi3));
                ch[i + 3*l1ido] = cr4;
                ch[i + 3*l1ido + 1] = ci4;
            }
        }
    }
} /* passf4 */

/*
 * passf5 and passb5 has been merged here, fsign = -1 for passf5, +1 for passb5
 */
NOINLINE void passf5_ps(const size_t ido, const size_t l1, const v4sf *cc, v4sf *RESTRICT ch,
    const float *const wa1, const float fsign)
{
    const auto tr11 = ld_ps1(0.309016994374947f);
    const auto tr12 = ld_ps1(-0.809016994374947f);
    const auto ti11 = ld_ps1(0.951056516295154f*fsign);
    const auto ti12 = ld_ps1(0.587785252292473f*fsign);

    auto cc_ref = [&cc,ido](size_t a_1, size_t a_2) noexcept -> auto&
    { return cc[(a_2-1)*ido + a_1 + 1]; };
    auto ch_ref = [&ch,ido,l1](size_t a_1, size_t a_3) noexcept -> auto&
    { return ch[(a_3-1)*l1*ido + a_1 + 1]; };

    Expects(ido > 2);

    const auto wa2 = wa1 + ido;
    const auto wa3 = wa2 + ido;
    const auto wa4 = wa3 + ido;
    for(auto k = 0_uz;k < l1;++k, cc += 5*ido, ch += ido)
    {
        for(auto i = 0_uz;i < ido-1;i += 2)
        {
            auto ti5 = vsub(cc_ref(i  , 2), cc_ref(i  , 5));
            auto ti2 = vadd(cc_ref(i  , 2), cc_ref(i  , 5));
            auto ti4 = vsub(cc_ref(i  , 3), cc_ref(i  , 4));
            auto ti3 = vadd(cc_ref(i  , 3), cc_ref(i  , 4));
            auto tr5 = vsub(cc_ref(i-1, 2), cc_ref(i-1, 5));
            auto tr2 = vadd(cc_ref(i-1, 2), cc_ref(i-1, 5));
            auto tr4 = vsub(cc_ref(i-1, 3), cc_ref(i-1, 4));
            auto tr3 = vadd(cc_ref(i-1, 3), cc_ref(i-1, 4));
            ch_ref(i-1, 1) = vadd(cc_ref(i-1, 1), vadd(tr2, tr3));
            ch_ref(i  , 1) = vadd(cc_ref(i  , 1), vadd(ti2, ti3));
            auto cr2 = vadd(cc_ref(i-1, 1), vmadd(tr11, tr2, vmul(tr12, tr3)));
            auto ci2 = vadd(cc_ref(i  , 1), vmadd(tr11, ti2, vmul(tr12, ti3)));
            auto cr3 = vadd(cc_ref(i-1, 1), vmadd(tr12, tr2, vmul(tr11, tr3)));
            auto ci3 = vadd(cc_ref(i  , 1), vmadd(tr12, ti2, vmul(tr11, ti3)));
            auto cr5 = vmadd(ti11, tr5, vmul(ti12, tr4));
            auto ci5 = vmadd(ti11, ti5, vmul(ti12, ti4));
            auto cr4 = vsub(vmul(ti12, tr5), vmul(ti11, tr4));
            auto ci4 = vsub(vmul(ti12, ti5), vmul(ti11, ti4));
            auto dr3 = vsub(cr3, ci4);
            auto dr4 = vadd(cr3, ci4);
            auto di3 = vadd(ci3, cr4);
            auto di4 = vsub(ci3, cr4);
            auto dr5 = vadd(cr2, ci5);
            auto dr2 = vsub(cr2, ci5);
            auto di5 = vsub(ci2, cr5);
            auto di2 = vadd(ci2, cr5);
            auto wr1 = wa1[i];
            auto wi1 = fsign*wa1[i+1];
            auto wr2 = wa2[i];
            auto wi2 = fsign*wa2[i+1];
            auto wr3 = wa3[i];
            auto wi3 = fsign*wa3[i+1];
            auto wr4 = wa4[i];
            auto wi4 = fsign*wa4[i+1];
            vcplxmul(dr2, di2, ld_ps1(wr1), ld_ps1(wi1));
            ch_ref(i - 1, 2) = dr2;
            ch_ref(i, 2)     = di2;
            vcplxmul(dr3, di3, ld_ps1(wr2), ld_ps1(wi2));
            ch_ref(i - 1, 3) = dr3;
            ch_ref(i, 3)     = di3;
            vcplxmul(dr4, di4, ld_ps1(wr3), ld_ps1(wi3));
            ch_ref(i - 1, 4) = dr4;
            ch_ref(i, 4)     = di4;
            vcplxmul(dr5, di5, ld_ps1(wr4), ld_ps1(wi4));
            ch_ref(i - 1, 5) = dr5;
            ch_ref(i, 5)     = di5;
        }
    }
}

NOINLINE void radf2_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc,
    v4sf *RESTRICT ch, const float *const wa1)
{
    const auto l1ido = l1*ido;
    for(auto k = 0_uz;k < l1ido;k += ido)
    {
        auto a = cc[k];
        auto b = cc[k + l1ido];
        ch[2*k] = vadd(a, b);
        ch[2*(k+ido)-1] = vsub(a, b);
    }
    if(ido < 2)
        return;
    if(ido != 2)
    {
        for(auto k = 0_uz;k < l1ido;k += ido)
        {
            for(auto i = 2_uz;i < ido;i += 2)
            {
                auto tr2 = cc[i - 1 + k + l1ido];
                auto ti2 = cc[i + k + l1ido];
                auto br = cc[i - 1 + k];
                auto bi = cc[i + k];
                vcplxmulconj(tr2, ti2, ld_ps1(wa1[i - 2]), ld_ps1(wa1[i - 1]));
                ch[i + 2*k] = vadd(bi, ti2);
                ch[2*(k+ido) - i] = vsub(ti2, bi);
                ch[i - 1 + 2*k] = vadd(br, tr2);
                ch[2*(k+ido) - i -1] = vsub(br, tr2);
            }
        }
        if((ido&1) == 1)
            return;
    }
    const auto minus_one = ld_ps1(-1.0f);
    for(auto k = 0_uz;k < l1ido;k += ido)
    {
        ch[2*k + ido] = vmul(minus_one, cc[ido-1 + k + l1ido]);
        ch[2*k + ido-1] = cc[k + ido-1];
    }
} /* radf2 */


NOINLINE void radb2_ps(const size_t ido, const size_t l1, const v4sf *cc, v4sf *RESTRICT ch,
    const float *const wa1)
{
    const auto l1ido = l1*ido;
    for(auto k = 0_uz;k < l1ido;k += ido)
    {
        auto a = cc[2*k];
        auto b = cc[2*(k+ido) - 1];
        ch[k] = vadd(a, b);
        ch[k + l1ido] = vsub(a, b);
    }
    if(ido < 2)
        return;
    if(ido != 2)
    {
        for(auto k = 0_uz;k < l1ido;k += ido)
        {
            for(auto i = 2_uz;i < ido;i += 2)
            {
                auto a = cc[i-1 + 2*k];
                auto b = cc[2*(k + ido) - i - 1];
                auto c = cc[i+0 + 2*k];
                auto d = cc[2*(k + ido) - i + 0];
                ch[i-1 + k] = vadd(a, b);
                auto tr2 = vsub(a, b);
                ch[i+0 + k] = vsub(c, d);
                auto ti2 = vadd(c, d);
                vcplxmul(tr2, ti2, ld_ps1(wa1[i - 2]), ld_ps1(wa1[i - 1]));
                ch[i-1 + k + l1ido] = tr2;
                ch[i+0 + k + l1ido] = ti2;
            }
        }
        if((ido&1) == 1)
            return;
    }
    const auto minus_two = ld_ps1(-2.0f);
    for(auto k = 0_uz;k < l1ido;k += ido)
    {
        auto a = cc[2*k + ido-1];
        auto b = cc[2*k + ido];
        ch[k + ido-1] = vadd(a, a);
        ch[k + ido-1 + l1ido] = vmul(minus_two, b);
    }
} /* radb2 */

void radf3_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch,
    const float *const wa1)
{
    const auto taur = ld_ps1(-0.5f);
    const auto taui = ld_ps1(0.866025403784439f);
    for(auto k = 0_uz;k < l1;++k)
    {
        auto cr2 = vadd(cc[(k + l1)*ido], cc[(k + 2*l1)*ido]);
        ch[        (3*k    )*ido] = vadd(cc[k*ido], cr2);
        ch[        (3*k + 2)*ido] = vmul(taui, vsub(cc[(k + l1*2)*ido], cc[(k + l1)*ido]));
        ch[ido-1 + (3*k + 1)*ido] = vmadd(taur, cr2, cc[k*ido]);
    }
    if(ido == 1)
        return;

    const auto wa2 = wa1 + ido;
    for(auto k = 0_uz;k < l1;++k)
    {
        for(auto i = 2_uz;i < ido;i += 2)
        {
            const auto ic = ido - i;
            auto wr1 = ld_ps1(wa1[i - 2]);
            auto wi1 = ld_ps1(wa1[i - 1]);
            auto dr2 = cc[i - 1 + (k + l1)*ido];
            auto di2 = cc[i     + (k + l1)*ido];
            vcplxmulconj(dr2, di2, wr1, wi1);

            auto wr2 = ld_ps1(wa2[i - 2]);
            auto wi2 = ld_ps1(wa2[i - 1]);
            auto dr3 = cc[i - 1 + (k + l1*2)*ido];
            auto di3 = cc[i     + (k + l1*2)*ido];
            vcplxmulconj(dr3, di3, wr2, wi2);

            auto cr2 = vadd(dr2, dr3);
            auto ci2 = vadd(di2, di3);
            ch[i - 1 + 3*k*ido] = vadd(cc[i - 1 + k*ido], cr2);
            ch[i     + 3*k*ido] = vadd(cc[i     + k*ido], ci2);
            auto tr2 = vmadd(taur, cr2, cc[i - 1 + k*ido]);
            auto ti2 = vmadd(taur, ci2, cc[i     + k*ido]);
            auto tr3 = vmul(taui, vsub(di2, di3));
            auto ti3 = vmul(taui, vsub(dr3, dr2));
            ch[i  - 1 + (3*k + 2)*ido] = vadd(tr2, tr3);
            ch[ic - 1 + (3*k + 1)*ido] = vsub(tr2, tr3);
            ch[i      + (3*k + 2)*ido] = vadd(ti2, ti3);
            ch[ic     + (3*k + 1)*ido] = vsub(ti3, ti2);
        }
    }
} /* radf3 */


void radb3_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch,
    const float *const wa1)
{
    static constexpr auto taur = -0.5f;
    static constexpr auto taui = 0.866025403784439f;
    static constexpr auto taui_2 = taui*2.0f;

    const auto vtaur = ld_ps1(taur);
    const auto vtaui_2 = ld_ps1(taui_2);
    for(auto k = 0_uz;k < l1;++k)
    {
        auto tr2 = cc[ido-1 + (3*k + 1)*ido];
        tr2 = vadd(tr2,tr2);
        auto cr2 = vmadd(vtaur, tr2, cc[3*k*ido]);
        ch[k*ido] = vadd(cc[3*k*ido], tr2);
        auto ci3 = vmul(vtaui_2, cc[(3*k + 2)*ido]);
        ch[(k + l1)*ido] = vsub(cr2, ci3);
        ch[(k + 2*l1)*ido] = vadd(cr2, ci3);
    }
    if(ido == 1)
        return;

    const auto wa2 = wa1 + ido;
    const auto vtaui = ld_ps1(taui);
    for(auto k = 0_uz;k < l1;++k)
    {
        for(auto i = 2_uz;i < ido;i += 2)
        {
            const auto ic = ido - i;
            auto tr2 = vadd(cc[i - 1 + (3*k + 2)*ido], cc[ic - 1 + (3*k + 1)*ido]);
            auto cr2 = vmadd(vtaur, tr2, cc[i - 1 + 3*k*ido]);
            ch[i - 1 + k*ido] = vadd(cc[i - 1 + 3*k*ido], tr2);
            auto ti2 = vsub(cc[i + (3*k + 2)*ido], cc[ic + (3*k + 1)*ido]);
            auto ci2 = vmadd(vtaur, ti2, cc[i + 3*k*ido]);
            ch[i + k*ido] = vadd(cc[i + 3*k*ido], ti2);
            auto cr3 = vmul(vtaui, vsub(cc[i - 1 + (3*k + 2)*ido], cc[ic - 1 + (3*k + 1)*ido]));
            auto ci3 = vmul(vtaui, vadd(cc[i + (3*k + 2)*ido], cc[ic + (3*k + 1)*ido]));
            auto dr2 = vsub(cr2, ci3);
            auto dr3 = vadd(cr2, ci3);
            auto di2 = vadd(ci2, cr3);
            auto di3 = vsub(ci2, cr3);
            vcplxmul(dr2, di2, ld_ps1(wa1[i-2]), ld_ps1(wa1[i-1]));
            ch[i - 1 + (k + l1)*ido] = dr2;
            ch[i + (k + l1)*ido] = di2;
            vcplxmul(dr3, di3, ld_ps1(wa2[i-2]), ld_ps1(wa2[i-1]));
            ch[i - 1 + (k + 2*l1)*ido] = dr3;
            ch[i + (k + 2*l1)*ido] = di3;
        }
    }
} /* radb3 */

NOINLINE void radf4_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc,
    v4sf *RESTRICT ch, const float *const wa1)
{
    const auto l1ido = l1*ido;
    {
        const auto *RESTRICT cc_ = cc;
        const auto *RESTRICT cc_end = cc + l1ido;
        auto *RESTRICT ch_ = ch;
        while(cc != cc_end)
        {
            // this loop represents between 25% and 40% of total radf4_ps cost !
            auto a0 = cc[0];
            auto a1 = cc[l1ido];
            auto a2 = cc[2*l1ido];
            auto a3 = cc[3*l1ido];
            auto tr1 = vadd(a1, a3);
            auto tr2 = vadd(a0, a2);
            ch[2*ido-1] = vsub(a0, a2);
            ch[2*ido  ] = vsub(a3, a1);
            ch[0      ] = vadd(tr1, tr2);
            ch[4*ido-1] = vsub(tr2, tr1);
            cc += ido; ch += 4*ido;
        }
        cc = cc_;
        ch = ch_;
    }
    if(ido < 2)
        return;
    if(ido != 2)
    {
        const auto wa2 = wa1 + ido;
        const auto wa3 = wa2 + ido;

        for(auto k = 0_uz;k < l1ido;k += ido)
        {
            const auto *RESTRICT pc = cc + 1 + k;
            for(auto i = 2_uz;i < ido;i += 2, pc += 2)
            {
                const auto ic = ido - i;

                auto cr2 = pc[1*l1ido+0];
                auto ci2 = pc[1*l1ido+1];
                auto wr = ld_ps1(wa1[i - 2]);
                auto wi = ld_ps1(wa1[i - 1]);
                vcplxmulconj(cr2, ci2, wr, wi);

                auto cr3 = pc[2*l1ido+0];
                auto ci3 = pc[2*l1ido+1];
                wr = ld_ps1(wa2[i-2]);
                wi = ld_ps1(wa2[i-1]);
                vcplxmulconj(cr3, ci3, wr, wi);

                auto cr4 = pc[3*l1ido];
                auto ci4 = pc[3*l1ido+1];
                wr = ld_ps1(wa3[i-2]);
                wi = ld_ps1(wa3[i-1]);
                vcplxmulconj(cr4, ci4, wr, wi);

                /* at this point, on SSE, five of "cr2 cr3 cr4 ci2 ci3 ci4" should be loaded in registers */

                auto tr1 = vadd(cr2,cr4);
                auto tr4 = vsub(cr4,cr2);
                auto tr2 = vadd(pc[0],cr3);
                auto tr3 = vsub(pc[0],cr3);
                ch[i  - 1 + 4*k        ] = vadd(tr2,tr1);
                ch[ic - 1 + 4*k + 3*ido] = vsub(tr2,tr1); // at this point tr1 and tr2 can be disposed
                auto ti1 = vadd(ci2,ci4);
                auto ti4 = vsub(ci2,ci4);
                ch[i  - 1 + 4*k + 2*ido] = vadd(tr3,ti4);
                ch[ic - 1 + 4*k + 1*ido] = vsub(tr3,ti4); // dispose tr3, ti4
                auto ti2 = vadd(pc[1],ci3);
                auto ti3 = vsub(pc[1],ci3);
                ch[i  + 4*k        ] = vadd(ti1, ti2);
                ch[ic + 4*k + 3*ido] = vsub(ti1, ti2);
                ch[i  + 4*k + 2*ido] = vadd(tr4, ti3);
                ch[ic + 4*k + 1*ido] = vsub(tr4, ti3);
            }
        }
        if((ido&1) == 1)
            return;
    }
    const auto minus_hsqt2 = ld_ps1(std::numbers::sqrt2_v<float> * -0.5f);
    for(auto k = 0_uz;k < l1ido;k += ido)
    {
        auto a = cc[ido-1 + k + l1ido];
        auto b = cc[ido-1 + k + 3*l1ido];
        auto c = cc[ido-1 + k];
        auto d = cc[ido-1 + k + 2*l1ido];
        auto ti1 = vmul(minus_hsqt2, vadd(b, a));
        auto tr1 = vmul(minus_hsqt2, vsub(b, a));
        ch[ido-1 + 4*k        ] = vadd(c, tr1);
        ch[ido-1 + 4*k + 2*ido] = vsub(c, tr1);
        ch[        4*k + 1*ido] = vsub(ti1, d);
        ch[        4*k + 3*ido] = vadd(ti1, d);
    }
} /* radf4 */


NOINLINE void radb4_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc,
    v4sf *RESTRICT ch, const float *const wa1)
{
    const auto two = ld_ps1(2.0f);
    const auto l1ido = l1*ido;
    {
        const auto *RESTRICT cc_ = cc;
        const auto *RESTRICT ch_end = ch + l1ido;
        auto *ch_ = ch;
        while(ch != ch_end)
        {
            auto a = cc[0];
            auto b = cc[4*ido-1];
            auto c = cc[2*ido];
            auto d = cc[2*ido-1];
            auto tr3 = vmul(two, d);
            auto tr2 = vadd(a, b);
            auto tr1 = vsub(a, b);
            auto tr4 = vmul(two, c);
            ch[0*l1ido] = vadd(tr2, tr3);
            ch[2*l1ido] = vsub(tr2, tr3);
            ch[1*l1ido] = vsub(tr1, tr4);
            ch[3*l1ido] = vadd(tr1, tr4);

            cc += 4*ido; ch += ido;
        }
        cc = cc_; ch = ch_;
    }
    if(ido < 2)
        return;
    if(ido != 2)
    {
        const auto wa2 = wa1 + ido;
        const auto wa3 = wa2 + ido;

        for(auto k = 0_uz;k < l1ido;k += ido)
        {
            const auto *RESTRICT pc = cc - 1 + 4*k;
            auto *RESTRICT ph = ch + k + 1;
            for(auto i = 2_uz;i < ido;i += 2)
            {
                auto tr1 = vsub(pc[        i], pc[4*ido - i]);
                auto tr2 = vadd(pc[        i], pc[4*ido - i]);
                auto ti4 = vsub(pc[2*ido + i], pc[2*ido - i]);
                auto tr3 = vadd(pc[2*ido + i], pc[2*ido - i]);
                ph[0] = vadd(tr2, tr3);
                auto cr3 = vsub(tr2, tr3);

                auto ti3 = vsub(pc[2*ido + i + 1], pc[2*ido - i + 1]);
                auto tr4 = vadd(pc[2*ido + i + 1], pc[2*ido - i + 1]);
                auto cr2 = vsub(tr1, tr4);
                auto cr4 = vadd(tr1, tr4);

                auto ti1 = vadd(pc[i + 1], pc[4*ido - i + 1]);
                auto ti2 = vsub(pc[i + 1], pc[4*ido - i + 1]);

                ph[1] = vadd(ti2, ti3); ph += l1ido;
                auto ci3 = vsub(ti2, ti3);
                auto ci2 = vadd(ti1, ti4);
                auto ci4 = vsub(ti1, ti4);
                vcplxmul(cr2, ci2, ld_ps1(wa1[i-2]), ld_ps1(wa1[i-1]));
                ph[0] = cr2;
                ph[1] = ci2; ph += l1ido;
                vcplxmul(cr3, ci3, ld_ps1(wa2[i-2]), ld_ps1(wa2[i-1]));
                ph[0] = cr3;
                ph[1] = ci3; ph += l1ido;
                vcplxmul(cr4, ci4, ld_ps1(wa3[i-2]), ld_ps1(wa3[i-1]));
                ph[0] = cr4;
                ph[1] = ci4; ph = ph - 3*l1ido + 2;
            }
        }
        if((ido&1) == 1)
            return;
    }
    const auto minus_sqrt2 = ld_ps1(-1.414213562373095f);
    for(auto k = 0_uz;k < l1ido;k += ido)
    {
        const auto i0 = 4*k + ido;
        auto c = cc[i0-1];
        auto d = cc[i0 + 2*ido-1];
        auto a = cc[i0+0];
        auto b = cc[i0 + 2*ido+0];
        auto tr1 = vsub(c, d);
        auto tr2 = vadd(c, d);
        auto ti1 = vadd(b, a);
        auto ti2 = vsub(b, a);
        ch[ido-1 + k + 0*l1ido] = vadd(tr2,tr2);
        ch[ido-1 + k + 1*l1ido] = vmul(minus_sqrt2, vsub(ti1, tr1));
        ch[ido-1 + k + 2*l1ido] = vadd(ti2, ti2);
        ch[ido-1 + k + 3*l1ido] = vmul(minus_sqrt2, vadd(ti1, tr1));
    }
} /* radb4 */

void radf5_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch,
    const float *const wa1)
{
    const auto tr11 = ld_ps1(0.309016994374947f);
    const auto ti11 = ld_ps1(0.951056516295154f);
    const auto tr12 = ld_ps1(-0.809016994374947f);
    const auto ti12 = ld_ps1(0.587785252292473f);

    auto cc_ref = [&cc,l1,ido](size_t a_1, size_t a_2, size_t a_3) noexcept -> auto&
    { return cc[(a_3*l1 + a_2)*ido + a_1]; };
    auto ch_ref = [&ch,ido](size_t a_1, size_t a_2, size_t a_3) noexcept -> auto&
    { return ch[(a_3*5 + a_2)*ido + a_1]; };

    /* Parameter adjustments */
    ch -= 1 + ido * 6;
    cc -= 1 + ido * (1 + l1);

    const auto wa2 = wa1 + ido;
    const auto wa3 = wa2 + ido;
    const auto wa4 = wa3 + ido;

    /* Function Body */
    for(auto k = 1_uz;k <= l1;++k)
    {
        auto cr2 = vadd(cc_ref(1, k, 5), cc_ref(1, k, 2));
        auto ci5 = vsub(cc_ref(1, k, 5), cc_ref(1, k, 2));
        auto cr3 = vadd(cc_ref(1, k, 4), cc_ref(1, k, 3));
        auto ci4 = vsub(cc_ref(1, k, 4), cc_ref(1, k, 3));
        ch_ref(1, 1, k)   = vadd(cc_ref(1, k, 1), vadd(cr2, cr3));
        ch_ref(ido, 2, k) = vadd(cc_ref(1, k, 1), vmadd(tr11, cr2, vmul(tr12, cr3)));
        ch_ref(1, 3, k)   = vmadd(ti11, ci5, vmul(ti12, ci4));
        ch_ref(ido, 4, k) = vadd(cc_ref(1, k, 1), vmadd(tr12, cr2, vmul(tr11, cr3)));
        ch_ref(1, 5, k)   = vsub(vmul(ti12, ci5), vmul(ti11, ci4));
        //fmt::println("pffft: radf5, k={} ch_ref={:f}, ci4={:f}", k, ch_ref(1, 5, k), ci4);
    }
    if(ido == 1)
        return;

    const auto idp2 = ido + 2_uz;
    for(auto k = 1_uz;k <= l1;++k)
    {
        for(auto i = 3_uz;i <= ido;i += 2)
        {
            const auto ic = idp2 - i;
            auto dr2 = ld_ps1(wa1[i-3]);
            auto di2 = ld_ps1(wa1[i-2]);
            auto dr3 = ld_ps1(wa2[i-3]);
            auto di3 = ld_ps1(wa2[i-2]);
            auto dr4 = ld_ps1(wa3[i-3]);
            auto di4 = ld_ps1(wa3[i-2]);
            auto dr5 = ld_ps1(wa4[i-3]);
            auto di5 = ld_ps1(wa4[i-2]);
            vcplxmulconj(dr2, di2, cc_ref(i-1, k, 2), cc_ref(i, k, 2));
            vcplxmulconj(dr3, di3, cc_ref(i-1, k, 3), cc_ref(i, k, 3));
            vcplxmulconj(dr4, di4, cc_ref(i-1, k, 4), cc_ref(i, k, 4));
            vcplxmulconj(dr5, di5, cc_ref(i-1, k, 5), cc_ref(i, k, 5));
            auto cr2 = vadd(dr2, dr5);
            auto ci5 = vsub(dr5, dr2);
            auto cr5 = vsub(di2, di5);
            auto ci2 = vadd(di2, di5);
            auto cr3 = vadd(dr3, dr4);
            auto ci4 = vsub(dr4, dr3);
            auto cr4 = vsub(di3, di4);
            auto ci3 = vadd(di3, di4);
            ch_ref(i - 1, 1, k) = vadd(cc_ref(i - 1, k, 1), vadd(cr2, cr3));
            ch_ref(i, 1, k) = vsub(cc_ref(i, k, 1), vadd(ci2, ci3));
            auto tr2 = vadd(cc_ref(i - 1, k, 1), vmadd(tr11, cr2, vmul(tr12, cr3)));
            auto ti2 = vsub(cc_ref(i, k, 1), vmadd(tr11, ci2, vmul(tr12, ci3)));
            auto tr3 = vadd(cc_ref(i - 1, k, 1), vmadd(tr12, cr2, vmul(tr11, cr3)));
            auto ti3 = vsub(cc_ref(i, k, 1), vmadd(tr12, ci2, vmul(tr11, ci3)));
            auto tr5 = vmadd(ti11, cr5, vmul(ti12, cr4));
            auto ti5 = vmadd(ti11, ci5, vmul(ti12, ci4));
            auto tr4 = vsub(vmul(ti12, cr5), vmul(ti11, cr4));
            auto ti4 = vsub(vmul(ti12, ci5), vmul(ti11, ci4));
            ch_ref(i  - 1, 3, k) = vsub(tr2, tr5);
            ch_ref(ic - 1, 2, k) = vadd(tr2, tr5);
            ch_ref(i     , 3, k) = vadd(ti5, ti2);
            ch_ref(ic    , 2, k) = vsub(ti5, ti2);
            ch_ref(i  - 1, 5, k) = vsub(tr3, tr4);
            ch_ref(ic - 1, 4, k) = vadd(tr3, tr4);
            ch_ref(i     , 5, k) = vadd(ti4, ti3);
            ch_ref(ic    , 4, k) = vsub(ti4, ti3);
        }
    }
} /* radf5 */

void radb5_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch,
    const float *const wa1)
{
    const auto tr11 = ld_ps1(0.309016994374947f);
    const auto ti11 = ld_ps1(0.951056516295154f);
    const auto tr12 = ld_ps1(-0.809016994374947f);
    const auto ti12 = ld_ps1(0.587785252292473f);

    auto cc_ref = [&cc,ido](size_t a_1, size_t a_2, size_t a_3) noexcept -> auto&
    { return cc[(a_3*5 + a_2)*ido + a_1]; };
    auto ch_ref = [&ch,ido,l1](size_t a_1, size_t a_2, size_t a_3) noexcept -> auto&
    { return ch[(a_3*l1 + a_2)*ido + a_1]; };

    /* Parameter adjustments */
    ch -= 1 + ido*(1 + l1);
    cc -= 1 + ido*6;

    const auto wa2 = wa1 + ido;
    const auto wa3 = wa2 + ido;
    const auto wa4 = wa3 + ido;

    /* Function Body */
    for(auto k = 1_uz;k <= l1;++k)
    {
        auto ti5 = vadd(cc_ref(  1, 3, k), cc_ref(1, 3, k));
        auto ti4 = vadd(cc_ref(  1, 5, k), cc_ref(1, 5, k));
        auto tr2 = vadd(cc_ref(ido, 2, k), cc_ref(ido, 2, k));
        auto tr3 = vadd(cc_ref(ido, 4, k), cc_ref(ido, 4, k));
        ch_ref(1, k, 1) = vadd(cc_ref(1, 1, k), vadd(tr2, tr3));
        auto cr2 = vadd(cc_ref(1, 1, k), vmadd(tr11, tr2, vmul(tr12, tr3)));
        auto cr3 = vadd(cc_ref(1, 1, k), vmadd(tr12, tr2, vmul(tr11, tr3)));
        auto ci5 = vmadd(ti11, ti5, vmul(ti12, ti4));
        auto ci4 = vsub(vmul(ti12, ti5), vmul(ti11, ti4));
        ch_ref(1, k, 2) = vsub(cr2, ci5);
        ch_ref(1, k, 3) = vsub(cr3, ci4);
        ch_ref(1, k, 4) = vadd(cr3, ci4);
        ch_ref(1, k, 5) = vadd(cr2, ci5);
    }
    if(ido == 1)
        return;

    const auto idp2 = ido + 2_uz;
    for(auto k = 1_uz;k <= l1;++k)
    {
        for(auto i = 3_uz;i <= ido;i += 2)
        {
            const auto ic = idp2 - i;
            auto ti5 = vadd(cc_ref(i  , 3, k), cc_ref(ic  , 2, k));
            auto ti2 = vsub(cc_ref(i  , 3, k), cc_ref(ic  , 2, k));
            auto ti4 = vadd(cc_ref(i  , 5, k), cc_ref(ic  , 4, k));
            auto ti3 = vsub(cc_ref(i  , 5, k), cc_ref(ic  , 4, k));
            auto tr5 = vsub(cc_ref(i-1, 3, k), cc_ref(ic-1, 2, k));
            auto tr2 = vadd(cc_ref(i-1, 3, k), cc_ref(ic-1, 2, k));
            auto tr4 = vsub(cc_ref(i-1, 5, k), cc_ref(ic-1, 4, k));
            auto tr3 = vadd(cc_ref(i-1, 5, k), cc_ref(ic-1, 4, k));
            ch_ref(i - 1, k, 1) = vadd(cc_ref(i-1, 1, k), vadd(tr2, tr3));
            ch_ref(i    , k, 1) = vadd(cc_ref(i  , 1, k), vadd(ti2, ti3));
            auto cr2 = vadd(cc_ref(i-1, 1, k), vmadd(tr11, tr2, vmul(tr12, tr3)));
            auto ci2 = vadd(cc_ref(i  , 1, k), vmadd(tr11, ti2, vmul(tr12, ti3)));
            auto cr3 = vadd(cc_ref(i-1, 1, k), vmadd(tr12, tr2, vmul(tr11, tr3)));
            auto ci3 = vadd(cc_ref(i  , 1, k), vmadd(tr12, ti2, vmul(tr11, ti3)));
            auto cr5 = vmadd(ti11, tr5, vmul(ti12, tr4));
            auto ci5 = vmadd(ti11, ti5, vmul(ti12, ti4));
            auto cr4 = vsub(vmul(ti12, tr5), vmul(ti11, tr4));
            auto ci4 = vsub(vmul(ti12, ti5), vmul(ti11, ti4));
            auto dr3 = vsub(cr3, ci4);
            auto dr4 = vadd(cr3, ci4);
            auto di3 = vadd(ci3, cr4);
            auto di4 = vsub(ci3, cr4);
            auto dr5 = vadd(cr2, ci5);
            auto dr2 = vsub(cr2, ci5);
            auto di5 = vsub(ci2, cr5);
            auto di2 = vadd(ci2, cr5);
            vcplxmul(dr2, di2, ld_ps1(wa1[i-3]), ld_ps1(wa1[i-2]));
            vcplxmul(dr3, di3, ld_ps1(wa2[i-3]), ld_ps1(wa2[i-2]));
            vcplxmul(dr4, di4, ld_ps1(wa3[i-3]), ld_ps1(wa3[i-2]));
            vcplxmul(dr5, di5, ld_ps1(wa4[i-3]), ld_ps1(wa4[i-2]));

            ch_ref(i-1, k, 2) = dr2; ch_ref(i, k, 2) = di2;
            ch_ref(i-1, k, 3) = dr3; ch_ref(i, k, 3) = di3;
            ch_ref(i-1, k, 4) = dr4; ch_ref(i, k, 4) = di4;
            ch_ref(i-1, k, 5) = dr5; ch_ref(i, k, 5) = di5;
        }
    }
} /* radb5 */

NOINLINE auto rfftf1_ps(const size_t n, const v4sf *input_readonly, v4sf *work1, v4sf *work2,
    const float *wa, const std::span<const uint,15> ifac) -> v4sf*
{
    Expects(work1 != work2);

    const auto *in = input_readonly;
    auto *out = in == work2 ? work1 : work2;
    const auto nf = size_t{ifac[1]};
    auto l2 = n;
    auto iw = n - 1_uz;
    auto k1 = 1_uz;
    while(true)
    {
        const auto kh = nf - k1;
        const auto ip = size_t{ifac[kh + 2]};
        const auto l1 = l2 / ip;
        const auto ido = n / l2;
        iw -= (ip - 1)*ido;
        switch(ip)
        {
        case 5: radf5_ps(ido, l1, in, out, &wa[iw]); break;
        case 4: radf4_ps(ido, l1, in, out, &wa[iw]); break;
        case 3: radf3_ps(ido, l1, in, out, &wa[iw]); break;
        case 2: radf2_ps(ido, l1, in, out, &wa[iw]); break;
        default: std::abort();
        }
        if(++k1 > nf)
            return out;

        l2 = l1;
        if(out == work2)
        {
            out = work1;
            in = work2;
        }
        else
        {
            out = work2;
            in = work1;
        }
    }
} /* rfftf1 */

NOINLINE v4sf *rfftb1_ps(const size_t n, const v4sf *input_readonly, v4sf *work1, v4sf *work2,
    const float *wa, const std::span<const uint,15> ifac)
{
    Expects(work1 != work2);

    const auto *in = input_readonly;
    auto *out = in == work2 ? work1 : work2;
    const auto nf = size_t{ifac[1]};
    auto l1 = 1_uz;
    auto iw = 0_uz;
    auto k1 = 1_uz;
    while(true)
    {
        const auto ip = size_t{ifac[k1 + 1]};
        const auto l2 = ip * l1;
        const auto ido = n / l2;
        switch(ip)
        {
        case 5: radb5_ps(ido, l1, in, out, &wa[iw]); break;
        case 4: radb4_ps(ido, l1, in, out, &wa[iw]); break;
        case 3: radb3_ps(ido, l1, in, out, &wa[iw]); break;
        case 2: radb2_ps(ido, l1, in, out, &wa[iw]); break;
        default: std::abort();
        }
        if(++k1 > nf)
            return out;

        l1 = l2;
        iw += (ip - 1)*ido;

        if(out == work2)
        {
            out = work1;
            in = work2;
        }
        else
        {
            out = work2;
            in = work1;
        }
    }
}

v4sf *cfftf1_ps(const size_t n, const v4sf *input_readonly, v4sf *work1, v4sf *work2,
    const float *wa, const std::span<const uint,15> ifac, const float fsign)
{
    Expects(work1 != work2);

    const auto *in = input_readonly;
    auto *out = in == work2 ? work1 : work2;
    const auto nf = size_t{ifac[1]};
    auto l1 = 1_uz;
    auto iw = 0_uz;
    auto k1 = 2_uz;
    while(true)
    {
        const auto ip = size_t{ifac[k1]};
        const auto l2 = ip * l1;
        const auto ido = n / l2;
        const auto idot = ido + ido;
        switch(ip)
        {
        case 5: passf5_ps(idot, l1, in, out, &wa[iw], fsign); break;
        case 4: passf4_ps(idot, l1, in, out, &wa[iw], fsign); break;
        case 3: passf3_ps(idot, l1, in, out, &wa[iw], fsign); break;
        case 2: passf2_ps(idot, l1, in, out, &wa[iw], fsign); break;
        default: std::abort();
        }
        if(++k1 > nf+1)
            return out;

        l1 = l2;
        iw += (ip - 1)*idot;
        if(out == work2)
        {
            out = work1;
            in = work2;
        }
        else
        {
            out = work2;
            in = work1;
        }
    }
}


auto decompose(const uint n, const std::span<uint,15> ifac, const std::span<const uint,4> ntryh)
    -> uint
{
    auto nl = n;
    auto nf = 0u;
    for(const auto ntry : ntryh)
    {
        while(nl != 1)
        {
            const auto nq = nl / ntry;
            const auto nr = nl % ntry;
            if(nr != 0) break;

            ifac[2 + nf++] = ntry;
            nl = nq;
            if(ntry == 2 && nf != 1)
            {
                for(auto i = 2_uz;i <= nf;++i)
                {
                    auto ib = nf - i + 2;
                    ifac[ib + 1] = ifac[ib];
                }
                ifac[2] = 2;
            }
        }
    }
    ifac[0] = n;
    ifac[1] = nf;
    return nf;
}

void rffti1_ps(const uint n, float *wa, const std::span<uint,15> ifac)
{
    static constexpr std::array ntryh{4u, 2u, 3u, 5u};

    const auto nf = size_t{decompose(n, ifac, ntryh)};
    const auto argh = 2.0*std::numbers::pi / n;
    auto is = 0_uz;
    auto nfm1 = nf - 1_uz;
    auto l1 = 1_uz;
    for(auto k1 = 0_uz;k1 < nfm1;++k1)
    {
        const auto ip = size_t{ifac[k1+2]};
        const auto l2 = l1 * ip;
        const auto ido = n / l2;
        const auto ipm = ip - 1;
        auto ld = 0_uz;
        for(auto j = 0_uz;j < ipm;++j)
        {
            auto i = is;
            ld += l1;
            const auto argld = gsl::narrow_cast<double>(ld)*argh;
            auto fi = 0.0;
            for(auto ii = 2_uz;ii < ido;ii += 2)
            {
                fi += 1.0;
                wa[i++] = gsl::narrow_cast<float>(std::cos(fi*argld));
                wa[i++] = gsl::narrow_cast<float>(std::sin(fi*argld));
            }
            is += ido;
        }
        l1 = l2;
    }
} /* rffti1 */

void cffti1_ps(const uint n, float *wa, const std::span<uint,15> ifac)
{
    static constexpr auto ntryh = std::array{5u, 3u, 4u, 2u};

    const auto nf = size_t{decompose(n, ifac, ntryh)};
    const auto argh = 2.0*std::numbers::pi / n;
    auto i = 1_uz;
    auto l1 = 1_uz;
    for(auto k1 = 0_uz;k1 < nf;++k1)
    {
        const auto ip = size_t{ifac[k1+2]};
        const auto l2 = l1 * ip;
        const auto ido = n / l2;
        const auto idot = ido + ido + 2_uz;
        const auto ipm = ip - 1_uz;
        auto ld = 0_uz;
        for(auto j = 0_uz;j < ipm;++j)
        {
            auto i1 = i;
            wa[i-1] = 1.0f;
            wa[i] = 0.0f;
            ld += l1;
            const auto argld = gsl::narrow_cast<double>(ld)*argh;
            auto fi = 0.0;
            for(auto ii = 3_uz;ii < idot;ii += 2)
            {
                fi += 1.0;
                wa[++i] = gsl::narrow_cast<float>(std::cos(fi*argld));
                wa[++i] = gsl::narrow_cast<float>(std::sin(fi*argld));
            }
            if(ip > 5)
            {
                wa[i1-1] = wa[i-1];
                wa[i1] = wa[i];
            }
        }
        l1 = l2;
    }
} /* cffti1 */

} // namespace


struct alignas(V4sfAlignment) PFFFT_Setup {
    uint N{};
    uint Ncvec{}; /* nb of complex simd vectors (N/4 if PFFFT_COMPLEX, N/8 if PFFFT_REAL) */
    std::array<uint,15> ifac{};
    pffft_transform_t transform{};

    float *twiddle{}; /* N/4 elements */
    DIAGNOSTIC_PUSH
    std_pragma("GCC diagnostic ignored \"-Wignored-attributes\"")
    std::span<v4sf> e; /* N/4*3 elements */
    DIAGNOSTIC_POP
};

auto pffft_new_setup(const unsigned int N, const pffft_transform_t transform) -> PFFFTSetupPtr
{
    Expects(transform == PFFFT_REAL || transform == PFFFT_COMPLEX);
    /* unfortunately, the fft size must be a multiple of 16 for complex FFTs
     * and 32 for real FFTs -- a lot of stuff would need to be rewritten to
     * handle other cases (or maybe just switch to a scalar fft, I don't know..)
     */
    if(transform == PFFFT_REAL)
    {
        Expects(N >= 2u*SimdSize*SimdSize);
        Expects((N%(2u*SimdSize*SimdSize)) == 0);
    }
    else
    {
        Expects(N >= SimdSize*SimdSize);
        Expects((N%(SimdSize*SimdSize)) == 0);
    }

    const auto Ncvec = uint{(transform == PFFFT_REAL ? N/2 : N) / SimdSize};
    Expects(Ncvec > 0u);

    const auto storelen = sizeof(PFFFT_Setup) + 2_zu*Ncvec*sizeof(v4sf);
    auto storage = static_cast<gsl::owner<std::byte*>>(::operator new(storelen, V4sfAlignVal));
    auto extrastore = std::span{&storage[sizeof(PFFFT_Setup)], 2_zu*Ncvec*sizeof(v4sf)};

    auto s = PFFFTSetupPtr{::new(storage) PFFFT_Setup{}};
    s->N = N;
    s->transform = transform;
    s->Ncvec = Ncvec;

    const auto ecount = 2_zu*Ncvec*(SimdSize-1)/SimdSize;
    /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) */
    s->e = {std::launder(reinterpret_cast<v4sf*>(extrastore.data())), ecount};
    s->twiddle = std::launder(reinterpret_cast<float*>(&extrastore[ecount*sizeof(v4sf)]));
    /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

#ifndef PFFFT_SIMD_DISABLE
    if constexpr(SimdSize > 1)
    {
        auto e = std::vector<float>(s->e.size()*SimdSize, 0.0f);
        for(auto k = 0_uz;k < s->Ncvec;++k)
        {
            const auto i = k / SimdSize;
            const auto j = k % SimdSize;
            for(auto m = 0_uz;m < SimdSize-1;++m)
            {
                const auto A = -2.0*std::numbers::pi*gsl::narrow_cast<double>((m+1)*k) / N;
                e[((i*3 + m)*2 + 0)*SimdSize + j] = gsl::narrow_cast<float>(std::cos(A));
                e[((i*3 + m)*2 + 1)*SimdSize + j] = gsl::narrow_cast<float>(std::sin(A));
            }
        }
        auto eiter = e.begin();
        std::ranges::generate(s->e, [&eiter]
        {
            const auto a = *(eiter++);
            const auto b = *(eiter++);
            const auto c = *(eiter++);
            const auto d = *(eiter++);
            return vset4(a, b, c, d);
        });
    }
#endif
    if(transform == PFFFT_REAL)
        rffti1_ps(N/SimdSize, s->twiddle, s->ifac);
    else
        cffti1_ps(N/SimdSize, s->twiddle, s->ifac);

    /* check that N is decomposable with allowed prime factors */
    auto m = 1_uz;
    for(auto k = 0_uz;k < s->ifac[1];++k)
        m *= s->ifac[2+k];

    if(m != N/SimdSize)
        s = nullptr;

    return s;
}


void PFFFTSetupDeleter::operator()(gsl::owner<PFFFT_Setup*> setup) const noexcept
{
    std::destroy_at(setup);
    ::operator delete(gsl::owner<void*>{setup}, V4sfAlignVal);
}

#if !defined(PFFFT_SIMD_DISABLE)

namespace {

/* [0 0 1 2 3 4 5 6 7 8] -> [0 8 7 6 5 4 3 2 1] */
void reversed_copy(const size_t N, const v4sf *in, const int in_stride, v4sf *RESTRICT out)
{
    auto g0 = v4sf{};
    auto g1 = v4sf{};
    interleave2(in[0], in[1], g0, g1);
    in += in_stride;

    *--out = vswaphl(g0, g1); // [g0l, g0h], [g1l g1h] -> [g1l, g0h]
    for(auto k = 1_uz;k < N;++k)
    {
        auto h0 = v4sf{};
        auto h1 = v4sf{};
        interleave2(in[0], in[1], h0, h1);
        in += in_stride;
        *--out = vswaphl(g1, h0);
        *--out = vswaphl(h0, h1);
        g1 = h1;
    }
    *--out = vswaphl(g1, g0);
}

void unreversed_copy(const size_t N, const v4sf *in, v4sf *RESTRICT out, const int out_stride)
{
    auto g0 = in[0];
    auto g1 = g0;
    ++in;
    for(auto k = 1_uz;k < N;++k)
    {
        auto h0 = *in++;
        auto h1 = *in++;
        g1 = vswaphl(g1, h0);
        h0 = vswaphl(h0, h1);
        uninterleave2(h0, g1, out[0], out[1]);
        out += out_stride;
        g1 = h1;
    }
    auto h0 = *in++;
    auto h1 = g0;
    g1 = vswaphl(g1, h0);
    h0 = vswaphl(h0, h1);
    uninterleave2(h0, g1, out[0], out[1]);
}

void pffft_cplx_finalize(const size_t Ncvec, const v4sf *in, v4sf *RESTRICT out, const v4sf *e)
{
    Expects(in != out);

    const auto dk = size_t{Ncvec/SimdSize}; // number of 4x4 matrix blocks
    for(auto k = 0_uz;k < dk;++k)
    {
        auto r0 = in[8*k + 0];
        auto i0 = in[8*k + 1];
        auto r1 = in[8*k + 2];
        auto i1 = in[8*k + 3];
        auto r2 = in[8*k + 4];
        auto i2 = in[8*k + 5];
        auto r3 = in[8*k + 6];
        auto i3 = in[8*k + 7];

        vtranspose4(r0, r1, r2, r3);
        vtranspose4(i0, i1, i2, i3);
        vcplxmul(r1, i1, e[k*6 + 0], e[k*6 + 1]);
        vcplxmul(r2, i2, e[k*6 + 2], e[k*6 + 3]);
        vcplxmul(r3, i3, e[k*6 + 4], e[k*6 + 5]);

        auto sr0 = vadd(r0, r2);
        auto dr0 = vsub(r0, r2);
        auto sr1 = vadd(r1, r3);
        auto dr1 = vsub(r1, r3);
        auto si0 = vadd(i0, i2);
        auto di0 = vsub(i0, i2);
        auto si1 = vadd(i1, i3);
        auto di1 = vsub(i1, i3);

        /* transformation for each column is:
         *
         * [1   1   1   1   0   0   0   0]   [r0]
         * [1   0  -1   0   0  -1   0   1]   [r1]
         * [1  -1   1  -1   0   0   0   0]   [r2]
         * [1   0  -1   0   0   1   0  -1]   [r3]
         * [0   0   0   0   1   1   1   1] * [i0]
         * [0   1   0  -1   1   0  -1   0]   [i1]
         * [0   0   0   0   1  -1   1  -1]   [i2]
         * [0  -1   0   1   1   0  -1   0]   [i3]
         */

        r0 = vadd(sr0, sr1);
        i0 = vadd(si0, si1);
        r1 = vadd(dr0, di1);
        i1 = vsub(di0, dr1);
        r2 = vsub(sr0, sr1);
        i2 = vsub(si0, si1);
        r3 = vsub(dr0, di1);
        i3 = vadd(di0, dr1);

        *out++ = r0;
        *out++ = i0;
        *out++ = r1;
        *out++ = i1;
        *out++ = r2;
        *out++ = i2;
        *out++ = r3;
        *out++ = i3;
    }
}

void pffft_cplx_preprocess(const size_t Ncvec, const v4sf *in, v4sf *RESTRICT out, const v4sf *e)
{
    Expects(in != out);

    const auto dk = size_t{Ncvec/SimdSize}; // number of 4x4 matrix blocks
    for(size_t k{0};k < dk;++k)
    {
        auto r0 = in[8*k + 0];
        auto i0 = in[8*k + 1];
        auto r1 = in[8*k + 2];
        auto i1 = in[8*k + 3];
        auto r2 = in[8*k + 4];
        auto i2 = in[8*k + 5];
        auto r3 = in[8*k + 6];
        auto i3 = in[8*k + 7];

        auto sr0 = vadd(r0, r2);
        auto dr0 = vsub(r0, r2);
        auto sr1 = vadd(r1, r3);
        auto dr1 = vsub(r1, r3);
        auto si0 = vadd(i0, i2);
        auto di0 = vsub(i0, i2);
        auto si1 = vadd(i1, i3);
        auto di1 = vsub(i1, i3);

        r0 = vadd(sr0, sr1);
        i0 = vadd(si0, si1);
        r1 = vsub(dr0, di1);
        i1 = vadd(di0, dr1);
        r2 = vsub(sr0, sr1);
        i2 = vsub(si0, si1);
        r3 = vadd(dr0, di1);
        i3 = vsub(di0, dr1);

        vcplxmulconj(r1, i1, e[k*6 + 0], e[k*6 + 1]);
        vcplxmulconj(r2, i2, e[k*6 + 2], e[k*6 + 3]);
        vcplxmulconj(r3, i3, e[k*6 + 4], e[k*6 + 5]);

        vtranspose4(r0, r1, r2, r3);
        vtranspose4(i0, i1, i2, i3);

        *out++ = r0;
        *out++ = i0;
        *out++ = r1;
        *out++ = i1;
        *out++ = r2;
        *out++ = i2;
        *out++ = r3;
        *out++ = i3;
    }
}


force_inline void pffft_real_finalize_4x4(const v4sf *in0, const v4sf *in1, const v4sf *in,
    const v4sf *e, v4sf *RESTRICT out)
{
    auto r0 = *in0;
    auto i0 = *in1;
    auto r1 = *in++;
    auto i1 = *in++;
    auto r2 = *in++;
    auto i2 = *in++;
    auto r3 = *in++;
    auto i3 = *in++;
    vtranspose4(r0, r1, r2, r3);
    vtranspose4(i0, i1, i2, i3);

    /* transformation for each column is:
     *
     * [1   1   1   1   0   0   0   0]   [r0]
     * [1   0  -1   0   0  -1   0   1]   [r1]
     * [1   0  -1   0   0   1   0  -1]   [r2]
     * [1  -1   1  -1   0   0   0   0]   [r3]
     * [0   0   0   0   1   1   1   1] * [i0]
     * [0  -1   0   1  -1   0   1   0]   [i1]
     * [0  -1   0   1   1   0  -1   0]   [i2]
     * [0   0   0   0  -1   1  -1   1]   [i3]
     */

    //cerr << "matrix initial, before e , REAL:\n 1: " << r0 << "\n 1: " << r1 << "\n 1: " << r2 << "\n 1: " << r3 << "\n";
    //cerr << "matrix initial, before e, IMAG :\n 1: " << i0 << "\n 1: " << i1 << "\n 1: " << i2 << "\n 1: " << i3 << "\n";

    vcplxmul(r1, i1, e[0], e[1]);
    vcplxmul(r2, i2, e[2], e[3]);
    vcplxmul(r3, i3, e[4], e[5]);

    //cerr << "matrix initial, real part:\n 1: " << r0 << "\n 1: " << r1 << "\n 1: " << r2 << "\n 1: " << r3 << "\n";
    //cerr << "matrix initial, imag part:\n 1: " << i0 << "\n 1: " << i1 << "\n 1: " << i2 << "\n 1: " << i3 << "\n";

    auto sr0 = vadd(r0, r2);
    auto dr0 = vsub(r0, r2);
    auto sr1 = vadd(r1, r3);
    auto dr1 = vsub(r3, r1);
    auto si0 = vadd(i0, i2);
    auto di0 = vsub(i0, i2);
    auto si1 = vadd(i1, i3);
    auto di1 = vsub(i3, i1);

    r0 = vadd(sr0, sr1);
    r3 = vsub(sr0, sr1);
    i0 = vadd(si0, si1);
    i3 = vsub(si1, si0);
    r1 = vadd(dr0, di1);
    r2 = vsub(dr0, di1);
    i1 = vsub(dr1, di0);
    i2 = vadd(dr1, di0);

    *out++ = r0;
    *out++ = i0;
    *out++ = r1;
    *out++ = i1;
    *out++ = r2;
    *out++ = i2;
    *out++ = r3;
    *out++ = i3;
}

NOINLINE void pffft_real_finalize(const size_t Ncvec, const v4sf *in, v4sf *RESTRICT out,
    const v4sf *e)
{
    static constexpr auto s = std::numbers::sqrt2_v<float>/2.0f;

    Expects(in != out);
    const auto dk = size_t{Ncvec/SimdSize}; // number of 4x4 matrix blocks
    /* fftpack order is f0r f1r f1i f2r f2i ... f(n-1)r f(n-1)i f(n)r */

    const auto zero = vzero();
    const auto cr = std::bit_cast<std::array<float,SimdSize>>(in[0]);
    const auto ci = std::bit_cast<std::array<float,SimdSize>>(in[Ncvec*2-1]);
    pffft_real_finalize_4x4(&zero, &zero, in+1, e, out);

    /* [cr0 cr1 cr2 cr3 ci0 ci1 ci2 ci3]
     *
     * [Xr(1)]  ] [1   1   1   1   0   0   0   0]
     * [Xr(N/4) ] [0   0   0   0   1   s   0  -s]
     * [Xr(N/2) ] [1   0  -1   0   0   0   0   0]
     * [Xr(3N/4)] [0   0   0   0   1  -s   0   s]
     * [Xi(1)   ] [1  -1   1  -1   0   0   0   0]
     * [Xi(N/4) ] [0   0   0   0   0  -s  -1  -s]
     * [Xi(N/2) ] [0  -1   0   1   0   0   0   0]
     * [Xi(3N/4)] [0   0   0   0   0  -s   1  -s]
     */

    out[0] = vinsert0(out[0], (cr[0]+cr[2]) +   (cr[1]+cr[3]));
    out[1] = vinsert0(out[1], (cr[0]+cr[2]) -   (cr[1]+cr[3]));
    out[4] = vinsert0(out[4], (cr[0]-cr[2]));
    out[5] = vinsert0(out[5], (cr[3]-cr[1]));
    out[2] = vinsert0(out[2],  ci[0]        + s*(ci[1]-ci[3]));
    out[3] = vinsert0(out[3],       -ci[2]  - s*(ci[1]+ci[3]));
    out[6] = vinsert0(out[6],  ci[0]        - s*(ci[1]-ci[3]));
    out[7] = vinsert0(out[7],        ci[2]  - s*(ci[1]+ci[3]));

    for(auto k = 1_uz;k < dk;++k)
        pffft_real_finalize_4x4(&in[8*k-1], &in[8*k+0], in + 8*k+1, e + k*6, out + k*8);
}

force_inline void pffft_real_preprocess_4x4(const v4sf *in, const v4sf *e, v4sf *RESTRICT out,
    const bool first)
{
    auto r0 = in[0];
    auto i0 = in[1];
    auto r1 = in[2];
    auto i1 = in[3];
    auto r2 = in[4];
    auto i2 = in[5];
    auto r3 = in[6];
    auto i3 = in[7];

    /* transformation for each column is:
     *
     * [1   1   1   1   0   0   0   0]   [r0]
     * [1   0   0  -1   0  -1  -1   0]   [r1]
     * [1  -1  -1   1   0   0   0   0]   [r2]
     * [1   0   0  -1   0   1   1   0]   [r3]
     * [0   0   0   0   1  -1   1  -1] * [i0]
     * [0  -1   1   0   1   0   0   1]   [i1]
     * [0   0   0   0   1   1  -1  -1]   [i2]
     * [0   1  -1   0   1   0   0   1]   [i3]
     */

    auto sr0 = vadd(r0, r3);
    auto dr0 = vsub(r0, r3);
    auto sr1 = vadd(r1, r2);
    auto dr1 = vsub(r1, r2);
    auto si0 = vadd(i0, i3);
    auto di0 = vsub(i0, i3);
    auto si1 = vadd(i1, i2);
    auto di1 = vsub(i1, i2);

    r0 = vadd(sr0, sr1);
    r2 = vsub(sr0, sr1);
    r1 = vsub(dr0, si1);
    r3 = vadd(dr0, si1);
    i0 = vsub(di0, di1);
    i2 = vadd(di0, di1);
    i1 = vsub(si0, dr1);
    i3 = vadd(si0, dr1);

    vcplxmulconj(r1, i1, e[0], e[1]);
    vcplxmulconj(r2, i2, e[2], e[3]);
    vcplxmulconj(r3, i3, e[4], e[5]);

    vtranspose4(r0, r1, r2, r3);
    vtranspose4(i0, i1, i2, i3);

    if(!first)
    {
        *out++ = r0;
        *out++ = i0;
    }
    *out++ = r1;
    *out++ = i1;
    *out++ = r2;
    *out++ = i2;
    *out++ = r3;
    *out++ = i3;
}

NOINLINE void pffft_real_preprocess(const size_t Ncvec, const v4sf *in, v4sf *RESTRICT out,
    const v4sf *e)
{
    static constexpr auto sqrt2 = std::numbers::sqrt2_v<float>;

    Expects(in != out);
    const auto dk = size_t{Ncvec/SimdSize}; // number of 4x4 matrix blocks
    /* fftpack order is f0r f1r f1i f2r f2i ... f(n-1)r f(n-1)i f(n)r */

    auto Xr = std::array<float,SimdSize>{};
    auto Xi = std::array<float,SimdSize>{};
    for(auto k = 0_uz;k < SimdSize;++k)
    {
        Xr[k] = vextract0(in[2*k]);
        Xi[k] = vextract0(in[2*k + 1]);
    }

    pffft_real_preprocess_4x4(in, e, out+1, true); // will write only 6 values

    /* [Xr0 Xr1 Xr2 Xr3 Xi0 Xi1 Xi2 Xi3]
     *
     * [cr0] [1   0   2   0   1   0   0   0]
     * [cr1] [1   0   0   0  -1   0  -2   0]
     * [cr2] [1   0  -2   0   1   0   0   0]
     * [cr3] [1   0   0   0  -1   0   2   0]
     * [ci0] [0   2   0   2   0   0   0   0]
     * [ci1] [0   s   0  -s   0  -s   0  -s]
     * [ci2] [0   0   0   0   0  -2   0   2]
     * [ci3] [0  -s   0   s   0  -s   0  -s]
     */
    for(auto k = 1_uz;k < dk;++k)
        pffft_real_preprocess_4x4(in+8*k, e + k*6, out-1+k*8, false);

    const auto cr0 = (Xr[0]+Xi[0]) + 2*Xr[2];
    const auto cr1 = (Xr[0]-Xi[0]) - 2*Xi[2];
    const auto cr2 = (Xr[0]+Xi[0]) - 2*Xr[2];
    const auto cr3 = (Xr[0]-Xi[0]) + 2*Xi[2];
    out[0] = vset4(cr0, cr1, cr2, cr3);
    const auto ci0 =      2*(Xr[1]+Xr[3]);
    const auto ci1 =  sqrt2*(Xr[1]-Xr[3]) - sqrt2*(Xi[1]+Xi[3]);
    const auto ci2 =      2*(Xi[3]-Xi[1]);
    const auto ci3 = -sqrt2*(Xr[1]-Xr[3]) - sqrt2*(Xi[1]+Xi[3]);
    out[2*Ncvec-1] = vset4(ci0, ci1, ci2, ci3);
}


void pffft_zreorder_internal(const PFFFT_Setup *setup, const v4sf *vin, v4sf *RESTRICT vout,
    pffft_direction_t direction)
{
    const auto N = size_t{setup->N};
    const auto Ncvec = size_t{setup->Ncvec};
    if(setup->transform == PFFFT_REAL)
    {
        const auto dk = N/32;
        if(direction == PFFFT_FORWARD)
        {
            for(auto k = 0_uz;k < dk;++k)
            {
                interleave2(vin[k*8 + 0],vin[k*8 + 1], vout[2*(0*dk + k)],vout[2*(0*dk + k) + 1]);
                interleave2(vin[k*8 + 4],vin[k*8 + 5], vout[2*(2*dk + k)],vout[2*(2*dk + k) + 1]);
            }
            reversed_copy(dk, vin+2, 8, vout + N/SimdSize/2);
            reversed_copy(dk, vin+6, 8, vout + N/SimdSize);
        }
        else
        {
            for(auto k = 0_uz;k < dk;++k)
            {
                uninterleave2(vin[2*(0*dk + k)],vin[2*(0*dk + k) + 1],vout[k*8 + 0],vout[k*8 + 1]);
                uninterleave2(vin[2*(2*dk + k)],vin[2*(2*dk + k) + 1],vout[k*8 + 4],vout[k*8 + 5]);
            }
            unreversed_copy(dk, vin + N/SimdSize/4, vout + N/SimdSize - 6, -8);
            unreversed_copy(dk, vin + 3_uz*N/SimdSize/4, vout + N/SimdSize - 2, -8);
        }
    }
    else
    {
        if(direction == PFFFT_FORWARD)
        {
            for(auto k = 0_uz;k < Ncvec;++k)
            {
                const auto kk = (k/4) + (k%4)*(Ncvec/4);
                interleave2(vin[k*2], vin[k*2+1], vout[kk*2], vout[kk*2+1]);
            }
        }
        else
        {
            for(auto k = 0_uz;k < Ncvec;++k)
            {
                const auto kk = (k/4) + (k%4)*(Ncvec/4);
                uninterleave2(vin[kk*2], vin[kk*2+1], vout[k*2], vout[k*2+1]);
            }
        }
    }
}

void pffft_transform_internal(const PFFFT_Setup *setup, const v4sf *vinput, v4sf *voutput,
    v4sf *scratch, const pffft_direction_t direction, const bool ordered)
{
    Expects(scratch != nullptr);
    Expects(voutput != scratch);

    const auto Ncvec = size_t{setup->Ncvec};
    const auto nf_odd = (setup->ifac[1]&1) != 0;

    auto buff = std::array{voutput, scratch};
    auto ib = nf_odd != ordered;
    if(direction == PFFFT_FORWARD)
    {
        /* Swap the initial work buffer for forward FFTs, which helps avoid an
         * extra copy for output.
         */
        ib = !ib;
        if(setup->transform == PFFFT_REAL)
        {
            ib = (rfftf1_ps(Ncvec*2, vinput, buff[ib], buff[!ib], setup->twiddle, setup->ifac) == buff[1]);
            pffft_real_finalize(Ncvec, buff[ib], buff[!ib], setup->e.data());
        }
        else
        {
            auto *tmp = buff[ib];
            for(auto k = 0_uz;k < Ncvec;++k)
                uninterleave2(vinput[k*2], vinput[k*2+1], tmp[k*2], tmp[k*2+1]);

            ib = (cfftf1_ps(Ncvec, buff[ib], buff[!ib], buff[ib], setup->twiddle, setup->ifac, -1.0f) == buff[1]);
            pffft_cplx_finalize(Ncvec, buff[ib], buff[!ib], setup->e.data());
        }
        if(ordered)
            pffft_zreorder_internal(setup, buff[!ib], buff[ib], PFFFT_FORWARD);
        else
            ib = !ib;
    }
    else
    {
        if(vinput == buff[ib])
            ib = !ib; // may happen when finput == foutput

        if(ordered)
        {
            pffft_zreorder_internal(setup, vinput, buff[ib], PFFFT_BACKWARD);
            vinput = buff[ib];
            ib = !ib;
        }
        if(setup->transform == PFFFT_REAL)
        {
            pffft_real_preprocess(Ncvec, vinput, buff[ib], setup->e.data());
            ib = (rfftb1_ps(Ncvec*2, buff[ib], buff[0], buff[1], setup->twiddle, setup->ifac) == buff[1]);
        }
        else
        {
            pffft_cplx_preprocess(Ncvec, vinput, buff[ib], setup->e.data());
            ib = (cfftf1_ps(Ncvec, buff[ib], buff[0], buff[1],  setup->twiddle, setup->ifac, +1.0f) == buff[1]);
            for(auto k = 0_uz;k < Ncvec;++k)
                interleave2(buff[ib][k*2], buff[ib][k*2+1], buff[ib][k*2], buff[ib][k*2+1]);
        }
    }

    if(buff[ib] != voutput)
    {
        /* extra copy required -- this situation should only happen when finput == foutput */
        Expects(vinput == voutput);
        for(auto k = 0_uz;k < Ncvec;++k)
        {
            const auto a = buff[ib][2*k];
            const auto b = buff[ib][2*k+1];
            voutput[2*k] = a;
            voutput[2*k+1] = b;
        }
    }
}

void pffft_zconvolve_scale_accumulate_internal(const PFFFT_Setup *s, const v4sf *RESTRICT va,
    const v4sf *RESTRICT vb, v4sf *RESTRICT vab, float scaling)
{
    const auto Ncvec = size_t{s->Ncvec};

#ifdef __arm__
    __builtin_prefetch(va);
    __builtin_prefetch(vb);
    __builtin_prefetch(vab);
    __builtin_prefetch(va+2);
    __builtin_prefetch(vb+2);
    __builtin_prefetch(vab+2);
    __builtin_prefetch(va+4);
    __builtin_prefetch(vb+4);
    __builtin_prefetch(vab+4);
    __builtin_prefetch(va+6);
    __builtin_prefetch(vb+6);
    __builtin_prefetch(vab+6);
#endif

    const auto ar1 = vextract0(va[0]);
    const auto ai1 = vextract0(va[1]);
    const auto br1 = vextract0(vb[0]);
    const auto bi1 = vextract0(vb[1]);
    const auto abr1 = vextract0(vab[0]);
    const auto abi1 = vextract0(vab[1]);

    const auto vscale = ld_ps1(scaling);
    for(auto i = 0_uz;i < Ncvec;i += 2)
    {
        auto ar4 = va[2*i + 0];
        auto ai4 = va[2*i + 1];
        auto br4 = vb[2*i + 0];
        auto bi4 = vb[2*i + 1];
        vcplxmul(ar4, ai4, br4, bi4);
        vab[2*i + 0] = vmadd(ar4, vscale, vab[2*i + 0]);
        vab[2*i + 1] = vmadd(ai4, vscale, vab[2*i + 1]);

        ar4 = va[2*i + 2];
        ai4 = va[2*i + 3];
        br4 = vb[2*i + 2];
        bi4 = vb[2*i + 3];
        vcplxmul(ar4, ai4, br4, bi4);
        vab[2*i + 2] = vmadd(ar4, vscale, vab[2*i + 2]);
        vab[2*i + 3] = vmadd(ai4, vscale, vab[2*i + 3]);
    }

    if(s->transform == PFFFT_REAL)
    {
        vab[0] = vinsert0(vab[0], abr1 + ar1*br1*scaling);
        vab[1] = vinsert0(vab[1], abi1 + ai1*bi1*scaling);
    }
}

void pffft_zconvolve_accumulate_internal(const PFFFT_Setup *s, const v4sf *RESTRICT va,
    const v4sf *RESTRICT vb, v4sf *RESTRICT vab)
{
    const auto Ncvec = size_t{s->Ncvec};

#ifdef __arm__
    __builtin_prefetch(va);
    __builtin_prefetch(vb);
    __builtin_prefetch(vab);
    __builtin_prefetch(va+2);
    __builtin_prefetch(vb+2);
    __builtin_prefetch(vab+2);
    __builtin_prefetch(va+4);
    __builtin_prefetch(vb+4);
    __builtin_prefetch(vab+4);
    __builtin_prefetch(va+6);
    __builtin_prefetch(vb+6);
    __builtin_prefetch(vab+6);
#endif

    const auto ar1 = vextract0(va[0]);
    const auto ai1 = vextract0(va[1]);
    const auto br1 = vextract0(vb[0]);
    const auto bi1 = vextract0(vb[1]);
    const auto abr1 = vextract0(vab[0]);
    const auto abi1 = vextract0(vab[1]);

    for(auto i = 0_uz;i < Ncvec;i += 2)
    {
        auto ar4 = va[2*i + 0];
        auto ai4 = va[2*i + 1];
        auto br4 = vb[2*i + 0];
        auto bi4 = vb[2*i + 1];
        vcplxmul(ar4, ai4, br4, bi4);
        vab[2*i + 0] = vadd(ar4, vab[2*i + 0]);
        vab[2*i + 1] = vadd(ai4, vab[2*i + 1]);

        ar4 = va[2*i + 2];
        ai4 = va[2*i + 3];
        br4 = vb[2*i + 2];
        bi4 = vb[2*i + 3];
        vcplxmul(ar4, ai4, br4, bi4);
        vab[2*i + 2] = vadd(ar4, vab[2*i + 2]);
        vab[2*i + 3] = vadd(ai4, vab[2*i + 3]);
    }

    if(s->transform == PFFFT_REAL)
    {
        vab[0] = vinsert0(vab[0], abr1 + ar1*br1);
        vab[1] = vinsert0(vab[1], abi1 + ai1*bi1);
    }
}

} // namespace

/* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
 * Without some way to "safely" reinterpret float buffers as SIMD float
 * vectors, these casts are needed.
 */
void pffft_zreorder(const PFFFT_Setup *setup, const float *in, float *out,
    pffft_direction_t direction)
{
    Expects(in != out);
    Expects(valigned(in) && valigned(out));
    pffft_zreorder_internal(setup, reinterpret_cast<const v4sf*>(in), reinterpret_cast<v4sf*>(out),
        direction);
}

void pffft_zconvolve_scale_accumulate(const PFFFT_Setup *s, const float *a, const float *b,
    float *ab, float scaling)
{
    Expects(valigned(a) && valigned(b) && valigned(ab));
    pffft_zconvolve_scale_accumulate_internal(s, reinterpret_cast<const v4sf*>(a),
        reinterpret_cast<const v4sf*>(b), reinterpret_cast<v4sf*>(ab), scaling);
}

void pffft_zconvolve_accumulate(const PFFFT_Setup *s, const float *a, const float *b, float *ab)
{
    Expects(valigned(a) && valigned(b) && valigned(ab));
    pffft_zconvolve_accumulate_internal(s, reinterpret_cast<const v4sf*>(a),
        reinterpret_cast<const v4sf*>(b), reinterpret_cast<v4sf*>(ab));
}

void pffft_transform(const PFFFT_Setup *setup, const float *input, float *output, float *work,
    pffft_direction_t direction)
{
    Expects(valigned(input) && valigned(output) && valigned(work));
    pffft_transform_internal(setup, reinterpret_cast<const v4sf*>(std::assume_aligned<16>(input)),
        reinterpret_cast<v4sf*>(std::assume_aligned<16>(output)),
        reinterpret_cast<v4sf*>(std::assume_aligned<16>(work)), direction, false);
}

void pffft_transform_ordered(const PFFFT_Setup *setup, const float *input, float *output,
    float *work, pffft_direction_t direction)
{
    Expects(valigned(input) && valigned(output) && valigned(work));
    pffft_transform_internal(setup, reinterpret_cast<const v4sf*>(std::assume_aligned<16>(input)),
        reinterpret_cast<v4sf*>(std::assume_aligned<16>(output)),
        reinterpret_cast<v4sf*>(std::assume_aligned<16>(work)), direction, true);
}
/* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

#else // defined(PFFFT_SIMD_DISABLE)

// standard routine using scalar floats, without SIMD stuff.

namespace {

void pffft_transform_internal(const PFFFT_Setup *setup, const float *input, float *output,
    float *scratch, const pffft_direction_t direction, bool ordered)
{
    const auto Ncvec = size_t{setup->Ncvec};
    const auto nf_odd = (setup->ifac[1]&1) != 0;

    Expects(scratch != nullptr);

    /* z-domain data for complex transforms is already ordered without SIMD. */
    if(setup->transform == PFFFT_COMPLEX)
        ordered = false;

    const auto buff = std::array{output, scratch};
    auto ib = nf_odd != ordered;
    if(direction == PFFFT_FORWARD)
    {
        if(setup->transform == PFFFT_REAL)
            ib = (rfftf1_ps(Ncvec*2, input, buff[ib], buff[!ib], setup->twiddle, setup->ifac) == buff[1]);
        else
            ib = (cfftf1_ps(Ncvec, input, buff[ib], buff[!ib], setup->twiddle, setup->ifac, -1.0f) == buff[1]);
        if(ordered)
        {
            pffft_zreorder(setup, buff[ib], buff[!ib], PFFFT_FORWARD);
            ib = !ib;
        }
    }
    else
    {
        if(input == buff[ib])
            ib = !ib; // may happen when finput == foutput

        if(ordered)
        {
            pffft_zreorder(setup, input, buff[ib], PFFFT_BACKWARD);
            input = buff[ib];
            ib = !ib;
        }
        if(setup->transform == PFFFT_REAL)
            ib = (rfftb1_ps(Ncvec*2, input, buff[ib], buff[!ib],  setup->twiddle, setup->ifac) == buff[1]);
        else
            ib = (cfftf1_ps(Ncvec, input, buff[ib], buff[!ib], setup->twiddle, setup->ifac, +1.0f) == buff[1]);
    }
    if(buff[ib] != output)
    {
        // extra copy required -- this situation should happens only when finput == foutput
        Expects(input == output);
        std::ranges::copy(std::span{buff[ib], Ncvec*2_uz}, output);
    }
}

} // namespace

void pffft_zreorder(const PFFFT_Setup *setup, const float *in, float *RESTRICT out,
    pffft_direction_t direction)
{
    const auto N = size_t{setup->N};
    if(setup->transform == PFFFT_COMPLEX)
    {
        for(auto k = 0_uz;k < 2*N;++k)
            out[k] = in[k];
    }
    else if(direction == PFFFT_FORWARD)
    {
        const auto x_N = in[N-1];
        for(auto k = N-1_uz;k > 1;--k)
            out[k] = in[k-1];
        out[0] = in[0];
        out[1] = x_N;
    }
    else
    {
        const auto x_N = in[1];
        for(auto k = 1_uz;k < N-1;++k)
            out[k] = in[k+1];
        out[0] = in[0];
        out[N-1] = x_N;
    }
}

void pffft_zconvolve_scale_accumulate(const PFFFT_Setup *s, const float *a, const float *b,
    float *ab, float scaling)
{
    auto Ncvec = size_t{s->Ncvec};

    if(s->transform == PFFFT_REAL)
    {
        // take care of the fftpack ordering
        ab[0] += a[0]*b[0]*scaling;
        ab[2*Ncvec-1] += a[2*Ncvec-1]*b[2*Ncvec-1]*scaling;
        ++ab; ++a; ++b; --Ncvec;
    }
    for(auto i = 0_uz;i < Ncvec;++i)
    {
        auto ar = a[2*i+0];
        auto ai = a[2*i+1];
        const auto br = b[2*i+0];
        const auto bi = b[2*i+1];
        vcplxmul(ar, ai, br, bi);
        ab[2*i+0] += ar*scaling;
        ab[2*i+1] += ai*scaling;
    }
}

void pffft_zconvolve_accumulate(const PFFFT_Setup *s, const float *a, const float *b, float *ab)
{
    auto Ncvec = size_t{s->Ncvec};

    if(s->transform == PFFFT_REAL)
    {
        // take care of the fftpack ordering
        ab[0] += a[0]*b[0];
        ab[2*Ncvec-1] += a[2*Ncvec-1]*b[2*Ncvec-1];
        ++ab; ++a; ++b; --Ncvec;
    }
    for(auto i = 0_uz;i < Ncvec;++i)
    {
        auto ar = a[2*i+0];
        auto ai = a[2*i+1];
        const auto br = b[2*i+0];
        const auto bi = b[2*i+1];
        vcplxmul(ar, ai, br, bi);
        ab[2*i+0] += ar;
        ab[2*i+1] += ai;
    }
}


void pffft_transform(const PFFFT_Setup *setup, const float *input, float *output, float *work,
    pffft_direction_t direction)
{
    pffft_transform_internal(setup, input, output, work, direction, false);
}

void pffft_transform_ordered(const PFFFT_Setup *setup, const float *input, float *output,
    float *work, pffft_direction_t direction)
{
    pffft_transform_internal(setup, input, output, work, direction, true);
}

#endif /* defined(PFFFT_SIMD_DISABLE) */
/* NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
