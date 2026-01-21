#ifndef CORE_AMBIDEFS_H
#define CORE_AMBIDEFS_H

#include <array>
#include <numbers>

#include "alnumeric.h"
#include "opthelpers.h"


/* The maximum number of Ambisonics channels. For a given order (o), the size
 * needed will be (o+1)**2, thus zero-order has 1, first-order has 4, second-
 * order has 9, third-order has 16, and fourth-order has 25.
 */
constexpr auto AmbiChannelsFromOrder(usize const order) noexcept -> usize
{ return (order+1) * (order+1); }

inline constexpr auto MaxAmbiOrder = 4_uz;
inline constexpr auto MaxAmbiChannels = AmbiChannelsFromOrder(MaxAmbiOrder);

/* A bitmask of ambisonic channels for 0 to 4th order. This only specifies up
 * to 4th order, which is the highest order a 32-bit mask value can specify (a
 * 64-bit mask could handle up to 7th order).
 */
inline constexpr auto Ambi0OrderMask = 0x00000001_u32;
inline constexpr auto Ambi1OrderMask = 0x0000000f_u32;
inline constexpr auto Ambi2OrderMask = 0x000001ff_u32;
inline constexpr auto Ambi3OrderMask = 0x0000ffff_u32;
inline constexpr auto Ambi4OrderMask = 0x01ffffff_u32;

/* A bitmask of ambisonic channels with height information. If none of these
 * channels are used/needed, there's no height (e.g. with most surround sound
 * speaker setups). This is ACN ordering, with bit 0 being ACN 0, etc.
 */
inline constexpr auto AmbiPeriphonicMask = 0xfe7ce4_u32;

/* The maximum number of ambisonic channels for 2D (non-periphonic)
 * representation. This is 2 per each order above zero-order, plus 1 for zero-
 * order. Or simply, o*2 + 1.
 */
constexpr auto Ambi2DChannelsFromOrder(usize const order) noexcept -> usize
{ return order*2 + 1; }
inline constexpr auto MaxAmbi2DChannels = Ambi2DChannelsFromOrder(MaxAmbiOrder);


/* NOTE: These are scale factors as applied to Ambisonics content. Decoder
 * coefficients should be divided by these values to get proper scalings.
 */
