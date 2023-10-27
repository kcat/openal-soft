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

#include <array>
#include <assert.h>
#include <cmath>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "albit.h"
#include "almalloc.h"
#include "alnumbers.h"
#include "alspan.h"
#include "opthelpers.h"


namespace {

using uint = unsigned int;


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
#if defined(__ppc__) || defined(__ppc64__) || defined(__powerpc__) || defined(__powerpc64__)
typedef vector float v4sf;
#define SIMD_SZ 4
#define VZERO() ((vector float) vec_splat_u8(0))
#define VMUL(a,b) vec_madd(a,b, VZERO())
#define VADD vec_add
#define VMADD vec_madd
#define VSUB vec_sub
#define LD_PS1 vec_splats
force_inline v4sf vset4(float a, float b, float c, float d) noexcept
{
    /* There a more efficient way to do this? */
    alignas(16) std::array<float,4> vals{{a, b, c, d}};
    return vec_ld(0, vals.data());
}
#define VSET4 vset4
#define VINSERT0(v, a) vec_insert((a), (v), 0)
#define VEXTRACT0(v) vec_extract((v), 0)

force_inline void interleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    v4sf tmp{vec_mergeh(in1, in2)};
    out2 = vec_mergel(in1, in2);
    out1 = tmp;
}
force_inline void uninterleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    v4sf tmp{vec_perm(in1, in2, (vector unsigned char)(0,1,2,3,8,9,10,11,16,17,18,19,24,25,26,27))};
    out2 = vec_perm(in1, in2, (vector unsigned char)(4,5,6,7,12,13,14,15,20,21,22,23,28,29,30,31));
    out1 = tmp;
}

force_inline void vtranspose4(v4sf &x0, v4sf &x1, v4sf &x2, v4sf &x3) noexcept
{
    v4sf y0{vec_mergeh(x0, x2)};
    v4sf y1{vec_mergel(x0, x2)};
    v4sf y2{vec_mergeh(x1, x3)};
    v4sf y3{vec_mergel(x1, x3)};
    x0 = vec_mergeh(y0, y2);
    x1 = vec_mergel(y0, y2);
    x2 = vec_mergeh(y1, y3);
    x3 = vec_mergel(y1, y3);
}

#define VSWAPHL(a,b) vec_perm(a,b, (vector unsigned char)(16,17,18,19,20,21,22,23,8,9,10,11,12,13,14,15))

/*
 * SSE1 support macros
 */
#elif defined(__x86_64__) || defined(__SSE__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 1)

#include <xmmintrin.h>
typedef __m128 v4sf;
#define SIMD_SZ 4 // 4 floats by simd vector -- this is pretty much hardcoded in the preprocess/finalize functions anyway so you will have to work if you want to enable AVX with its 256-bit vectors.
#define VZERO _mm_setzero_ps
#define VMUL _mm_mul_ps
#define VADD _mm_add_ps
#define VMADD(a,b,c) _mm_add_ps(_mm_mul_ps(a,b), c)
#define VSUB _mm_sub_ps
#define LD_PS1 _mm_set1_ps
#define VSET4 _mm_setr_ps
#define VINSERT0(v, a) _mm_move_ss((v), _mm_set_ss(a))
#define VEXTRACT0 _mm_cvtss_f32

force_inline void interleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    v4sf tmp{_mm_unpacklo_ps(in1, in2)};
    out2 = _mm_unpackhi_ps(in1, in2);
    out1 = tmp;
}
force_inline void uninterleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    v4sf tmp{_mm_shuffle_ps(in1, in2, _MM_SHUFFLE(2,0,2,0))};
    out2 = _mm_shuffle_ps(in1, in2, _MM_SHUFFLE(3,1,3,1));
    out1 = tmp;
}

force_inline void vtranspose4(v4sf &x0, v4sf &x1, v4sf &x2, v4sf &x3) noexcept
{ _MM_TRANSPOSE4_PS(x0, x1, x2, x3); }

#define VSWAPHL(a,b) _mm_shuffle_ps(b, a, _MM_SHUFFLE(3,2,1,0))

/*
 * ARM NEON support macros
 */
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(__arm64)

#include <arm_neon.h>
typedef float32x4_t v4sf;
#define SIMD_SZ 4
#define VZERO() vdupq_n_f32(0)
#define VMUL vmulq_f32
#define VADD vaddq_f32
#define VMADD(a,b,c) vmlaq_f32(c,a,b)
#define VSUB vsubq_f32
#define LD_PS1 vdupq_n_f32
force_inline v4sf vset4(float a, float b, float c, float d) noexcept
{
    float32x4_t ret{vmovq_n_f32(a)};
    ret = vsetq_lane_f32(b, ret, 1);
    ret = vsetq_lane_f32(c, ret, 2);
    ret = vsetq_lane_f32(d, ret, 3);
    return ret;
}
#define VSET4 vset4
#define VINSERT0(v, a) vsetq_lane_f32((a), (v), 0)
#define VEXTRACT0(v) vgetq_lane_f32((v), 0)

force_inline void interleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    float32x4x2_t tmp{vzipq_f32(in1, in2)};
    out1 = tmp.val[0];
    out2 = tmp.val[1];
}
force_inline void uninterleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    float32x4x2_t tmp{vuzpq_f32(in1, in2)};
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
    float32x4x2_t t0_{vzipq_f32(x0, x2)};
    float32x4x2_t t1_{vzipq_f32(x1, x3)};
    float32x4x2_t u0_{vzipq_f32(t0_.val[0], t1_.val[0])};
    float32x4x2_t u1_{vzipq_f32(t0_.val[1], t1_.val[1])};
    x0 = u0_.val[0];
    x1 = u0_.val[1];
    x2 = u1_.val[0];
    x3 = u1_.val[1];
}

#define VSWAPHL(a,b) vcombine_f32(vget_low_f32(b), vget_high_f32(a))

/*
 * Generic GCC vector macros
 */
#elif defined(__GNUC__)

using v4sf [[gnu::vector_size(16), gnu::aligned(16)]] = float;
#define SIMD_SZ 4
#define VZERO() v4sf{0,0,0,0}
#define VMUL(a,b) ((a) * (b))
#define VADD(a,b) ((a) + (b))
#define VMADD(a,b,c) ((a)*(b) + (c))
#define VSUB(a,b) ((a) - (b))

constexpr force_inline v4sf ld_ps1(float a) noexcept { return v4sf{a, a, a, a}; }
#define LD_PS1 ld_ps1
#define VSET4(a, b, c, d) v4sf{(a), (b), (c), (d)}
constexpr force_inline v4sf vinsert0(v4sf v, float a) noexcept
{ return v4sf{a, v[1], v[2], v[3]}; }
#define VINSERT0 vinsert0
#define VEXTRACT0(v) ((v)[0])

force_inline v4sf unpacklo(v4sf a, v4sf b) noexcept
{ return v4sf{a[0], b[0], a[1], b[1]}; }
force_inline v4sf unpackhi(v4sf a, v4sf b) noexcept
{ return v4sf{a[2], b[2], a[3], b[3]}; }

force_inline void interleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    v4sf tmp{unpacklo(in1, in2)};
    out2 = unpackhi(in1, in2);
    out1 = tmp;
}
force_inline void uninterleave2(v4sf in1, v4sf in2, v4sf &out1, v4sf &out2) noexcept
{
    v4sf tmp{in1[0], in1[2], in2[0], in2[2]};
    out2 = v4sf{in1[1], in1[3], in2[1], in2[3]};
    out1 = tmp;
}

force_inline void vtranspose4(v4sf &x0, v4sf &x1, v4sf &x2, v4sf &x3) noexcept
{
    v4sf tmp0{unpacklo(x0, x1)};
    v4sf tmp2{unpacklo(x2, x3)};
    v4sf tmp1{unpackhi(x0, x1)};
    v4sf tmp3{unpackhi(x2, x3)};
    x0 = v4sf{tmp0[0], tmp0[1], tmp2[0], tmp2[1]};
    x1 = v4sf{tmp0[2], tmp0[3], tmp2[2], tmp2[3]};
    x2 = v4sf{tmp1[0], tmp1[1], tmp3[0], tmp3[1]};
    x3 = v4sf{tmp1[2], tmp1[3], tmp3[2], tmp3[3]};
}

force_inline v4sf vswaphl(v4sf a, v4sf b) noexcept
{ return v4sf{b[0], b[1], a[2], a[3]}; }
#define VSWAPHL vswaphl

#else

#warning "building with simd disabled !\n";
#define PFFFT_SIMD_DISABLE // fallback to scalar code
#endif

#endif /* PFFFT_SIMD_DISABLE */

// fallback mode for situations where SIMD is not available, use scalar mode instead
#ifdef PFFFT_SIMD_DISABLE
typedef float v4sf;
#define SIMD_SZ 1
#define VZERO() 0.f
#define VMUL(a,b) ((a)*(b))
#define VADD(a,b) ((a)+(b))
#define VMADD(a,b,c) ((a)*(b)+(c))
#define VSUB(a,b) ((a)-(b))
#define LD_PS1(p) (p)
#endif

inline bool valigned(const float *ptr) noexcept
{
    static constexpr uintptr_t alignmask{SIMD_SZ*4 - 1};
    return (reinterpret_cast<uintptr_t>(ptr) & alignmask) == 0;
}

// shortcuts for complex multiplications
force_inline void vcplxmul(v4sf &ar, v4sf &ai, v4sf br, v4sf bi) noexcept
{
    v4sf tmp{VMUL(ar, bi)};
    ar = VSUB(VMUL(ar, br), VMUL(ai, bi));
    ai = VMADD(ai, br, tmp);
}
force_inline void vcplxmulconj(v4sf &ar, v4sf &ai, v4sf br, v4sf bi) noexcept
{
    v4sf tmp{VMUL(ar, bi)};
    ar = VMADD(ai, bi, VMUL(ar, br));
    ai = VSUB(VMUL(ai, br), tmp);
}

#if !defined(PFFFT_SIMD_DISABLE)

#define assertv4(v,f0,f1,f2,f3) assert(v##_f[0] == (f0) && v##_f[1] == (f1) && v##_f[2] == (f2) && v##_f[3] == (f3))