namespace AmbiScale {
    inline constexpr auto FromN3D = std::array{
        1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    inline constexpr auto FromSN3D = std::array{
        1.000000000f, /* ACN  0, sqrt(1) */
        1.732050808f, /* ACN  1, sqrt(3) */
        1.732050808f, /* ACN  2, sqrt(3) */
        1.732050808f, /* ACN  3, sqrt(3) */
        2.236067978f, /* ACN  4, sqrt(5) */
        2.236067978f, /* ACN  5, sqrt(5) */
        2.236067978f, /* ACN  6, sqrt(5) */
        2.236067978f, /* ACN  7, sqrt(5) */
        2.236067978f, /* ACN  8, sqrt(5) */
        2.645751311f, /* ACN  9, sqrt(7) */
        2.645751311f, /* ACN 10, sqrt(7) */
        2.645751311f, /* ACN 11, sqrt(7) */
        2.645751311f, /* ACN 12, sqrt(7) */
        2.645751311f, /* ACN 13, sqrt(7) */
        2.645751311f, /* ACN 14, sqrt(7) */
        2.645751311f, /* ACN 15, sqrt(7) */
        3.000000000f, /* ACN 16, sqrt(9) */
        3.000000000f, /* ACN 17, sqrt(9) */
        3.000000000f, /* ACN 18, sqrt(9) */
        3.000000000f, /* ACN 19, sqrt(9) */
        3.000000000f, /* ACN 20, sqrt(9) */
        3.000000000f, /* ACN 21, sqrt(9) */
        3.000000000f, /* ACN 22, sqrt(9) */
        3.000000000f, /* ACN 23, sqrt(9) */
        3.000000000f, /* ACN 24, sqrt(9) */
    };
    inline constexpr auto FromFuMa = std::array{
        1.414213562f, /* ACN  0 (W), sqrt(2) */
        1.732050808f, /* ACN  1 (Y), sqrt(3) */
        1.732050808f, /* ACN  2 (Z), sqrt(3) */
        1.732050808f, /* ACN  3 (X), sqrt(3) */
        1.936491673f, /* ACN  4 (V), sqrt(15)/2 */
        1.936491673f, /* ACN  5 (T), sqrt(15)/2 */
        2.236067978f, /* ACN  6 (R), sqrt(5) */
        1.936491673f, /* ACN  7 (S), sqrt(15)/2 */
        1.936491673f, /* ACN  8 (U), sqrt(15)/2 */
        2.091650066f, /* ACN  9 (Q), sqrt(35/8) */
        1.972026594f, /* ACN 10 (O), sqrt(35)/3 */
        2.231093404f, /* ACN 11 (M), sqrt(224/45) */
        2.645751311f, /* ACN 12 (K), sqrt(7) */
        2.231093404f, /* ACN 13 (L), sqrt(224/45) */
        1.972026594f, /* ACN 14 (N), sqrt(35)/3 */
        2.091650066f, /* ACN 15 (P), sqrt(35/8) */
        /* Higher orders not relevant for FuMa. Although maxN (which is what
         * FuMa uses aside from an extra -3dB factor on W) is defined such that
         * any one component never exceeds a gain of 1.0 for a panned mono
         * source, allowing scaling factors to be calculated. But unless and
         * until I hear of software using FuMa in fourth-order and above, or
         * otherwise get confirmation on its accuracy, I don't want to make
         * that assumption.
         *
         * These ratios were calculated by brute-forcing the maximum N3D value
         * (testing various directions to find the largest absolute result),
         * which will be the maxN/FuMa->N3D scaling factor, and then searching
         * for a fraction fitting the square of that factor. There may be more
         * accurate or formal methods to find these values, but that aside,
         * these scalings have less than 1e-8 error from the calculated scaling
         * factor.
         */
        1.0f, /* 2.218529919f, ACN 16, sqrt(315)/8 */
        1.0f, /* 2.037849855f, ACN 17, sqrt(8505/2048) */
        1.0f, /* 2.156208407f, ACN 18, sqrt(3645)/28 */
        1.0f, /* 2.504586102f, ACN 19, sqrt(35599/5675) */
        1.0f, /* 3.000000000f, ACN 20, sqrt(9) */
        1.0f, /* 2.504586102f, ACN 21, sqrt(35599/5675) */
        1.0f, /* 2.156208407f, ACN 22, sqrt(3645)/28 */
        1.0f, /* 2.037849855f, ACN 23, sqrt(8505/2048) */
        1.0f, /* 2.218529919f, ACN 24, sqrt(315)/8 */
    };

    template<usize N>
    using UpsamplerArrays = std::array<std::array<f32, MaxAmbiChannels>, N>;
    DECL_HIDDEN extern constinit UpsamplerArrays<4> const FirstOrderUp;
    DECL_HIDDEN extern constinit UpsamplerArrays<4> const FirstOrder2DUp;
    DECL_HIDDEN extern constinit UpsamplerArrays<9> const SecondOrderUp;
    DECL_HIDDEN extern constinit UpsamplerArrays<9> const SecondOrder2DUp;
    DECL_HIDDEN extern constinit UpsamplerArrays<16> const ThirdOrderUp;
    DECL_HIDDEN extern constinit UpsamplerArrays<16> const ThirdOrder2DUp;
    DECL_HIDDEN extern constinit UpsamplerArrays<25> const FourthOrder2DUp;

    /* Retrieves per-order HF scaling factors for "upsampling" ambisonic data. */
    auto GetHFOrderScales(u32 src_order, u32 dev_order, bool horizontalOnly) noexcept
        -> std::array<f32, MaxAmbiOrder+1>;
} /* namespace AmbiScale */

namespace AmbiIndex {
    inline constexpr auto FromFuMa = std::array{
        0_u8,  /* W */
        3_u8,  /* X */
        1_u8,  /* Y */
        2_u8,  /* Z */
        6_u8,  /* R */
        7_u8,  /* S */
        5_u8,  /* T */
        8_u8,  /* U */
        4_u8,  /* V */
        12_u8, /* K */
        13_u8, /* L */
        11_u8, /* M */
        14_u8, /* N */
        10_u8, /* O */
        15_u8, /* P */
        9_u8,  /* Q */
        /* Higher orders not relevant for FuMa. The previous orders form a
         * pattern suggesting 20,21,19,22,18,23,17,24,16, but as above, unless
         * I hear otherwise, I don't want to make assumptions here.
         */
        0_u8, 0_u8, 0_u8, 0_u8, 0_u8, 0_u8, 0_u8, 0_u8, 0_u8,
    };
    inline constexpr auto FromFuMa2D = std::array{
        0_u8,  /* W */
        3_u8,  /* X */
        1_u8,  /* Y */
        8_u8,  /* U */
        4_u8,  /* V */
        15_u8, /* P */
        9_u8,  /* Q */
        /* Higher orders not relevant for FuMa. Though the previous orders form
         * a pattern suggesting 24,16.
         */
        0_u8, 0_u8,
    };