/* detect bugs with the vector support macros */
[[maybe_unused]] void validate_pffft_simd()
{
    using float4 = std::array<float,4>;
    static constexpr float f[16]{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

    float4 a0_f, a1_f, a2_f, a3_f, t_f, u_f;
    v4sf a0_v, a1_v, a2_v, a3_v, t_v, u_v;
    std::memcpy(&a0_v, f, 4*sizeof(float));
    std::memcpy(&a1_v, f+4, 4*sizeof(float));
    std::memcpy(&a2_v, f+8, 4*sizeof(float));
    std::memcpy(&a3_v, f+12, 4*sizeof(float));

    t_v = VZERO(); t_f = al::bit_cast<float4>(t_v);
    printf("VZERO=[%2g %2g %2g %2g]\n", t_f[0], t_f[1], t_f[2], t_f[3]); assertv4(t, 0, 0, 0, 0);
    t_v = VADD(a1_v, a2_v); t_f = al::bit_cast<float4>(t_v);
    printf("VADD(4:7,8:11)=[%2g %2g %2g %2g]\n", t_f[0], t_f[1], t_f[2], t_f[3]); assertv4(t, 12, 14, 16, 18);
    t_v = VMUL(a1_v, a2_v); t_f = al::bit_cast<float4>(t_v);
    printf("VMUL(4:7,8:11)=[%2g %2g %2g %2g]\n", t_f[0], t_f[1], t_f[2], t_f[3]); assertv4(t, 32, 45, 60, 77);
    t_v = VMADD(a1_v, a2_v,a0_v); t_f = al::bit_cast<float4>(t_v);
    printf("VMADD(4:7,8:11,0:3)=[%2g %2g %2g %2g]\n", t_f[0], t_f[1], t_f[2], t_f[3]); assertv4(t, 32, 46, 62, 80);

    interleave2(a1_v,a2_v,t_v,u_v); t_f = al::bit_cast<float4>(t_v); u_f = al::bit_cast<float4>(u_v);
    printf("INTERLEAVE2(4:7,8:11)=[%2g %2g %2g %2g] [%2g %2g %2g %2g]\n", t_f[0], t_f[1], t_f[2], t_f[3], u_f[0], u_f[1], u_f[2], u_f[3]);
    assertv4(t, 4, 8, 5, 9); assertv4(u, 6, 10, 7, 11);
    uninterleave2(a1_v,a2_v,t_v,u_v); t_f = al::bit_cast<float4>(t_v); u_f = al::bit_cast<float4>(u_v);
    printf("UNINTERLEAVE2(4:7,8:11)=[%2g %2g %2g %2g] [%2g %2g %2g %2g]\n", t_f[0], t_f[1], t_f[2], t_f[3], u_f[0], u_f[1], u_f[2], u_f[3]);
    assertv4(t, 4, 6, 8, 10); assertv4(u, 5, 7, 9, 11);

    t_v=LD_PS1(f[15]); t_f = al::bit_cast<float4>(t_v);
    printf("LD_PS1(15)=[%2g %2g %2g %2g]\n", t_f[0], t_f[1], t_f[2], t_f[3]);
    assertv4(t, 15, 15, 15, 15);
    t_v = VSWAPHL(a1_v, a2_v); t_f = al::bit_cast<float4>(t_v);
    printf("VSWAPHL(4:7,8:11)=[%2g %2g %2g %2g]\n", t_f[0], t_f[1], t_f[2], t_f[3]);
    assertv4(t, 8, 9, 6, 7);
    vtranspose4(a0_v, a1_v, a2_v, a3_v);
    a0_f = al::bit_cast<float4>(a0_v);
    a1_f = al::bit_cast<float4>(a1_v);
    a2_f = al::bit_cast<float4>(a2_v);
    a3_f = al::bit_cast<float4>(a3_v);
    printf("VTRANSPOSE4(0:3,4:7,8:11,12:15)=[%2g %2g %2g %2g] [%2g %2g %2g %2g] [%2g %2g %2g %2g] [%2g %2g %2g %2g]\n",
          a0_f[0], a0_f[1], a0_f[2], a0_f[3], a1_f[0], a1_f[1], a1_f[2], a1_f[3],
          a2_f[0], a2_f[1], a2_f[2], a2_f[3], a3_f[0], a3_f[1], a3_f[2], a3_f[3]);
    assertv4(a0, 0, 4, 8, 12); assertv4(a1, 1, 5, 9, 13); assertv4(a2, 2, 6, 10, 14); assertv4(a3, 3, 7, 11, 15);
}
#endif //!PFFFT_SIMD_DISABLE

/* SSE and co like 16-bytes aligned pointers */
#define MALLOC_V4SF_ALIGNMENT 64 // with a 64-byte alignment, we are even aligned on L2 cache lines...

/*
  passf2 and passb2 has been merged here, fsign = -1 for passf2, +1 for passb2
*/
NOINLINE void passf2_ps(const size_t ido, const size_t l1, const v4sf *cc, v4sf *RESTRICT ch,
    const float *wa1, const float fsign)
{
    const size_t l1ido{l1*ido};
    if(ido <= 2)
    {
        for(size_t k{0};k < l1ido;k += ido, ch += ido, cc += 2*ido)
        {
            ch[0]         = VADD(cc[0], cc[ido+0]);
            ch[l1ido]     = VSUB(cc[0], cc[ido+0]);
            ch[1]         = VADD(cc[1], cc[ido+1]);
            ch[l1ido + 1] = VSUB(cc[1], cc[ido+1]);
        }
    }
    else
    {
        for(size_t k{0};k < l1ido;k += ido, ch += ido, cc += 2*ido)
        {
            for(size_t i{0};i < ido-1;i += 2)
            {
                v4sf tr2{VSUB(cc[i+0], cc[i+ido+0])};
                v4sf ti2{VSUB(cc[i+1], cc[i+ido+1])};
                v4sf wr{LD_PS1(wa1[i])}, wi{LD_PS1(wa1[i+1]*fsign)};
                ch[i]   = VADD(cc[i+0], cc[i+ido+0]);
                ch[i+1] = VADD(cc[i+1], cc[i+ido+1]);
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
    const float *wa1, const float *wa2, const float fsign)
{
    assert(ido > 2);

    const v4sf taur{LD_PS1(-0.5f)};
    const v4sf taui{LD_PS1(0.866025403784439f*fsign)};
    const size_t l1ido{l1*ido};
    for(size_t k{0};k < l1ido;k += ido, cc += 3*ido, ch +=ido)
    {
        for(size_t i{0};i < ido-1;i += 2)
        {
            v4sf tr2{VADD(cc[i+ido], cc[i+2*ido])};
            v4sf cr2{VMADD(taur, tr2, cc[i])};
            ch[i]  = VADD(tr2, cc[i]);
            v4sf ti2{VADD(cc[i+ido+1], cc[i+2*ido+1])};
            v4sf ci2{VMADD(taur, ti2, cc[i+1])};
            ch[i+1] = VADD(cc[i+1], ti2);
            v4sf cr3{VMUL(taui, VSUB(cc[i+ido], cc[i+2*ido]))};
            v4sf ci3{VMUL(taui, VSUB(cc[i+ido+1], cc[i+2*ido+1]))};
            v4sf dr2{VSUB(cr2, ci3)};
            v4sf dr3{VADD(cr2, ci3)};
            v4sf di2{VADD(ci2, cr3)};
            v4sf di3{VSUB(ci2, cr3)};
            float wr1{wa1[i]}, wi1{fsign*wa1[i+1]}, wr2{wa2[i]}, wi2{fsign*wa2[i+1]};
            vcplxmul(dr2, di2, LD_PS1(wr1), LD_PS1(wi1));
            ch[i+l1ido] = dr2;
            ch[i+l1ido + 1] = di2;
            vcplxmul(dr3, di3, LD_PS1(wr2), LD_PS1(wi2));
            ch[i+2*l1ido] = dr3;
            ch[i+2*l1ido+1] = di3;
        }
    }
} /* passf3 */

NOINLINE void passf4_ps(const size_t ido, const size_t l1, const v4sf *cc, v4sf *RESTRICT ch,
    const float *wa1, const float *wa2, const float *wa3, const float fsign)
{
    /* fsign == -1 for forward transform and +1 for backward transform */
    const v4sf vsign{LD_PS1(fsign)};
    const size_t l1ido{l1*ido};
    if(ido == 2)
    {
        for(size_t k{0};k < l1ido;k += ido, ch += ido, cc += 4*ido)
        {
            v4sf tr1{VSUB(cc[0], cc[2*ido + 0])};
            v4sf tr2{VADD(cc[0], cc[2*ido + 0])};
            v4sf ti1{VSUB(cc[1], cc[2*ido + 1])};
            v4sf ti2{VADD(cc[1], cc[2*ido + 1])};
            v4sf ti4{VMUL(VSUB(cc[1*ido + 0], cc[3*ido + 0]), vsign)};
            v4sf tr4{VMUL(VSUB(cc[3*ido + 1], cc[1*ido + 1]), vsign)};
            v4sf tr3{VADD(cc[ido + 0], cc[3*ido + 0])};
            v4sf ti3{VADD(cc[ido + 1], cc[3*ido + 1])};

            ch[0*l1ido + 0] = VADD(tr2, tr3);
            ch[0*l1ido + 1] = VADD(ti2, ti3);
            ch[1*l1ido + 0] = VADD(tr1, tr4);
            ch[1*l1ido + 1] = VADD(ti1, ti4);
            ch[2*l1ido + 0] = VSUB(tr2, tr3);
            ch[2*l1ido + 1] = VSUB(ti2, ti3);
            ch[3*l1ido + 0] = VSUB(tr1, tr4);
            ch[3*l1ido + 1] = VSUB(ti1, ti4);
        }
    }
    else
    {
        for(size_t k{0};k < l1ido;k += ido, ch+=ido, cc += 4*ido)
        {
            for(size_t i{0};i < ido-1;i+=2)
            {
                v4sf tr1{VSUB(cc[i + 0], cc[i + 2*ido + 0])};
                v4sf tr2{VADD(cc[i + 0], cc[i + 2*ido + 0])};
                v4sf ti1{VSUB(cc[i + 1], cc[i + 2*ido + 1])};
                v4sf ti2{VADD(cc[i + 1], cc[i + 2*ido + 1])};
                v4sf tr4{VMUL(VSUB(cc[i + 3*ido + 1], cc[i + 1*ido + 1]), vsign)};
                v4sf ti4{VMUL(VSUB(cc[i + 1*ido + 0], cc[i + 3*ido + 0]), vsign)};
                v4sf tr3{VADD(cc[i + ido + 0], cc[i + 3*ido + 0])};
                v4sf ti3{VADD(cc[i + ido + 1], cc[i + 3*ido + 1])};

                ch[i] = VADD(tr2, tr3);
                v4sf cr3{VSUB(tr2, tr3)};
                ch[i + 1] = VADD(ti2, ti3);
                v4sf ci3{VSUB(ti2, ti3)};

                v4sf cr2{VADD(tr1, tr4)};
                v4sf cr4{VSUB(tr1, tr4)};
                v4sf ci2{VADD(ti1, ti4)};
                v4sf ci4{VSUB(ti1, ti4)};
                float wr1{wa1[i]}, wi1{fsign*wa1[i+1]};
                vcplxmul(cr2, ci2, LD_PS1(wr1), LD_PS1(wi1));
                float wr2{wa2[i]}, wi2{fsign*wa2[i+1]};
                ch[i + l1ido] = cr2;
                ch[i + l1ido + 1] = ci2;

                vcplxmul(cr3, ci3, LD_PS1(wr2), LD_PS1(wi2));
                float wr3{wa3[i]}, wi3{fsign*wa3[i+1]};
                ch[i + 2*l1ido] = cr3;
                ch[i + 2*l1ido + 1] = ci3;

                vcplxmul(cr4, ci4, LD_PS1(wr3), LD_PS1(wi3));
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
    const float *wa1, const float *wa2, const float *wa3, const float *wa4, const float fsign)
{
    const v4sf tr11{LD_PS1(0.309016994374947f)};
    const v4sf tr12{LD_PS1(-0.809016994374947f)};
    const v4sf ti11{LD_PS1(0.951056516295154f*fsign)};
    const v4sf ti12{LD_PS1(0.587785252292473f*fsign)};

#define cc_ref(a_1,a_2) cc[(a_2-1)*ido + (a_1) + 1]
#define ch_ref(a_1,a_3) ch[(a_3-1)*l1*ido + (a_1) + 1]

    assert(ido > 2);
    for(size_t k{0};k < l1;++k, cc += 5*ido, ch += ido)
    {
        for(size_t i{0};i < ido-1;i += 2)
        {
            v4sf ti5{VSUB(cc_ref(i  , 2), cc_ref(i  , 5))};
            v4sf ti2{VADD(cc_ref(i  , 2), cc_ref(i  , 5))};
            v4sf ti4{VSUB(cc_ref(i  , 3), cc_ref(i  , 4))};
            v4sf ti3{VADD(cc_ref(i  , 3), cc_ref(i  , 4))};
            v4sf tr5{VSUB(cc_ref(i-1, 2), cc_ref(i-1, 5))};
            v4sf tr2{VADD(cc_ref(i-1, 2), cc_ref(i-1, 5))};
            v4sf tr4{VSUB(cc_ref(i-1, 3), cc_ref(i-1, 4))};
            v4sf tr3{VADD(cc_ref(i-1, 3), cc_ref(i-1, 4))};
            ch_ref(i-1, 1) = VADD(cc_ref(i-1, 1), VADD(tr2, tr3));
            ch_ref(i  , 1) = VADD(cc_ref(i  , 1), VADD(ti2, ti3));
            v4sf cr2{VADD(cc_ref(i-1, 1), VMADD(tr11, tr2, VMUL(tr12, tr3)))};
            v4sf ci2{VADD(cc_ref(i  , 1), VMADD(tr11, ti2, VMUL(tr12, ti3)))};
            v4sf cr3{VADD(cc_ref(i-1, 1), VMADD(tr12, tr2, VMUL(tr11, tr3)))};
            v4sf ci3{VADD(cc_ref(i  , 1), VMADD(tr12, ti2, VMUL(tr11, ti3)))};
            v4sf cr5{VMADD(ti11, tr5, VMUL(ti12, tr4))};
            v4sf ci5{VMADD(ti11, ti5, VMUL(ti12, ti4))};
            v4sf cr4{VSUB(VMUL(ti12, tr5), VMUL(ti11, tr4))};
            v4sf ci4{VSUB(VMUL(ti12, ti5), VMUL(ti11, ti4))};
            v4sf dr3{VSUB(cr3, ci4)};
            v4sf dr4{VADD(cr3, ci4)};
            v4sf di3{VADD(ci3, cr4)};
            v4sf di4{VSUB(ci3, cr4)};
            v4sf dr5{VADD(cr2, ci5)};
            v4sf dr2{VSUB(cr2, ci5)};
            v4sf di5{VSUB(ci2, cr5)};
            v4sf di2{VADD(ci2, cr5)};
            float wr1{wa1[i]}, wi1{fsign*wa1[i+1]}, wr2{wa2[i]}, wi2{fsign*wa2[i+1]};
            float wr3{wa3[i]}, wi3{fsign*wa3[i+1]}, wr4{wa4[i]}, wi4{fsign*wa4[i+1]};
            vcplxmul(dr2, di2, LD_PS1(wr1), LD_PS1(wi1));
            ch_ref(i - 1, 2) = dr2;
            ch_ref(i, 2)     = di2;
            vcplxmul(dr3, di3, LD_PS1(wr2), LD_PS1(wi2));
            ch_ref(i - 1, 3) = dr3;
            ch_ref(i, 3)     = di3;
            vcplxmul(dr4, di4, LD_PS1(wr3), LD_PS1(wi3));
            ch_ref(i - 1, 4) = dr4;
            ch_ref(i, 4)     = di4;
            vcplxmul(dr5, di5, LD_PS1(wr4), LD_PS1(wi4));
            ch_ref(i - 1, 5) = dr5;
            ch_ref(i, 5)     = di5;
        }
    }
#undef ch_ref
#undef cc_ref
}

NOINLINE void radf2_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc,
    v4sf *RESTRICT ch, const float *wa1)
{
    const size_t l1ido{l1*ido};
    for(size_t k{0};k < l1ido;k += ido)
    {
        v4sf a{cc[k]}, b{cc[k + l1ido]};
        ch[2*k] = VADD(a, b);
        ch[2*(k+ido)-1] = VSUB(a, b);
    }
    if(ido < 2)
        return;
    if(ido != 2)
    {
        for(size_t k{0};k < l1ido;k += ido)
        {
            for(size_t i{2};i < ido;i += 2)
            {
                v4sf tr2{cc[i - 1 + k + l1ido]}, ti2{cc[i + k + l1ido]};
                v4sf br{cc[i - 1 + k]}, bi{cc[i + k]};
                vcplxmulconj(tr2, ti2, LD_PS1(wa1[i - 2]), LD_PS1(wa1[i - 1]));
                ch[i + 2*k] = VADD(bi, ti2);
                ch[2*(k+ido) - i] = VSUB(ti2, bi);
                ch[i - 1 + 2*k] = VADD(br, tr2);
                ch[2*(k+ido) - i -1] = VSUB(br, tr2);
            }
        }
        if((ido&1) == 1)
            return;
    }
    const v4sf minus_one{LD_PS1(-1.0f)};
    for(size_t k{0};k < l1ido;k += ido)
    {
        ch[2*k + ido] = VMUL(minus_one, cc[ido-1 + k + l1ido]);
        ch[2*k + ido-1] = cc[k + ido-1];
    }
} /* radf2 */


NOINLINE void radb2_ps(const size_t ido, const size_t l1, const v4sf *cc, v4sf *RESTRICT ch,
    const float *wa1)
{
    const size_t l1ido{l1*ido};
    for(size_t k{0};k < l1ido;k += ido)
    {
        v4sf a{cc[2*k]};
        v4sf b{cc[2*(k+ido) - 1]};
        ch[k] = VADD(a, b);
        ch[k + l1ido] = VSUB(a, b);
    }
    if(ido < 2)
        return;
    if(ido != 2)
    {
        for(size_t k{0};k < l1ido;k += ido)
        {
            for(size_t i{2};i < ido;i += 2)
            {
                v4sf a{cc[i-1 + 2*k]};
                v4sf b{cc[2*(k + ido) - i - 1]};
                v4sf c{cc[i+0 + 2*k]};
                v4sf d{cc[2*(k + ido) - i + 0]};
                ch[i-1 + k] = VADD(a, b);
                v4sf tr2{VSUB(a, b)};
                ch[i+0 + k] = VSUB(c, d);
                v4sf ti2{VADD(c, d)};
                vcplxmul(tr2, ti2, LD_PS1(wa1[i - 2]), LD_PS1(wa1[i - 1]));
                ch[i-1 + k + l1ido] = tr2;
                ch[i+0 + k + l1ido] = ti2;
            }
        }
        if((ido&1) == 1)
            return;
    }
    const v4sf minus_two{LD_PS1(-2.0f)};
    for(size_t k{0};k < l1ido;k += ido)
    {
        v4sf a{cc[2*k + ido-1]};
        v4sf b{cc[2*k + ido]};
        ch[k + ido-1] = VADD(a,a);
        ch[k + ido-1 + l1ido] = VMUL(minus_two, b);
    }
} /* radb2 */

void radf3_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch,
    const float *wa1, const float *wa2)
{
    const v4sf taur{LD_PS1(-0.5f)};
    const v4sf taui{LD_PS1(0.866025403784439f)};
    for(size_t k{0};k < l1;++k)
    {
        v4sf cr2{VADD(cc[(k + l1)*ido], cc[(k + 2*l1)*ido])};
        ch[        (3*k    )*ido] = VADD(cc[k*ido], cr2);
        ch[        (3*k + 2)*ido] = VMUL(taui, VSUB(cc[(k + l1*2)*ido], cc[(k + l1)*ido]));
        ch[ido-1 + (3*k + 1)*ido] = VMADD(taur, cr2, cc[k*ido]);
    }
    if(ido == 1)
        return;
    for(size_t k{0};k < l1;++k)
    {
        for(size_t i{2};i < ido;i += 2)
        {
            const size_t ic{ido - i};
            v4sf wr1{LD_PS1(wa1[i - 2])};
            v4sf wi1{LD_PS1(wa1[i - 1])};
            v4sf dr2{cc[i - 1 + (k + l1)*ido]};
            v4sf di2{cc[i     + (k + l1)*ido]};
            vcplxmulconj(dr2, di2, wr1, wi1);

            v4sf wr2{LD_PS1(wa2[i - 2])};
            v4sf wi2{LD_PS1(wa2[i - 1])};
            v4sf dr3{cc[i - 1 + (k + l1*2)*ido]};
            v4sf di3{cc[i     + (k + l1*2)*ido]};
            vcplxmulconj(dr3, di3, wr2, wi2);

            v4sf cr2{VADD(dr2, dr3)};
            v4sf ci2{VADD(di2, di3)};
            ch[i - 1 + 3*k*ido] = VADD(cc[i - 1 + k*ido], cr2);
            ch[i     + 3*k*ido] = VADD(cc[i     + k*ido], ci2);
            v4sf tr2{VMADD(taur, cr2, cc[i - 1 + k*ido])};
            v4sf ti2{VMADD(taur, ci2, cc[i     + k*ido])};
            v4sf tr3{VMUL(taui, VSUB(di2, di3))};
            v4sf ti3{VMUL(taui, VSUB(dr3, dr2))};
            ch[i  - 1 + (3*k + 2)*ido] = VADD(tr2, tr3);
            ch[ic - 1 + (3*k + 1)*ido] = VSUB(tr2, tr3);
            ch[i      + (3*k + 2)*ido] = VADD(ti2, ti3);
            ch[ic     + (3*k + 1)*ido] = VSUB(ti3, ti2);
        }
    }
} /* radf3 */


void radb3_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch,
    const float *wa1, const float *wa2)
{
    static constexpr float taur{-0.5f};
    static constexpr float taui{0.866025403784439f};
    static constexpr float taui_2{taui*2.0f};

    const v4sf vtaur{LD_PS1(taur)};
    const v4sf vtaui_2{LD_PS1(taui_2)};
    for(size_t k{0};k < l1;++k)
    {
        v4sf tr2 = cc[ido-1 + (3*k + 1)*ido];
        tr2 = VADD(tr2,tr2);
        v4sf cr2 = VMADD(vtaur, tr2, cc[3*k*ido]);
        ch[k*ido] = VADD(cc[3*k*ido], tr2);
        v4sf ci3 = VMUL(vtaui_2, cc[(3*k + 2)*ido]);
        ch[(k + l1)*ido] = VSUB(cr2, ci3);
        ch[(k + 2*l1)*ido] = VADD(cr2, ci3);
    }
    if(ido == 1)
        return;
    const v4sf vtaui{LD_PS1(taui)};
    for(size_t k{0};k < l1;++k)
    {
        for(size_t i{2};i < ido;i += 2)
        {
            const size_t ic{ido - i};
            v4sf tr2{VADD(cc[i - 1 + (3*k + 2)*ido], cc[ic - 1 + (3*k + 1)*ido])};
            v4sf cr2{VMADD(vtaur, tr2, cc[i - 1 + 3*k*ido])};
            ch[i - 1 + k*ido] = VADD(cc[i - 1 + 3*k*ido], tr2);
            v4sf ti2{VSUB(cc[i + (3*k + 2)*ido], cc[ic + (3*k + 1)*ido])};
            v4sf ci2{VMADD(vtaur, ti2, cc[i + 3*k*ido])};
            ch[i + k*ido] = VADD(cc[i + 3*k*ido], ti2);
            v4sf cr3{VMUL(vtaui, VSUB(cc[i - 1 + (3*k + 2)*ido], cc[ic - 1 + (3*k + 1)*ido]))};
            v4sf ci3{VMUL(vtaui, VADD(cc[i + (3*k + 2)*ido], cc[ic + (3*k + 1)*ido]))};
            v4sf dr2{VSUB(cr2, ci3)};
            v4sf dr3{VADD(cr2, ci3)};
            v4sf di2{VADD(ci2, cr3)};
            v4sf di3{VSUB(ci2, cr3)};
            vcplxmul(dr2, di2, LD_PS1(wa1[i-2]), LD_PS1(wa1[i-1]));
            ch[i - 1 + (k + l1)*ido] = dr2;
            ch[i + (k + l1)*ido] = di2;
            vcplxmul(dr3, di3, LD_PS1(wa2[i-2]), LD_PS1(wa2[i-1]));
            ch[i - 1 + (k + 2*l1)*ido] = dr3;
            ch[i + (k + 2*l1)*ido] = di3;
        }
    }
} /* radb3 */

NOINLINE void radf4_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc,
    v4sf *RESTRICT ch, const float *RESTRICT wa1, const float *RESTRICT wa2,
    const float *RESTRICT wa3)
{
    const size_t l1ido{l1*ido};
    {
        const v4sf *RESTRICT cc_{cc}, *RESTRICT cc_end{cc + l1ido};
        v4sf *RESTRICT ch_{ch};
        while(cc != cc_end)
        {
            // this loop represents between 25% and 40% of total radf4_ps cost !
            v4sf a0{cc[0]}, a1{cc[l1ido]};
            v4sf a2{cc[2*l1ido]}, a3{cc[3*l1ido]};
            v4sf tr1{VADD(a1, a3)};
            v4sf tr2{VADD(a0, a2)};
            ch[2*ido-1] = VSUB(a0, a2);
            ch[2*ido  ] = VSUB(a3, a1);
            ch[0      ] = VADD(tr1, tr2);
            ch[4*ido-1] = VSUB(tr2, tr1);
            cc += ido; ch += 4*ido;
        }
        cc = cc_;
        ch = ch_;
    }
    if(ido < 2)
        return;
    if(ido != 2)
    {
        for(size_t k{0};k < l1ido;k += ido)
        {
            const v4sf *RESTRICT pc{cc + 1 + k};
            for(size_t i{2};i < ido;i += 2, pc += 2)
            {
                const size_t ic{ido - i};

                v4sf cr2{pc[1*l1ido+0]};
                v4sf ci2{pc[1*l1ido+1]};
                v4sf wr{LD_PS1(wa1[i - 2])};
                v4sf wi{LD_PS1(wa1[i - 1])};
                vcplxmulconj(cr2,ci2,wr,wi);

                v4sf cr3{pc[2*l1ido+0]};
                v4sf ci3{pc[2*l1ido+1]};
                wr = LD_PS1(wa2[i-2]);
                wi = LD_PS1(wa2[i-1]);
                vcplxmulconj(cr3, ci3, wr, wi);

                v4sf cr4{pc[3*l1ido]};
                v4sf ci4{pc[3*l1ido+1]};
                wr = LD_PS1(wa3[i-2]);
                wi = LD_PS1(wa3[i-1]);
                vcplxmulconj(cr4, ci4, wr, wi);

                /* at this point, on SSE, five of "cr2 cr3 cr4 ci2 ci3 ci4" should be loaded in registers */

                v4sf tr1{VADD(cr2,cr4)};
                v4sf tr4{VSUB(cr4,cr2)};
                v4sf tr2{VADD(pc[0],cr3)};
                v4sf tr3{VSUB(pc[0],cr3)};
                ch[i  - 1 + 4*k        ] = VADD(tr2,tr1);
                ch[ic - 1 + 4*k + 3*ido] = VSUB(tr2,tr1); // at this point tr1 and tr2 can be disposed
                v4sf ti1{VADD(ci2,ci4)};
                v4sf ti4{VSUB(ci2,ci4)};
                ch[i  - 1 + 4*k + 2*ido] = VADD(tr3,ti4);
                ch[ic - 1 + 4*k + 1*ido] = VSUB(tr3,ti4); // dispose tr3, ti4
                v4sf ti2{VADD(pc[1],ci3)};
                v4sf ti3{VSUB(pc[1],ci3)};
                ch[i  + 4*k        ] = VADD(ti1, ti2);
                ch[ic + 4*k + 3*ido] = VSUB(ti1, ti2);
                ch[i  + 4*k + 2*ido] = VADD(tr4, ti3);
                ch[ic + 4*k + 1*ido] = VSUB(tr4, ti3);
            }
        }
        if((ido&1) == 1)
            return;
    }
    const v4sf minus_hsqt2{LD_PS1(al::numbers::sqrt2_v<float> * -0.5f)};
    for(size_t k{0};k < l1ido;k += ido)
    {
        v4sf a{cc[ido-1 + k + l1ido]}, b{cc[ido-1 + k + 3*l1ido]};
        v4sf c{cc[ido-1 + k]}, d{cc[ido-1 + k + 2*l1ido]};
        v4sf ti1{VMUL(minus_hsqt2, VADD(b, a))};
        v4sf tr1{VMUL(minus_hsqt2, VSUB(b, a))};
        ch[ido-1 + 4*k        ] = VADD(c, tr1);
        ch[ido-1 + 4*k + 2*ido] = VSUB(c, tr1);
        ch[        4*k + 1*ido] = VSUB(ti1, d);
        ch[        4*k + 3*ido] = VADD(ti1, d);
    }
} /* radf4 */


NOINLINE void radb4_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc,
    v4sf *RESTRICT ch, const float *RESTRICT wa1, const float *RESTRICT wa2,
    const float *RESTRICT wa3)
{
    const v4sf two{LD_PS1(2.0f)};
    const size_t l1ido{l1*ido};
    {
        const v4sf *RESTRICT cc_{cc}, *RESTRICT ch_end{ch + l1ido};
        v4sf *ch_{ch};
        while(ch != ch_end)
        {
            v4sf a{cc[0]}, b{cc[4*ido-1]};
            v4sf c{cc[2*ido]}, d{cc[2*ido-1]};
            v4sf tr3{VMUL(two,d)};
            v4sf tr2{VADD(a,b)};
            v4sf tr1{VSUB(a,b)};
            v4sf tr4{VMUL(two,c)};
            ch[0*l1ido] = VADD(tr2, tr3);
            ch[2*l1ido] = VSUB(tr2, tr3);
            ch[1*l1ido] = VSUB(tr1, tr4);
            ch[3*l1ido] = VADD(tr1, tr4);

            cc += 4*ido; ch += ido;
        }
        cc = cc_; ch = ch_;
    }
    if(ido < 2)
        return;
    if(ido != 2)
    {
        for(size_t k{0};k < l1ido;k += ido)
        {
            const v4sf *RESTRICT pc{cc - 1 + 4*k};
            v4sf *RESTRICT ph{ch + k + 1};
            for(size_t i{2};i < ido;i += 2)
            {
                v4sf tr1{VSUB(pc[        i], pc[4*ido - i])};
                v4sf tr2{VADD(pc[        i], pc[4*ido - i])};
                v4sf ti4{VSUB(pc[2*ido + i], pc[2*ido - i])};
                v4sf tr3{VADD(pc[2*ido + i], pc[2*ido - i])};
                ph[0] = VADD(tr2, tr3);
                v4sf cr3{VSUB(tr2, tr3)};

                v4sf ti3{VSUB(pc[2*ido + i + 1], pc[2*ido - i + 1])};
                v4sf tr4{VADD(pc[2*ido + i + 1], pc[2*ido - i + 1])};
                v4sf cr2{VSUB(tr1, tr4)};
                v4sf cr4{VADD(tr1, tr4)};

                v4sf ti1{VADD(pc[i + 1], pc[4*ido - i + 1])};
                v4sf ti2{VSUB(pc[i + 1], pc[4*ido - i + 1])};

                ph[1] = VADD(ti2, ti3); ph += l1ido;
                v4sf ci3{VSUB(ti2, ti3)};
                v4sf ci2{VADD(ti1, ti4)};
                v4sf ci4{VSUB(ti1, ti4)};
                vcplxmul(cr2, ci2, LD_PS1(wa1[i-2]), LD_PS1(wa1[i-1]));
                ph[0] = cr2;
                ph[1] = ci2; ph += l1ido;
                vcplxmul(cr3, ci3, LD_PS1(wa2[i-2]), LD_PS1(wa2[i-1]));
                ph[0] = cr3;
                ph[1] = ci3; ph += l1ido;
                vcplxmul(cr4, ci4, LD_PS1(wa3[i-2]), LD_PS1(wa3[i-1]));
                ph[0] = cr4;
                ph[1] = ci4; ph = ph - 3*l1ido + 2;
            }
        }
        if((ido&1) == 1)
            return;
    }
    const v4sf minus_sqrt2{LD_PS1(-1.414213562373095f)};
    for(size_t k{0};k < l1ido;k += ido)
    {
        const size_t i0{4*k + ido};
        v4sf c{cc[i0-1]}, d{cc[i0 + 2*ido-1]};
        v4sf a{cc[i0+0]}, b{cc[i0 + 2*ido+0]};
        v4sf tr1{VSUB(c,d)};
        v4sf tr2{VADD(c,d)};
        v4sf ti1{VADD(b,a)};
        v4sf ti2{VSUB(b,a)};
        ch[ido-1 + k + 0*l1ido] = VADD(tr2,tr2);
        ch[ido-1 + k + 1*l1ido] = VMUL(minus_sqrt2, VSUB(ti1, tr1));
        ch[ido-1 + k + 2*l1ido] = VADD(ti2, ti2);
        ch[ido-1 + k + 3*l1ido] = VMUL(minus_sqrt2, VADD(ti1, tr1));
    }
} /* radb4 */

void radf5_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch,
    const float *wa1, const float *wa2, const float *wa3, const float *wa4)
{
    const v4sf tr11{LD_PS1(0.309016994374947f)};
    const v4sf ti11{LD_PS1(0.951056516295154f)};
    const v4sf tr12{LD_PS1(-0.809016994374947f)};
    const v4sf ti12{LD_PS1(0.587785252292473f)};

#define cc_ref(a_1,a_2,a_3) cc[((a_3)*l1 + (a_2))*ido + a_1]
#define ch_ref(a_1,a_2,a_3) ch[((a_3)*5 + (a_2))*ido + a_1]

    /* Parameter adjustments */
    ch -= 1 + ido * 6;
    cc -= 1 + ido * (1 + l1);

    /* Function Body */
    for(size_t k{1};k <= l1;++k)
    {
        v4sf cr2{VADD(cc_ref(1, k, 5), cc_ref(1, k, 2))};
        v4sf ci5{VSUB(cc_ref(1, k, 5), cc_ref(1, k, 2))};
        v4sf cr3{VADD(cc_ref(1, k, 4), cc_ref(1, k, 3))};
        v4sf ci4{VSUB(cc_ref(1, k, 4), cc_ref(1, k, 3))};
        ch_ref(1, 1, k)   = VADD(cc_ref(1, k, 1), VADD(cr2, cr3));
        ch_ref(ido, 2, k) = VADD(cc_ref(1, k, 1), VMADD(tr11, cr2, VMUL(tr12, cr3)));
        ch_ref(1, 3, k)   = VMADD(ti11, ci5, VMUL(ti12, ci4));
        ch_ref(ido, 4, k) = VADD(cc_ref(1, k, 1), VMADD(tr12, cr2, VMUL(tr11, cr3)));
        ch_ref(1, 5, k)   = VSUB(VMUL(ti12, ci5), VMUL(ti11, ci4));
        //printf("pffft: radf5, k=%d ch_ref=%f, ci4=%f\n", k, ch_ref(1, 5, k), ci4);
    }
    if(ido == 1)
        return;

    const size_t idp2{ido + 2};
    for(size_t k{1};k <= l1;++k)
    {
        for(size_t i{3};i <= ido;i += 2)
        {
            const size_t ic{idp2 - i};
            v4sf dr2{LD_PS1(wa1[i-3])};
            v4sf di2{LD_PS1(wa1[i-2])};
            v4sf dr3{LD_PS1(wa2[i-3])};
            v4sf di3{LD_PS1(wa2[i-2])};
            v4sf dr4{LD_PS1(wa3[i-3])};
            v4sf di4{LD_PS1(wa3[i-2])};
            v4sf dr5{LD_PS1(wa4[i-3])};
            v4sf di5{LD_PS1(wa4[i-2])};
            vcplxmulconj(dr2, di2, cc_ref(i-1, k, 2), cc_ref(i, k, 2));
            vcplxmulconj(dr3, di3, cc_ref(i-1, k, 3), cc_ref(i, k, 3));
            vcplxmulconj(dr4, di4, cc_ref(i-1, k, 4), cc_ref(i, k, 4));
            vcplxmulconj(dr5, di5, cc_ref(i-1, k, 5), cc_ref(i, k, 5));
            v4sf cr2{VADD(dr2, dr5)};
            v4sf ci5{VSUB(dr5, dr2)};
            v4sf cr5{VSUB(di2, di5)};
            v4sf ci2{VADD(di2, di5)};
            v4sf cr3{VADD(dr3, dr4)};
            v4sf ci4{VSUB(dr4, dr3)};
            v4sf cr4{VSUB(di3, di4)};
            v4sf ci3{VADD(di3, di4)};
            ch_ref(i - 1, 1, k) = VADD(cc_ref(i - 1, k, 1), VADD(cr2, cr3));
            ch_ref(i, 1, k) = VSUB(cc_ref(i, k, 1), VADD(ci2, ci3));
            v4sf tr2{VADD(cc_ref(i - 1, k, 1), VMADD(tr11, cr2, VMUL(tr12, cr3)))};
            v4sf ti2{VSUB(cc_ref(i, k, 1), VMADD(tr11, ci2, VMUL(tr12, ci3)))};
            v4sf tr3{VADD(cc_ref(i - 1, k, 1), VMADD(tr12, cr2, VMUL(tr11, cr3)))};
            v4sf ti3{VSUB(cc_ref(i, k, 1), VMADD(tr12, ci2, VMUL(tr11, ci3)))};
            v4sf tr5{VMADD(ti11, cr5, VMUL(ti12, cr4))};
            v4sf ti5{VMADD(ti11, ci5, VMUL(ti12, ci4))};
            v4sf tr4{VSUB(VMUL(ti12, cr5), VMUL(ti11, cr4))};
            v4sf ti4{VSUB(VMUL(ti12, ci5), VMUL(ti11, ci4))};
            ch_ref(i  - 1, 3, k) = VSUB(tr2, tr5);
            ch_ref(ic - 1, 2, k) = VADD(tr2, tr5);
            ch_ref(i     , 3, k) = VADD(ti5, ti2);
            ch_ref(ic    , 2, k) = VSUB(ti5, ti2);
            ch_ref(i  - 1, 5, k) = VSUB(tr3, tr4);
            ch_ref(ic - 1, 4, k) = VADD(tr3, tr4);
            ch_ref(i     , 5, k) = VADD(ti4, ti3);
            ch_ref(ic    , 4, k) = VSUB(ti4, ti3);
        }
    }
#undef cc_ref
#undef ch_ref
} /* radf5 */

void radb5_ps(const size_t ido, const size_t l1, const v4sf *RESTRICT cc, v4sf *RESTRICT ch,
    const float *wa1, const float *wa2, const float *wa3, const float *wa4)
{
    const v4sf tr11{LD_PS1(0.309016994374947f)};
    const v4sf ti11{LD_PS1(0.951056516295154f)};
    const v4sf tr12{LD_PS1(-0.809016994374947f)};
    const v4sf ti12{LD_PS1(0.587785252292473f)};

#define cc_ref(a_1,a_2,a_3) cc[((a_3)*5 + (a_2))*ido + a_1]
#define ch_ref(a_1,a_2,a_3) ch[((a_3)*l1 + (a_2))*ido + a_1]

    /* Parameter adjustments */
    ch -= 1 + ido*(1 + l1);
    cc -= 1 + ido*6;

    /* Function Body */
    for(size_t k{1};k <= l1;++k)
    {
        v4sf ti5{VADD(cc_ref(  1, 3, k), cc_ref(1, 3, k))};
        v4sf ti4{VADD(cc_ref(  1, 5, k), cc_ref(1, 5, k))};
        v4sf tr2{VADD(cc_ref(ido, 2, k), cc_ref(ido, 2, k))};
        v4sf tr3{VADD(cc_ref(ido, 4, k), cc_ref(ido, 4, k))};
        ch_ref(1, k, 1) = VADD(cc_ref(1, 1, k), VADD(tr2, tr3));
        v4sf cr2{VADD(cc_ref(1, 1, k), VMADD(tr11, tr2, VMUL(tr12, tr3)))};
        v4sf cr3{VADD(cc_ref(1, 1, k), VMADD(tr12, tr2, VMUL(tr11, tr3)))};
        v4sf ci5{VMADD(ti11, ti5, VMUL(ti12, ti4))};
        v4sf ci4{VSUB(VMUL(ti12, ti5), VMUL(ti11, ti4))};
        ch_ref(1, k, 2) = VSUB(cr2, ci5);
        ch_ref(1, k, 3) = VSUB(cr3, ci4);
        ch_ref(1, k, 4) = VADD(cr3, ci4);
        ch_ref(1, k, 5) = VADD(cr2, ci5);
    }
    if(ido == 1)
        return;

    const size_t idp2{ido + 2};
    for(size_t k{1};k <= l1;++k)
    {
        for(size_t i{3};i <= ido;i += 2)
        {
            const size_t ic{idp2 - i};
            v4sf ti5{VADD(cc_ref(i  , 3, k), cc_ref(ic  , 2, k))};
            v4sf ti2{VSUB(cc_ref(i  , 3, k), cc_ref(ic  , 2, k))};
            v4sf ti4{VADD(cc_ref(i  , 5, k), cc_ref(ic  , 4, k))};
            v4sf ti3{VSUB(cc_ref(i  , 5, k), cc_ref(ic  , 4, k))};
            v4sf tr5{VSUB(cc_ref(i-1, 3, k), cc_ref(ic-1, 2, k))};
            v4sf tr2{VADD(cc_ref(i-1, 3, k), cc_ref(ic-1, 2, k))};
            v4sf tr4{VSUB(cc_ref(i-1, 5, k), cc_ref(ic-1, 4, k))};
            v4sf tr3{VADD(cc_ref(i-1, 5, k), cc_ref(ic-1, 4, k))};
            ch_ref(i - 1, k, 1) = VADD(cc_ref(i-1, 1, k), VADD(tr2, tr3));
            ch_ref(i    , k, 1) = VADD(cc_ref(i  , 1, k), VADD(ti2, ti3));
            v4sf cr2{VADD(cc_ref(i-1, 1, k), VMADD(tr11, tr2, VMUL(tr12, tr3)))};
            v4sf ci2{VADD(cc_ref(i  , 1, k), VMADD(tr11, ti2, VMUL(tr12, ti3)))};
            v4sf cr3{VADD(cc_ref(i-1, 1, k), VMADD(tr12, tr2, VMUL(tr11, tr3)))};
            v4sf ci3{VADD(cc_ref(i  , 1, k), VMADD(tr12, ti2, VMUL(tr11, ti3)))};
            v4sf cr5{VMADD(ti11, tr5, VMUL(ti12, tr4))};
            v4sf ci5{VMADD(ti11, ti5, VMUL(ti12, ti4))};
            v4sf cr4{VSUB(VMUL(ti12, tr5), VMUL(ti11, tr4))};
            v4sf ci4{VSUB(VMUL(ti12, ti5), VMUL(ti11, ti4))};
            v4sf dr3{VSUB(cr3, ci4)};
            v4sf dr4{VADD(cr3, ci4)};
            v4sf di3{VADD(ci3, cr4)};
            v4sf di4{VSUB(ci3, cr4)};
            v4sf dr5{VADD(cr2, ci5)};
            v4sf dr2{VSUB(cr2, ci5)};
            v4sf di5{VSUB(ci2, cr5)};
            v4sf di2{VADD(ci2, cr5)};
            vcplxmul(dr2, di2, LD_PS1(wa1[i-3]), LD_PS1(wa1[i-2]));
            vcplxmul(dr3, di3, LD_PS1(wa2[i-3]), LD_PS1(wa2[i-2]));
            vcplxmul(dr4, di4, LD_PS1(wa3[i-3]), LD_PS1(wa3[i-2]));
            vcplxmul(dr5, di5, LD_PS1(wa4[i-3]), LD_PS1(wa4[i-2]));

            ch_ref(i-1, k, 2) = dr2; ch_ref(i, k, 2) = di2;
            ch_ref(i-1, k, 3) = dr3; ch_ref(i, k, 3) = di3;
            ch_ref(i-1, k, 4) = dr4; ch_ref(i, k, 4) = di4;
            ch_ref(i-1, k, 5) = dr5; ch_ref(i, k, 5) = di5;
        }
    }
#undef cc_ref
#undef ch_ref
} /* radb5 */

NOINLINE v4sf *rfftf1_ps(const size_t n, const v4sf *input_readonly, v4sf *work1, v4sf *work2,
    const float *wa, const al::span<const uint,15> ifac)
{
    assert(work1 != work2);

    const v4sf *in{input_readonly};
    v4sf *out{in == work2 ? work1 : work2};
    const size_t nf{ifac[1]};
    size_t l2{n};
    size_t iw{n-1};
    for(size_t k1{1};k1 <= nf;++k1)
    {
        size_t kh{nf - k1};
        size_t ip{ifac[kh + 2]};
        size_t l1{l2 / ip};
        size_t ido{n / l2};
        iw -= (ip - 1)*ido;
        switch(ip)
        {
        case 5:
            {
                size_t ix2{iw + ido};
                size_t ix3{ix2 + ido};
                size_t ix4{ix3 + ido};
                radf5_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4]);
            }
            break;
        case 4:
            {
                size_t ix2{iw + ido};
                size_t ix3{ix2 + ido};
                radf4_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3]);
            }
            break;
        case 3:
            {
                size_t ix2{iw + ido};
                radf3_ps(ido, l1, in, out, &wa[iw], &wa[ix2]);
            }
            break;
        case 2:
            radf2_ps(ido, l1, in, out, &wa[iw]);
            break;
        default:
            assert(0);
            break;
        }
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
    return const_cast<v4sf*>(in); /* this is in fact the output .. */
} /* rfftf1 */

NOINLINE v4sf *rfftb1_ps(const size_t n, const v4sf *input_readonly, v4sf *work1, v4sf *work2,
    const float *wa, const al::span<const uint,15> ifac)
{
    assert(work1 != work2);

    const v4sf *in{input_readonly};
    v4sf *out{in == work2 ? work1 : work2};
    const size_t nf{ifac[1]};
    size_t l1{1};
    size_t iw{0};
    for(size_t k1{1};k1 <= nf;++k1)
    {
        size_t ip{ifac[k1 + 1]};
        size_t l2{ip*l1};
        size_t ido{n / l2};
        switch(ip)
        {
        case 5:
            {
                size_t ix2{iw + ido};
                size_t ix3{ix2 + ido};
                size_t ix4{ix3 + ido};
                radb5_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4]);
            }
            break;
        case 4:
            {
                size_t ix2{iw + ido};
                size_t ix3{ix2 + ido};
                radb4_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3]);
            }
            break;
        case 3:
            {
                size_t ix2{iw + ido};
                radb3_ps(ido, l1, in, out, &wa[iw], &wa[ix2]);
            }
            break;
        case 2:
            radb2_ps(ido, l1, in, out, &wa[iw]);
            break;
        default:
            assert(0);
            break;
        }
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
    return const_cast<v4sf*>(in); /* this is in fact the output .. */
}