    inline constexpr auto FromACN = std::array<u8, MaxAmbiChannels>{
        0_u8,
        1_u8, 2_u8, 3_u8,
        4_u8, 5_u8, 6_u8, 7_u8, 8_u8,
        9_u8, 10_u8, 11_u8, 12_u8, 13_u8, 14_u8, 15_u8,
        16_u8, 17_u8, 18_u8, 19_u8, 20_u8, 21_u8, 22_u8, 23_u8, 24_u8,
    };
    inline constexpr auto FromACN2D = std::array<u8, MaxAmbi2DChannels>{
        0_u8, 1_u8,3_u8, 4_u8,8_u8, 9_u8,15_u8, 16_u8,24_u8,
    };


    inline constexpr auto OrderFromChannel = std::array<u8, MaxAmbiChannels>{
        0_u8, 1_u8,1_u8,1_u8,
        2_u8,2_u8,2_u8,2_u8,2_u8,
        3_u8,3_u8,3_u8,3_u8,3_u8,3_u8,3_u8,
        4_u8,4_u8,4_u8,4_u8,4_u8,4_u8,4_u8,4_u8,4_u8,
    };
    inline constexpr auto OrderFrom2DChannel = std::array<u8, MaxAmbi2DChannels>{
        0_u8, 1_u8,1_u8, 2_u8,2_u8, 3_u8,3_u8, 4_u8,4_u8,
    };
} /* namespace AmbiIndex */


/**
 * Calculates ambisonic encoder coefficients using the X, Y, and Z direction
 * components, which must represent a normalized (unit length) vector.
 *
 * NOTE: The components use ambisonic coordinates. As a result:
 *
 * Ambisonic Y = OpenAL -X
 * Ambisonic Z = OpenAL Y
 * Ambisonic X = OpenAL -Z
 *
 * The components are ordered such that OpenAL's X, Y, and Z are the first,
 * second, and third parameters respectively -- simply negate X and Z.
 */
constexpr auto CalcAmbiCoeffs(f32 const y, f32 const z, f32 const x)
    -> std::array<f32, MaxAmbiChannels>
{
    auto const xx = x*x;
    auto const yy = y*y;
    auto const zz = z*z;
    auto const xy = x*y;
    auto const yz = y*z;
    auto const xz = x*z;
    auto const xxxx = xx*xx;
    auto const yyyy = yy*yy;
    auto const xxyy = xx*yy;
    auto const zzzz = zz*zz;

    return std::array<f32, MaxAmbiChannels>{{
        /* Zeroth-order */
        1.0f, /* ACN 0 = 1 */
        /* First-order */
        std::numbers::sqrt3_v<f32> * y, /* ACN 1 = sqrt(3) * Y */
        std::numbers::sqrt3_v<f32> * z, /* ACN 2 = sqrt(3) * Z */
        std::numbers::sqrt3_v<f32> * x, /* ACN 3 = sqrt(3) * X */
        /* Second-order */
        3.872983346e+00f * xy,               /* ACN 4 = sqrt(15) * X * Y */
        3.872983346e+00f * yz,               /* ACN 5 = sqrt(15) * Y * Z */
        1.118033989e+00f * (3.0f*zz - 1.0f), /* ACN 6 = sqrt(5)/2 * (3*Z*Z - 1) */
        3.872983346e+00f * xz,               /* ACN 7 = sqrt(15) * X * Z */
        1.936491673e+00f * (xx - yy),        /* ACN 8 = sqrt(15)/2 * (X*X - Y*Y) */
        /* Third-order */
        2.091650066e+00f * (y*(3.0f*xx - yy)),   /* ACN  9 = sqrt(35/8) * Y * (3*X*X - Y*Y) */
        1.024695076e+01f * (z*xy),               /* ACN 10 = sqrt(105) * Z * X * Y */
        1.620185175e+00f * (y*(5.0f*zz - 1.0f)), /* ACN 11 = sqrt(21/8) * Y * (5*Z*Z - 1) */
        1.322875656e+00f * (z*(5.0f*zz - 3.0f)), /* ACN 12 = sqrt(7)/2 * Z * (5*Z*Z - 3) */
        1.620185175e+00f * (x*(5.0f*zz - 1.0f)), /* ACN 13 = sqrt(21/8) * X * (5*Z*Z - 1) */
        5.123475383e+00f * (z*(xx - yy)),        /* ACN 14 = sqrt(105)/2 * Z * (X*X - Y*Y) */
        2.091650066e+00f * (x*(xx - 3.0f*yy)),   /* ACN 15 = sqrt(35/8) * X * (X*X - 3*Y*Y) */
        /* Fourth-order */
        8.874119675e+00f * (xy*(xx - yy)),                 /* ACN 16 = sqrt(35)*3/2 * X * Y * (X*X - Y*Y) */
        6.274950199e+00f * ((3.0f*xx - yy) * yz),          /* ACN 17 = sqrt(35/2)*3/2 * (3*X*X - Y*Y) * Y * Z */
        3.354101966e+00f * (xy * (7.0f*zz - 1.0f)),        /* ACN 18 = sqrt(5)*3/2 * X * Y * (7*Z*Z - 1) */
        2.371708245e+00f * (yz * (7.0f*zz - 3.0f)),        /* ACN 19 = sqrt(5/2)*3/2 * Y * Z * (7*Z*Z - 3) */
        3.750000000e-01f * (35.0f*zzzz - 30.0f*zz + 3.0f), /* ACN 20 = 3/8 * (35*Z*Z*Z*Z - 30*Z*Z + 3) */
        2.371708245e+00f * (xz * (7.0f*zz - 3.0f)),        /* ACN 21 = sqrt(5/2)*3/2 * X * Z * (7*Z*Z - 3) */
        1.677050983e+00f * ((xx - yy) * (7.0f*zz - 1.0f)), /* ACN 22 = sqrt(5)*3/4 * (X*X - Y*Y) * (7*Z*Z - 1) */
        6.274950199e+00f * ((xx - 3.0f*yy) * xz),          /* ACN 23 = sqrt(35/2)*3/2 * (X*X - 3*Y*Y) * X * Z */
        2.218529919e+00f * (xxxx - 6.0f*xxyy + yyyy),      /* ACN 24 = sqrt(35)*3/8 * (X*X*X*X - 6*X*X*Y*Y + Y*Y*Y*Y) */
    }};
}

#endif /* CORE_AMBIDEFS_H */