v4sf *cfftf1_ps(const size_t n, const v4sf *input_readonly, v4sf *work1, v4sf *work2,
    const float *wa, const al::span<const uint,15> ifac, const float fsign)
{
    assert(work1 != work2);

    const v4sf *in{input_readonly};
    v4sf *out{in == work2 ? work1 : work2};
    const size_t nf{ifac[1]};
    size_t l1{1}, iw{0};
    for(size_t k1{2};k1 <= nf+1;++k1)
    {
        const size_t ip{ifac[k1]};
        const size_t l2{ip*l1};
        const size_t ido{n / l2};
        const size_t idot{ido + ido};
        switch(ip)
        {
        case 5:
            {
                size_t ix2{iw + idot};
                size_t ix3{ix2 + idot};
                size_t ix4{ix3 + idot};
                passf5_ps(idot, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4], fsign);
            }
            break;
        case 4:
            {
                size_t ix2{iw + idot};
                size_t ix3{ix2 + idot};
                passf4_ps(idot, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], fsign);
            }
            break;
        case 3:
            {
                size_t ix2{iw + idot};
                passf3_ps(idot, l1, in, out, &wa[iw], &wa[ix2], fsign);
            }
            break;
        case 2:
            passf2_ps(idot, l1, in, out, &wa[iw], fsign);
            break;
        default:
            assert(0);
        }
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

    return const_cast<v4sf*>(in); /* this is in fact the output .. */
}


uint decompose(const uint n, const al::span<uint,15> ifac, const al::span<const uint,4> ntryh)
{
    uint nl{n}, nf{0};
    for(const uint ntry : ntryh)
    {
        while(nl != 1)
        {
            const uint nq{nl / ntry};
            const uint nr{nl % ntry};
            if(nr != 0) break;

            ifac[2+nf++] = ntry;
            nl = nq;
            if(ntry == 2 && nf != 1)
            {
                for(size_t i{2};i <= nf;++i)
                {
                    size_t ib{nf - i + 2};
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

void rffti1_ps(const uint n, float *wa, const al::span<uint,15> ifac)
{
    static constexpr uint ntryh[]{4,2,3,5};

    const uint nf{decompose(n, ifac, ntryh)};
    const double argh{2.0*al::numbers::pi / n};
    size_t is{0};
    size_t nfm1{nf - 1};
    size_t l1{1};
    for(size_t k1{0};k1 < nfm1;++k1)
    {
        const size_t ip{ifac[k1+2]};
        const size_t l2{l1*ip};
        const size_t ido{n / l2};
        const size_t ipm{ip - 1};
        size_t ld{0};
        for(size_t j{0};j < ipm;++j)
        {
            size_t i{is};
            ld += l1;
            const double argld{static_cast<double>(ld)*argh};
            double fi{0.0};
            for(size_t ii{2};ii < ido;ii += 2)
            {
                fi += 1.0;
                wa[i++] = static_cast<float>(std::cos(fi*argld));
                wa[i++] = static_cast<float>(std::sin(fi*argld));
            }
            is += ido;
        }
        l1 = l2;
    }
} /* rffti1 */

void cffti1_ps(const uint n, float *wa, const al::span<uint,15> ifac)
{
    static constexpr uint ntryh[]{5,3,4,2};

    const uint nf{decompose(n, ifac, ntryh)};
    const double argh{2.0*al::numbers::pi / n};
    size_t i{1};
    size_t l1{1};
    for(size_t k1{0};k1 < nf;++k1)
    {
        const size_t ip{ifac[k1+2]};
        const size_t l2{l1*ip};
        const size_t ido{n / l2};
        const size_t idot{ido + ido + 2};
        const size_t ipm{ip - 1};
        size_t ld{0};
        for(size_t j{0};j < ipm;++j)
        {
            size_t i1{i};
            wa[i-1] = 1;
            wa[i] = 0;
            ld += l1;
            const double argld{static_cast<double>(ld)*argh};
            double fi{0.0};
            for(size_t ii{3};ii < idot;ii += 2)
            {
                fi += 1.0;
                wa[++i] = static_cast<float>(std::cos(fi*argld));
                wa[++i] = static_cast<float>(std::sin(fi*argld));
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

void *pffft_aligned_malloc(size_t nb_bytes)
{ return al_malloc(MALLOC_V4SF_ALIGNMENT, nb_bytes); }

void pffft_aligned_free(void *p) { al_free(p); }

int pffft_simd_size() { return SIMD_SZ; }

struct PFFFT_Setup {
    uint N;
    uint Ncvec; // nb of complex simd vectors (N/4 if PFFFT_COMPLEX, N/8 if PFFFT_REAL)
    std::array<uint,15> ifac;
    pffft_transform_t transform;

    float *twiddle; // N/4 elements
    alignas(MALLOC_V4SF_ALIGNMENT) v4sf e[1]; // N/4*3 elements
};

PFFFT_Setup *pffft_new_setup(unsigned int N, pffft_transform_t transform)
{
    assert(transform == PFFFT_REAL || transform == PFFFT_COMPLEX);
    assert(N > 0);
    /* unfortunately, the fft size must be a multiple of 16 for complex FFTs
     * and 32 for real FFTs -- a lot of stuff would need to be rewritten to
     * handle other cases (or maybe just switch to a scalar fft, I don't know..)
     */
    if(transform == PFFFT_REAL)
        assert((N%(2*SIMD_SZ*SIMD_SZ)) == 0);
    else
        assert((N%(SIMD_SZ*SIMD_SZ)) == 0);

    const uint Ncvec = (transform == PFFFT_REAL ? N/2 : N)/SIMD_SZ;
    const size_t storelen{offsetof(PFFFT_Setup, e[0]) + (2u*Ncvec * sizeof(v4sf))};

    void *store{al_calloc(MALLOC_V4SF_ALIGNMENT, storelen)};
    if(!store) return nullptr;

    PFFFT_Setup *s{::new(store) PFFFT_Setup{}};
    s->N = N;
    s->transform = transform;
    /* nb of complex simd vectors */
    s->Ncvec = Ncvec;
    s->twiddle = reinterpret_cast<float*>(&s->e[2u*Ncvec*(SIMD_SZ-1)/SIMD_SZ]);

    if constexpr(SIMD_SZ > 1)
    {
        auto e = std::vector<float>(2u*Ncvec*(SIMD_SZ-1), 0.0f);
        for(size_t k{0};k < s->Ncvec;++k)
        {
            const size_t i{k / SIMD_SZ};
            const size_t j{k % SIMD_SZ};
            for(size_t m{0};m < SIMD_SZ-1;++m)
            {
                const double A{-2.0*al::numbers::pi*static_cast<double>((m+1)*k) / N};
                e[((i*3 + m)*2 + 0)*SIMD_SZ + j] = static_cast<float>(std::cos(A));
                e[((i*3 + m)*2 + 1)*SIMD_SZ + j] = static_cast<float>(std::sin(A));
            }
        }
        std::memcpy(s->e, e.data(), e.size()*sizeof(float));
    }
    if(transform == PFFFT_REAL)
        rffti1_ps(N/SIMD_SZ, s->twiddle, s->ifac);
    else
        cffti1_ps(N/SIMD_SZ, s->twiddle, s->ifac);

    /* check that N is decomposable with allowed prime factors */
    size_t m{1};
    for(size_t k{0};k < s->ifac[1];++k)
        m *= s->ifac[2+k];

    if(m != N/SIMD_SZ)
    {
        pffft_destroy_setup(s);
        s = nullptr;
    }

    return s;
}


void pffft_destroy_setup(PFFFT_Setup *s)
{
    std::destroy_at(s);
    al_free(s);
}

#if !defined(PFFFT_SIMD_DISABLE)

namespace {

/* [0 0 1 2 3 4 5 6 7 8] -> [0 8 7 6 5 4 3 2 1] */
void reversed_copy(const size_t N, const v4sf *in, const int in_stride, v4sf *RESTRICT out)
{
    v4sf g0, g1;
    interleave2(in[0], in[1], g0, g1);
    in += in_stride;

    *--out = VSWAPHL(g0, g1); // [g0l, g0h], [g1l g1h] -> [g1l, g0h]
    for(size_t k{1};k < N;++k)
    {
        v4sf h0, h1;
        interleave2(in[0], in[1], h0, h1);
        in += in_stride;
        *--out = VSWAPHL(g1, h0);
        *--out = VSWAPHL(h0, h1);
        g1 = h1;
    }
    *--out = VSWAPHL(g1, g0);
}

void unreversed_copy(const size_t N, const v4sf *in, v4sf *RESTRICT out, const int out_stride)
{
    v4sf g0{in[0]}, g1{g0};
    ++in;
    for(size_t k{1};k < N;++k)
    {
        v4sf h0{*in++}; v4sf h1{*in++};
        g1 = VSWAPHL(g1, h0);
        h0 = VSWAPHL(h0, h1);
        uninterleave2(h0, g1, out[0], out[1]);
        out += out_stride;
        g1 = h1;
    }
    v4sf h0{*in++}, h1{g0};
    g1 = VSWAPHL(g1, h0);
    h0 = VSWAPHL(h0, h1);
    uninterleave2(h0, g1, out[0], out[1]);
}

void pffft_cplx_finalize(const size_t Ncvec, const v4sf *in, v4sf *RESTRICT out, const v4sf *e)
{
    assert(in != out);

    const size_t dk{Ncvec/SIMD_SZ}; // number of 4x4 matrix blocks
    for(size_t k{0};k < dk;++k)
    {
        v4sf r0{in[8*k+0]}, i0{in[8*k+1]};
        v4sf r1{in[8*k+2]}, i1{in[8*k+3]};
        v4sf r2{in[8*k+4]}, i2{in[8*k+5]};
        v4sf r3{in[8*k+6]}, i3{in[8*k+7]};
        vtranspose4(r0,r1,r2,r3);
        vtranspose4(i0,i1,i2,i3);
        vcplxmul(r1,i1,e[k*6+0],e[k*6+1]);
        vcplxmul(r2,i2,e[k*6+2],e[k*6+3]);
        vcplxmul(r3,i3,e[k*6+4],e[k*6+5]);

        v4sf sr0{VADD(r0,r2)}, dr0{VSUB(r0, r2)};
        v4sf sr1{VADD(r1,r3)}, dr1{VSUB(r1, r3)};
        v4sf si0{VADD(i0,i2)}, di0{VSUB(i0, i2)};
        v4sf si1{VADD(i1,i3)}, di1{VSUB(i1, i3)};

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

        r0 = VADD(sr0, sr1); i0 = VADD(si0, si1);
        r1 = VADD(dr0, di1); i1 = VSUB(di0, dr1);
        r2 = VSUB(sr0, sr1); i2 = VSUB(si0, si1);
        r3 = VSUB(dr0, di1); i3 = VADD(di0, dr1);

        *out++ = r0; *out++ = i0; *out++ = r1; *out++ = i1;
        *out++ = r2; *out++ = i2; *out++ = r3; *out++ = i3;
    }
}

void pffft_cplx_preprocess(const size_t Ncvec, const v4sf *in, v4sf *RESTRICT out, const v4sf *e)
{
    assert(in != out);

    const size_t dk{Ncvec/SIMD_SZ}; // number of 4x4 matrix blocks
    for(size_t k{0};k < dk;++k)
    {
        v4sf r0{in[8*k+0]}, i0{in[8*k+1]};
        v4sf r1{in[8*k+2]}, i1{in[8*k+3]};
        v4sf r2{in[8*k+4]}, i2{in[8*k+5]};
        v4sf r3{in[8*k+6]}, i3{in[8*k+7]};

        v4sf sr0{VADD(r0,r2)}, dr0{VSUB(r0, r2)};
        v4sf sr1{VADD(r1,r3)}, dr1{VSUB(r1, r3)};
        v4sf si0{VADD(i0,i2)}, di0{VSUB(i0, i2)};
        v4sf si1{VADD(i1,i3)}, di1{VSUB(i1, i3)};

        r0 = VADD(sr0, sr1); i0 = VADD(si0, si1);
        r1 = VSUB(dr0, di1); i1 = VADD(di0, dr1);
        r2 = VSUB(sr0, sr1); i2 = VSUB(si0, si1);
        r3 = VADD(dr0, di1); i3 = VSUB(di0, dr1);

        vcplxmulconj(r1,i1,e[k*6+0],e[k*6+1]);
        vcplxmulconj(r2,i2,e[k*6+2],e[k*6+3]);
        vcplxmulconj(r3,i3,e[k*6+4],e[k*6+5]);

        vtranspose4(r0,r1,r2,r3);
        vtranspose4(i0,i1,i2,i3);

        *out++ = r0; *out++ = i0; *out++ = r1; *out++ = i1;
        *out++ = r2; *out++ = i2; *out++ = r3; *out++ = i3;
    }
}


force_inline void pffft_real_finalize_4x4(const v4sf *in0, const v4sf *in1, const v4sf *in,
    const v4sf *e, v4sf *RESTRICT out)
{
    v4sf r0{*in0}, i0{*in1};
    v4sf r1{*in++}; v4sf i1{*in++};
    v4sf r2{*in++}; v4sf i2{*in++};
    v4sf r3{*in++}; v4sf i3{*in++};
    vtranspose4(r0,r1,r2,r3);
    vtranspose4(i0,i1,i2,i3);

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

    vcplxmul(r1,i1,e[0],e[1]);
    vcplxmul(r2,i2,e[2],e[3]);
    vcplxmul(r3,i3,e[4],e[5]);

    //cerr << "matrix initial, real part:\n 1: " << r0 << "\n 1: " << r1 << "\n 1: " << r2 << "\n 1: " << r3 << "\n";
    //cerr << "matrix initial, imag part:\n 1: " << i0 << "\n 1: " << i1 << "\n 1: " << i2 << "\n 1: " << i3 << "\n";

    v4sf sr0{VADD(r0,r2)}, dr0{VSUB(r0,r2)};
    v4sf sr1{VADD(r1,r3)}, dr1{VSUB(r3,r1)};
    v4sf si0{VADD(i0,i2)}, di0{VSUB(i0,i2)};
    v4sf si1{VADD(i1,i3)}, di1{VSUB(i3,i1)};

    r0 = VADD(sr0, sr1);
    r3 = VSUB(sr0, sr1);
    i0 = VADD(si0, si1);
    i3 = VSUB(si1, si0);
    r1 = VADD(dr0, di1);
    r2 = VSUB(dr0, di1);
    i1 = VSUB(dr1, di0);
    i2 = VADD(dr1, di0);

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
    static constexpr float s{al::numbers::sqrt2_v<float>/2.0f};

    assert(in != out);
    const size_t dk{Ncvec/SIMD_SZ}; // number of 4x4 matrix blocks
    /* fftpack order is f0r f1r f1i f2r f2i ... f(n-1)r f(n-1)i f(n)r */

    const v4sf zero{VZERO()};
    const auto cr = al::bit_cast<std::array<float,SIMD_SZ>>(in[0]);
    const auto ci = al::bit_cast<std::array<float,SIMD_SZ>>(in[Ncvec*2-1]);
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

    const float xr0{(cr[0]+cr[2]) + (cr[1]+cr[3])}; out[0] = VINSERT0(out[0], xr0);
    const float xi0{(cr[0]+cr[2]) - (cr[1]+cr[3])}; out[1] = VINSERT0(out[1], xi0);
    const float xr2{(cr[0]-cr[2])};                 out[4] = VINSERT0(out[4], xr2);
    const float xi2{(cr[3]-cr[1])};                 out[5] = VINSERT0(out[5], xi2);
    const float xr1{ ci[0] + s*(ci[1]-ci[3])};      out[2] = VINSERT0(out[2], xr1);
    const float xi1{-ci[2] - s*(ci[1]+ci[3])};      out[3] = VINSERT0(out[3], xi1);
    const float xr3{ ci[0] - s*(ci[1]-ci[3])};      out[6] = VINSERT0(out[6], xr3);
    const float xi3{ ci[2] - s*(ci[1]+ci[3])};      out[7] = VINSERT0(out[7], xi3);

    for(size_t k{1};k < dk;++k)
        pffft_real_finalize_4x4(&in[8*k-1], &in[8*k+0], in + 8*k+1, e + k*6, out + k*8);
}

force_inline void pffft_real_preprocess_4x4(const v4sf *in, const v4sf *e, v4sf *RESTRICT out,
    const bool first)
{
    v4sf r0{in[0]}, i0{in[1]}, r1{in[2]}, i1{in[3]};
    v4sf r2{in[4]}, i2{in[5]}, r3{in[6]}, i3{in[7]};

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

    v4sf sr0{VADD(r0,r3)}, dr0{VSUB(r0,r3)};
    v4sf sr1{VADD(r1,r2)}, dr1{VSUB(r1,r2)};
    v4sf si0{VADD(i0,i3)}, di0{VSUB(i0,i3)};
    v4sf si1{VADD(i1,i2)}, di1{VSUB(i1,i2)};

    r0 = VADD(sr0, sr1);
    r2 = VSUB(sr0, sr1);
    r1 = VSUB(dr0, si1);
    r3 = VADD(dr0, si1);
    i0 = VSUB(di0, di1);
    i2 = VADD(di0, di1);
    i1 = VSUB(si0, dr1);
    i3 = VADD(si0, dr1);

    vcplxmulconj(r1,i1,e[0],e[1]);
    vcplxmulconj(r2,i2,e[2],e[3]);
    vcplxmulconj(r3,i3,e[4],e[5]);

    vtranspose4(r0,r1,r2,r3);
    vtranspose4(i0,i1,i2,i3);

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
    static constexpr float sqrt2{al::numbers::sqrt2_v<float>};

    assert(in != out);
    const size_t dk{Ncvec/SIMD_SZ}; // number of 4x4 matrix blocks
    /* fftpack order is f0r f1r f1i f2r f2i ... f(n-1)r f(n-1)i f(n)r */

    std::array<float,SIMD_SZ> Xr, Xi;
    for(size_t k{0};k < SIMD_SZ;++k)
    {
        Xr[k] = VEXTRACT0(in[2*k]);
        Xi[k] = VEXTRACT0(in[2*k + 1]);
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
    for(size_t k{1};k < dk;++k)
        pffft_real_preprocess_4x4(in+8*k, e + k*6, out-1+k*8, false);

    const float cr0{(Xr[0]+Xi[0]) + 2*Xr[2]};
    const float cr1{(Xr[0]-Xi[0]) - 2*Xi[2]};
    const float cr2{(Xr[0]+Xi[0]) - 2*Xr[2]};
    const float cr3{(Xr[0]-Xi[0]) + 2*Xi[2]};
    out[0] = VSET4(cr0, cr1, cr2, cr3);
    const float ci0{     2*(Xr[1]+Xr[3])};
    const float ci1{ sqrt2*(Xr[1]-Xr[3]) - sqrt2*(Xi[1]+Xi[3])};
    const float ci2{     2*(Xi[3]-Xi[1])};
    const float ci3{-sqrt2*(Xr[1]-Xr[3]) - sqrt2*(Xi[1]+Xi[3])};
    out[2*Ncvec-1] = VSET4(ci0, ci1, ci2, ci3);
}


void pffft_transform_internal(const PFFFT_Setup *setup, const v4sf *vinput, v4sf *voutput,
    v4sf *scratch, const pffft_direction_t direction, const bool ordered)
{
    assert(scratch != nullptr);
    assert(voutput != scratch);

    const size_t Ncvec{setup->Ncvec};
    const bool nf_odd{(setup->ifac[1]&1) != 0};

    v4sf *buff[2]{voutput, scratch};
    bool ib{nf_odd != ordered};
    if(direction == PFFFT_FORWARD)
    {
        /* Swap the initial work buffer for forward FFTs, which helps avoid an
         * extra copy for output.
         */
        ib = !ib;
        if(setup->transform == PFFFT_REAL)
        {
            ib = (rfftf1_ps(Ncvec*2, vinput, buff[ib], buff[!ib], setup->twiddle, setup->ifac) == buff[1]);
            pffft_real_finalize(Ncvec, buff[ib], buff[!ib], setup->e);
        }
        else
        {
            v4sf *tmp{buff[ib]};
            for(size_t k=0; k < Ncvec; ++k)
                uninterleave2(vinput[k*2], vinput[k*2+1], tmp[k*2], tmp[k*2+1]);

            ib = (cfftf1_ps(Ncvec, buff[ib], buff[!ib], buff[ib], setup->twiddle, setup->ifac, -1.0f) == buff[1]);
            pffft_cplx_finalize(Ncvec, buff[ib], buff[!ib], setup->e);
        }
        if(ordered)
            pffft_zreorder(setup, reinterpret_cast<float*>(buff[!ib]),
                reinterpret_cast<float*>(buff[ib]), PFFFT_FORWARD);
        else
            ib = !ib;
    }
    else
    {
        if(vinput == buff[ib])
            ib = !ib; // may happen when finput == foutput

        if(ordered)
        {
            pffft_zreorder(setup, reinterpret_cast<const float*>(vinput),
                reinterpret_cast<float*>(buff[ib]), PFFFT_BACKWARD);
            vinput = buff[ib];
            ib = !ib;
        }
        if(setup->transform == PFFFT_REAL)
        {
            pffft_real_preprocess(Ncvec, vinput, buff[ib], setup->e);
            ib = (rfftb1_ps(Ncvec*2, buff[ib], buff[0], buff[1], setup->twiddle, setup->ifac) == buff[1]);
        }
        else
        {
            pffft_cplx_preprocess(Ncvec, vinput, buff[ib], setup->e);
            ib = (cfftf1_ps(Ncvec, buff[ib], buff[0], buff[1],  setup->twiddle, setup->ifac, +1.0f) == buff[1]);
            for(size_t k{0};k < Ncvec;++k)
                interleave2(buff[ib][k*2], buff[ib][k*2+1], buff[ib][k*2], buff[ib][k*2+1]);
        }
    }

    if(buff[ib] != voutput)
    {
        /* extra copy required -- this situation should only happen when finput == foutput */
        assert(vinput==voutput);
        for(size_t k{0};k < Ncvec;++k)
        {
            v4sf a{buff[ib][2*k]}, b{buff[ib][2*k+1]};
            voutput[2*k] = a; voutput[2*k+1] = b;
        }
    }
}

} // namespace

void pffft_zreorder(const PFFFT_Setup *setup, const float *in, float *out,
    pffft_direction_t direction)
{
    assert(in != out);

    const size_t N{setup->N}, Ncvec{setup->Ncvec};
    const v4sf *vin{reinterpret_cast<const v4sf*>(in)};
    v4sf *RESTRICT vout{reinterpret_cast<v4sf*>(out)};
    if(setup->transform == PFFFT_REAL)
    {
        const size_t dk{N/32};
        if(direction == PFFFT_FORWARD)
        {
            for(size_t k{0};k < dk;++k)
            {
                interleave2(vin[k*8 + 0], vin[k*8 + 1], vout[2*(0*dk + k) + 0], vout[2*(0*dk + k) + 1]);
                interleave2(vin[k*8 + 4], vin[k*8 + 5], vout[2*(2*dk + k) + 0], vout[2*(2*dk + k) + 1]);
            }
            reversed_copy(dk, vin+2, 8, vout + N/SIMD_SZ/2);
            reversed_copy(dk, vin+6, 8, vout + N/SIMD_SZ);
        }
        else
        {
            for(size_t k{0};k < dk;++k)
            {
                uninterleave2(vin[2*(0*dk + k) + 0], vin[2*(0*dk + k) + 1], vout[k*8 + 0], vout[k*8 + 1]);
                uninterleave2(vin[2*(2*dk + k) + 0], vin[2*(2*dk + k) + 1], vout[k*8 + 4], vout[k*8 + 5]);
            }
            unreversed_copy(dk, vin + N/SIMD_SZ/4, vout + N/SIMD_SZ - 6, -8);
            unreversed_copy(dk, vin + 3*N/SIMD_SZ/4, vout + N/SIMD_SZ - 2, -8);
        }
    }
    else
    {
        if(direction == PFFFT_FORWARD)
        {
            for(size_t k{0};k < Ncvec;++k)
            {
                size_t kk{(k/4) + (k%4)*(Ncvec/4)};
                interleave2(vin[k*2], vin[k*2+1], vout[kk*2], vout[kk*2+1]);
            }
        }
        else
        {
            for(size_t k{0};k < Ncvec;++k)
            {
                size_t kk{(k/4) + (k%4)*(Ncvec/4)};
                uninterleave2(vin[kk*2], vin[kk*2+1], vout[k*2], vout[k*2+1]);
            }
        }
    }
}

void pffft_zconvolve_scale_accumulate(const PFFFT_Setup *s, const float *a, const float *b,
    float *ab, float scaling)
{
    const size_t Ncvec{s->Ncvec};
    const v4sf *RESTRICT va{reinterpret_cast<const v4sf*>(a)};
    const v4sf *RESTRICT vb{reinterpret_cast<const v4sf*>(b)};
    v4sf *RESTRICT vab{reinterpret_cast<v4sf*>(ab)};

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
#ifndef __clang__
#define ZCONVOLVE_USING_INLINE_NEON_ASM
#endif
#endif

    const float ar1{VEXTRACT0(va[0])};
    const float ai1{VEXTRACT0(va[1])};
    const float br1{VEXTRACT0(vb[0])};
    const float bi1{VEXTRACT0(vb[1])};
    const float abr1{VEXTRACT0(vab[0])};
    const float abi1{VEXTRACT0(vab[1])};

#ifdef ZCONVOLVE_USING_INLINE_ASM
    /* Inline asm version, unfortunately miscompiled by clang 3.2, at least on
     * Ubuntu. So this will be restricted to GCC.
     *
     * Does it still miscompile with Clang? Is it even needed with today's
     * optimizers?
     */
    const float *a_{a}, *b_{b}; float *ab_{ab};
    size_t N{Ncvec};
    asm volatile("mov         r8, %2                  \n"
                "vdup.f32    q15, %4                 \n"
                "1:                                  \n"
                "pld         [%0,#64]                \n"
                "pld         [%1,#64]                \n"
                "pld         [%2,#64]                \n"
                "pld         [%0,#96]                \n"
                "pld         [%1,#96]                \n"
                "pld         [%2,#96]                \n"
                "vld1.f32    {q0,q1},   [%0,:128]!         \n"
                "vld1.f32    {q4,q5},   [%1,:128]!         \n"
                "vld1.f32    {q2,q3},   [%0,:128]!         \n"
                "vld1.f32    {q6,q7},   [%1,:128]!         \n"
                "vld1.f32    {q8,q9},   [r8,:128]!          \n"

                "vmul.f32    q10, q0, q4             \n"
                "vmul.f32    q11, q0, q5             \n"
                "vmul.f32    q12, q2, q6             \n"
                "vmul.f32    q13, q2, q7             \n"
                "vmls.f32    q10, q1, q5             \n"
                "vmla.f32    q11, q1, q4             \n"
                "vld1.f32    {q0,q1}, [r8,:128]!     \n"
                "vmls.f32    q12, q3, q7             \n"
                "vmla.f32    q13, q3, q6             \n"
                "vmla.f32    q8, q10, q15            \n"
                "vmla.f32    q9, q11, q15            \n"
                "vmla.f32    q0, q12, q15            \n"
                "vmla.f32    q1, q13, q15            \n"
                "vst1.f32    {q8,q9},[%2,:128]!    \n"
                "vst1.f32    {q0,q1},[%2,:128]!    \n"
                "subs        %3, #2                  \n"
                "bne         1b                      \n"
                : "+r"(a_), "+r"(b_), "+r"(ab_), "+r"(N) : "r"(scaling) : "r8", "q0","q1","q2","q3","q4","q5","q6","q7","q8","q9", "q10","q11","q12","q13","q15","memory");

#else

    /* Default routine, works fine for non-arm cpus with current compilers. */
    const v4sf vscal{LD_PS1(scaling)};
    for(size_t i{0};i < Ncvec;i += 2)
    {
        v4sf ar4{va[2*i+0]}, ai4{va[2*i+1]};
        v4sf br4{vb[2*i+0]}, bi4{vb[2*i+1]};
        vcplxmul(ar4, ai4, br4, bi4);
        vab[2*i+0] = VMADD(ar4, vscal, vab[2*i+0]);
        vab[2*i+1] = VMADD(ai4, vscal, vab[2*i+1]);
        ar4 = va[2*i+2]; ai4 = va[2*i+3];
        br4 = vb[2*i+2]; bi4 = vb[2*i+3];
        vcplxmul(ar4, ai4, br4, bi4);
        vab[2*i+2] = VMADD(ar4, vscal, vab[2*i+2]);
        vab[2*i+3] = VMADD(ai4, vscal, vab[2*i+3]);
    }
#endif

    if(s->transform == PFFFT_REAL)
    {
        vab[0] = VINSERT0(vab[0], abr1 + ar1*br1*scaling);
        vab[1] = VINSERT0(vab[1], abi1 + ai1*bi1*scaling);
    }
}

void pffft_zconvolve_accumulate(const PFFFT_Setup *s, const float *a, const float *b, float *ab)
{
    const size_t Ncvec{s->Ncvec};
    const v4sf *RESTRICT va{reinterpret_cast<const v4sf*>(a)};
    const v4sf *RESTRICT vb{reinterpret_cast<const v4sf*>(b)};
    v4sf *RESTRICT vab{reinterpret_cast<v4sf*>(ab)};

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

    const float ar1{VEXTRACT0(va[0])};
    const float ai1{VEXTRACT0(va[1])};
    const float br1{VEXTRACT0(vb[0])};
    const float bi1{VEXTRACT0(vb[1])};
    const float abr1{VEXTRACT0(vab[0])};
    const float abi1{VEXTRACT0(vab[1])};

    /* No inline assembly for this version. I'm not familiar enough with NEON
     * assembly, and I don't know that it's needed with today's optimizers.
     */
    for(size_t i{0};i < Ncvec;i += 2)
    {
        v4sf ar4{va[2*i+0]}, ai4{va[2*i+1]};
        v4sf br4{vb[2*i+0]}, bi4{vb[2*i+1]};
        vcplxmul(ar4, ai4, br4, bi4);
        vab[2*i+0] = VADD(ar4, vab[2*i+0]);
        vab[2*i+1] = VADD(ai4, vab[2*i+1]);
        ar4 = va[2*i+2]; ai4 = va[2*i+3];
        br4 = vb[2*i+2]; bi4 = vb[2*i+3];
        vcplxmul(ar4, ai4, br4, bi4);
        vab[2*i+2] = VADD(ar4, vab[2*i+2]);
        vab[2*i+3] = VADD(ai4, vab[2*i+3]);
    }

    if(s->transform == PFFFT_REAL)
    {
        vab[0] = VINSERT0(vab[0], abr1 + ar1*br1);
        vab[1] = VINSERT0(vab[1], abi1 + ai1*bi1);
    }
}


void pffft_transform(const PFFFT_Setup *setup, const float *input, float *output, float *work,
    pffft_direction_t direction)
{
    assert(valigned(input) && valigned(output) && valigned(work));
    pffft_transform_internal(setup, reinterpret_cast<const v4sf*>(al::assume_aligned<16>(input)),
        reinterpret_cast<v4sf*>(al::assume_aligned<16>(output)),
        reinterpret_cast<v4sf*>(al::assume_aligned<16>(work)), direction, false);
}

void pffft_transform_ordered(const PFFFT_Setup *setup, const float *input, float *output,
    float *work, pffft_direction_t direction)
{
    assert(valigned(input) && valigned(output) && valigned(work));
    pffft_transform_internal(setup, reinterpret_cast<const v4sf*>(al::assume_aligned<16>(input)),
        reinterpret_cast<v4sf*>(al::assume_aligned<16>(output)),
        reinterpret_cast<v4sf*>(al::assume_aligned<16>(work)), direction, true);
}

#else // defined(PFFFT_SIMD_DISABLE)

// standard routine using scalar floats, without SIMD stuff.

namespace {

#define pffft_transform_internal_nosimd pffft_transform_internal
void pffft_transform_internal_nosimd(const PFFFT_Setup *setup, const float *input, float *output,
    float *scratch, const pffft_direction_t direction, bool ordered)
{
    const size_t Ncvec{setup->Ncvec};
    const bool nf_odd{(setup->ifac[1]&1) != 0};

    assert(scratch != nullptr);

    /* z-domain data for complex transforms is already ordered without SIMD. */
    if(setup->transform == PFFFT_COMPLEX)
        ordered = false;

    float *buff[2]{output, scratch};
    bool ib{nf_odd != ordered};
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
        if (input == buff[ib])
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
        assert(input==output);
        for(size_t k{0};k < Ncvec;++k)
        {
            float a{buff[ib][2*k]}, b{buff[ib][2*k+1]};
            output[2*k] = a; output[2*k+1] = b;
        }
    }
}

} // namespace

#define pffft_zreorder_nosimd pffft_zreorder
void pffft_zreorder_nosimd(const PFFFT_Setup *setup, const float *in, float *RESTRICT out,
    pffft_direction_t direction)
{
    const size_t N{setup->N};
    if(setup->transform == PFFFT_COMPLEX)
    {
        for(size_t k{0};k < 2*N;++k)
            out[k] = in[k];
        return;
    }
    else if(direction == PFFFT_FORWARD)
    {
        float x_N{in[N-1]};
        for(size_t k{N-1};k > 1;--k)
            out[k] = in[k-1];
        out[0] = in[0];
        out[1] = x_N;
    }
    else
    {
        float x_N{in[1]};
        for(size_t k{1};k < N-1;++k)
            out[k] = in[k+1];
        out[0] = in[0];
        out[N-1] = x_N;
    }
}

void pffft_zconvolve_scale_accumulate(const PFFFT_Setup *s, const float *a, const float *b,
    float *ab, float scaling)
{
    size_t Ncvec{s->Ncvec};

    if(s->transform == PFFFT_REAL)
    {
        // take care of the fftpack ordering
        ab[0] += a[0]*b[0]*scaling;
        ab[2*Ncvec-1] += a[2*Ncvec-1]*b[2*Ncvec-1]*scaling;
        ++ab; ++a; ++b; --Ncvec;
    }
    for(size_t i{0};i < Ncvec;++i)
    {
        float ar{a[2*i+0]}, ai{a[2*i+1]};
        const float br{b[2*i+0]}, bi{b[2*i+1]};
        vcplxmul(ar, ai, br, bi);
        ab[2*i+0] += ar*scaling;
        ab[2*i+1] += ai*scaling;
    }
}

void pffft_zconvolve_accumulate(const PFFFT_Setup *s, const float *a, const float *b, float *ab)
{
    size_t Ncvec{s->Ncvec};

    if(s->transform == PFFFT_REAL)
    {
        // take care of the fftpack ordering
        ab[0] += a[0]*b[0];
        ab[2*Ncvec-1] += a[2*Ncvec-1]*b[2*Ncvec-1];
        ++ab; ++a; ++b; --Ncvec;
    }
    for(size_t i{0};i < Ncvec;++i)
    {
        float ar{a[2*i+0]}, ai{a[2*i+1]};
        const float br{b[2*i+0]}, bi{b[2*i+1]};
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

#endif // defined(PFFFT_SIMD_DISABLE)
