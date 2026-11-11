#ifndef CORE_AMBI_UTILS_HPP
#define CORE_AMBI_UTILS_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <numbers>
#include <ranges>
#include <span>

#include "ambidefs.h"
#include "opthelper_macros.hpp"
#include "zudl.hpp"


/* NOTE: These are scale factors as applied to Ambisonics content. Decoder
 * coefficients should be divided by these values to get proper scalings.
 */
namespace AmbiScale {
    inline auto constexpr FromN3D = std::array{
        1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    static_assert(FromN3D.size() == MaxAmbiChannels);

    inline auto constexpr FromSN3D = std::array{
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
    static_assert(FromSN3D.size() == MaxAmbiChannels);

    inline auto constexpr FromFuMa = std::array{
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
    static_assert(FromFuMa.size() == MaxAmbiChannels);

    namespace detail {
        /* These HF gains are derived from the same 32-point speaker array. The
         * scale factor between orders represents the same scale factors for
         * any (regular) speaker array decoder. e.g. Given a first-order source
         * and second-order output, applying an HF scale of
         * HFScales[1][0] / HFScales[2][0]
         * to channel 0 will result in that channel being subsequently decoded
         * for second-order as if it was a first-order decoder for that same
         * speaker array.
         */
        inline auto constexpr HFScales = std::array{
            std::array{4.000000000e+00f, 2.309401077e+00f, 1.192569588e+00f, 7.189495850e-01f, 4.784482742e-01f},
            std::array{4.000000000e+00f, 2.309401077e+00f, 1.192569588e+00f, 7.189495850e-01f, 4.784482742e-01f},
            std::array{2.981423970e+00f, 2.309401077e+00f, 1.192569588e+00f, 7.189495850e-01f, 4.784482742e-01f},
            std::array{2.359168820e+00f, 2.031565936e+00f, 1.444598386e+00f, 7.189495850e-01f, 4.784482742e-01f},
            std::array{1.947005434e+00f, 1.764337084e+00f, 1.424707344e+00f, 9.755104127e-01f, 4.784482742e-01f},
        };

        /* Same as above, but using a 10-point horizontal-only speaker array.
         * Should only be used when the device is mixing in 2D B-Format for
         * horizontal-only output.
         */
        inline auto constexpr HFScales2D = std::array{
            std::array{2.236067977e+00f, 1.581138830e+00f, 9.128709292e-01f, 6.050756345e-01f, 4.370160244e-01f},
            std::array{2.236067977e+00f, 1.581138830e+00f, 9.128709292e-01f, 6.050756345e-01f, 4.370160244e-01f},
            std::array{1.825741858e+00f, 1.581138830e+00f, 9.128709292e-01f, 6.050756345e-01f, 4.370160244e-01f},
            std::array{1.581138830e+00f, 1.460781803e+00f, 1.118033989e+00f, 6.050756345e-01f, 4.370160244e-01f},
            std::array{1.414213562e+00f, 1.344997024e+00f, 1.144122806e+00f, 8.312538756e-01f, 4.370160244e-01f},
        };
    }

    /* Retrieves per-order HF scaling factors for "upsampling" ambisonic data. */
    [[nodiscard]] constexpr
    auto GetHFOrderScales(unsigned const src_order, unsigned const dev_order,
        bool const horizontalOnly) noexcept NONBLOCKING -> std::array<float, MaxAmbiOrder+1>
    {
        auto res = std::array<float, MaxAmbiOrder+1>{};

        auto const scales = horizontalOnly ? std::span{detail::HFScales2D}
            : std::span{detail::HFScales};
        std::ranges::transform(scales[src_order], scales[dev_order], res.begin(), std::divides{});

        return res;
    }
} /* namespace AmbiScale */

namespace AmbiIndex {
    inline auto constexpr FromFuMa = std::to_array<std::uint8_t>({
        0,  /* W */
        3,  /* X */
        1,  /* Y */
        2,  /* Z */
        6,  /* R */
        7,  /* S */
        5,  /* T */
        8,  /* U */
        4,  /* V */
        12, /* K */
        13, /* L */
        11, /* M */
        14, /* N */
        10, /* O */
        15, /* P */
        9,  /* Q */
        /* Higher orders not relevant for FuMa. The previous orders form a
         * pattern suggesting 20,21,19,22,18,23,17,24,16, but as above, unless
         * I hear otherwise, I don't want to make assumptions here.
         */
        0, 0, 0, 0, 0, 0, 0, 0, 0,
    });
    static_assert(FromFuMa.size() == MaxAmbiChannels);

    inline auto constexpr FromFuMa2D = std::to_array<std::uint8_t>({
        0,  /* W */
        3,  /* X */
        1,  /* Y */
        8,  /* U */
        4,  /* V */
        15, /* P */
        9,  /* Q */
        /* Higher orders not relevant for FuMa. Though the previous orders form
         * a pattern suggesting 24,16.
         */
        0, 0,
    });
    static_assert(FromFuMa2D.size() == MaxAmbi2DChannels);

    inline auto constexpr FromACN = std::to_array<std::uint8_t>({
        0,
        1, 2, 3,
        4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24,
    });
    static_assert(FromACN.size() == MaxAmbiChannels);

    inline auto constexpr FromACN2D = std::to_array<std::uint8_t>({
        0, 1,3, 4,8, 9,15, 16,24,
    });
    static_assert(FromACN2D.size() == MaxAmbi2DChannels);

    inline auto constexpr OrderFromChannel = std::to_array<std::uint8_t>({
        0,
        1,1,1,
        2,2,2,2,2,
        3,3,3,3,3,3,3,
        4,4,4,4,4,4,4,4,4,
    });
    static_assert(OrderFromChannel.size() == MaxAmbiChannels);
    inline auto constexpr OrderFrom2DChannel = std::to_array<std::uint8_t>({
        0, 1,1, 2,2, 3,3, 4,4,
    });
    static_assert(OrderFrom2DChannel.size() == MaxAmbi2DChannels);
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
constexpr auto CalcAmbiCoeffs(float const y, float const z, float const x) noexcept
    -> std::array<float, MaxAmbiChannels>
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

    return std::array<float, MaxAmbiChannels>{{
        /* Zeroth-order */
        1.0f, /* ACN 0 = 1 */
        /* First-order */
        std::numbers::sqrt3_v<float> * y, /* ACN 1 = sqrt(3) * Y */
        std::numbers::sqrt3_v<float> * z, /* ACN 2 = sqrt(3) * Z */
        std::numbers::sqrt3_v<float> * x, /* ACN 3 = sqrt(3) * X */
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

/**
 * CalcDirectionCoeffs
 *
 * Calculates ambisonic coefficients based on an OpenAL direction vector. The
 * vector must be normalized (unit length).
 */
[[nodiscard]] constexpr
auto CalcDirectionCoeffs(const std::span<float const, 3> dir) noexcept NONBLOCKING
    -> std::array<float,MaxAmbiChannels>
{
    /* Convert from OpenAL coords to Ambisonics. */
    return CalcAmbiCoeffs(-dir[0], dir[1], -dir[2]);
}


namespace AmbiScale {

namespace detail {

using AmbiChannelFloatArray = std::array<float, MaxAmbiChannels>;

inline auto constexpr inv_sqrt3f = static_cast<float>(1.0/std::numbers::sqrt3);


/* This calculates a first-order "upsampler" matrix. It combines a first-order
 * decoder matrix with a max-order encoder matrix, creating a matrix that
 * behaves as if the B-Format input signal is first decoded to a speaker array
 * at first-order, then those speaker feeds are encoded to a higher-order
 * signal. While not perfect, this should accurately encode a lower-order
 * signal into a higher-order signal.
 */
inline auto constexpr Order1Dec = std::array{
    std::array{1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f},
    std::array{1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f},
    std::array{1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f},
    std::array{1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f},
    std::array{1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f},
    std::array{1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f},
    std::array{1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f},
    std::array{1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f},
};
inline auto constexpr Order1Enc = std::array{
    CalcAmbiCoeffs( inv_sqrt3f,  inv_sqrt3f,  inv_sqrt3f),
    CalcAmbiCoeffs( inv_sqrt3f,  inv_sqrt3f, -inv_sqrt3f),
    CalcAmbiCoeffs(-inv_sqrt3f,  inv_sqrt3f,  inv_sqrt3f),
    CalcAmbiCoeffs(-inv_sqrt3f,  inv_sqrt3f, -inv_sqrt3f),
    CalcAmbiCoeffs( inv_sqrt3f, -inv_sqrt3f,  inv_sqrt3f),
    CalcAmbiCoeffs( inv_sqrt3f, -inv_sqrt3f, -inv_sqrt3f),
    CalcAmbiCoeffs(-inv_sqrt3f, -inv_sqrt3f,  inv_sqrt3f),
    CalcAmbiCoeffs(-inv_sqrt3f, -inv_sqrt3f, -inv_sqrt3f),
};
static_assert(Order1Dec.size() == Order1Enc.size(), "First-order mismatch");

/* This calculates a 2D first-order "upsampler" matrix. Same as the first-order
 * matrix, just using a more optimized speaker array for horizontal-only
 * content.
 */
inline auto constexpr Order1Dec2D = std::array{
    std::array{1.666666667e-01f, -9.622504486e-02f, 0.0f,  1.666666667e-01f},
    std::array{1.666666667e-01f, -1.924500897e-01f, 0.0f,  0.000000000e+00f},
    std::array{1.666666667e-01f, -9.622504486e-02f, 0.0f, -1.666666667e-01f},
    std::array{1.666666667e-01f,  9.622504486e-02f, 0.0f, -1.666666667e-01f},
    std::array{1.666666667e-01f,  1.924500897e-01f, 0.0f,  0.000000000e+00f},
    std::array{1.666666667e-01f,  9.622504486e-02f, 0.0f,  1.666666667e-01f},
};
inline auto constexpr Order1Enc2D = std::array{
    CalcAmbiCoeffs(-0.50000000000f, 0.0f,  0.86602540379f),
    CalcAmbiCoeffs(-1.00000000000f, 0.0f,  0.00000000000f),
    CalcAmbiCoeffs(-0.50000000000f, 0.0f, -0.86602540379f),
    CalcAmbiCoeffs( 0.50000000000f, 0.0f, -0.86602540379f),
    CalcAmbiCoeffs( 1.00000000000f, 0.0f,  0.00000000000f),
    CalcAmbiCoeffs( 0.50000000000f, 0.0f,  0.86602540379f),
};
static_assert(Order1Dec2D.size() == Order1Enc2D.size(), "First-order 2D mismatch");


/* This calculates a second-order "upsampler" matrix. Same as the first-order
 * matrix, just using a slightly more dense speaker array suitable for second-
 * order content.
 */
inline auto constexpr Order2Dec = std::array{
    std::array{8.333333333e-02f,  0.000000000e+00f, -7.588274978e-02f,  1.227808683e-01f,  0.000000000e+00f,  0.000000000e+00f, -1.591525047e-02f, -1.443375673e-01f,  1.167715449e-01f},
    std::array{8.333333333e-02f, -1.227808683e-01f,  0.000000000e+00f,  7.588274978e-02f, -1.443375673e-01f,  0.000000000e+00f, -9.316949906e-02f,  0.000000000e+00f, -7.216878365e-02f},
    std::array{8.333333333e-02f, -7.588274978e-02f,  1.227808683e-01f,  0.000000000e+00f,  0.000000000e+00f, -1.443375673e-01f,  1.090847495e-01f,  0.000000000e+00f, -4.460276122e-02f},
    std::array{8.333333333e-02f,  0.000000000e+00f,  7.588274978e-02f,  1.227808683e-01f,  0.000000000e+00f,  0.000000000e+00f, -1.591525047e-02f,  1.443375673e-01f,  1.167715449e-01f},
    std::array{8.333333333e-02f, -1.227808683e-01f,  0.000000000e+00f, -7.588274978e-02f,  1.443375673e-01f,  0.000000000e+00f, -9.316949906e-02f,  0.000000000e+00f, -7.216878365e-02f},
    std::array{8.333333333e-02f,  7.588274978e-02f, -1.227808683e-01f,  0.000000000e+00f,  0.000000000e+00f, -1.443375673e-01f,  1.090847495e-01f,  0.000000000e+00f, -4.460276122e-02f},
    std::array{8.333333333e-02f,  0.000000000e+00f, -7.588274978e-02f, -1.227808683e-01f,  0.000000000e+00f,  0.000000000e+00f, -1.591525047e-02f,  1.443375673e-01f,  1.167715449e-01f},
    std::array{8.333333333e-02f,  1.227808683e-01f,  0.000000000e+00f, -7.588274978e-02f, -1.443375673e-01f,  0.000000000e+00f, -9.316949906e-02f,  0.000000000e+00f, -7.216878365e-02f},
    std::array{8.333333333e-02f,  7.588274978e-02f,  1.227808683e-01f,  0.000000000e+00f,  0.000000000e+00f,  1.443375673e-01f,  1.090847495e-01f,  0.000000000e+00f, -4.460276122e-02f},
    std::array{8.333333333e-02f,  0.000000000e+00f,  7.588274978e-02f, -1.227808683e-01f,  0.000000000e+00f,  0.000000000e+00f, -1.591525047e-02f, -1.443375673e-01f,  1.167715449e-01f},
    std::array{8.333333333e-02f,  1.227808683e-01f,  0.000000000e+00f,  7.588274978e-02f,  1.443375673e-01f,  0.000000000e+00f, -9.316949906e-02f,  0.000000000e+00f, -7.216878365e-02f},
    std::array{8.333333333e-02f, -7.588274978e-02f, -1.227808683e-01f,  0.000000000e+00f,  0.000000000e+00f,  1.443375673e-01f,  1.090847495e-01f,  0.000000000e+00f, -4.460276122e-02f},
};
inline auto constexpr Order2Enc = std::array{
    CalcAmbiCoeffs( 0.000000000e+00f, -5.257311121e-01f,  8.506508084e-01f),
    CalcAmbiCoeffs(-8.506508084e-01f,  0.000000000e+00f,  5.257311121e-01f),
    CalcAmbiCoeffs(-5.257311121e-01f,  8.506508084e-01f,  0.000000000e+00f),
    CalcAmbiCoeffs( 0.000000000e+00f,  5.257311121e-01f,  8.506508084e-01f),
    CalcAmbiCoeffs(-8.506508084e-01f,  0.000000000e+00f, -5.257311121e-01f),
    CalcAmbiCoeffs( 5.257311121e-01f, -8.506508084e-01f,  0.000000000e+00f),
    CalcAmbiCoeffs( 0.000000000e+00f, -5.257311121e-01f, -8.506508084e-01f),
    CalcAmbiCoeffs( 8.506508084e-01f,  0.000000000e+00f, -5.257311121e-01f),
    CalcAmbiCoeffs( 5.257311121e-01f,  8.506508084e-01f,  0.000000000e+00f),
    CalcAmbiCoeffs( 0.000000000e+00f,  5.257311121e-01f, -8.506508084e-01f),
    CalcAmbiCoeffs( 8.506508084e-01f,  0.000000000e+00f,  5.257311121e-01f),
    CalcAmbiCoeffs(-5.257311121e-01f, -8.506508084e-01f,  0.000000000e+00f),
};
static_assert(Order2Dec.size() == Order2Enc.size(), "Second-order mismatch");

/* This calculates a 2D second-order "upsampler" matrix. Same as the second-
 * order matrix, just using a more optimized speaker array for horizontal-only
 * content.
 */
inline auto constexpr Order2Dec2D = std::array{
    std::array{1.250000000e-01f, -5.523559567e-02f, 0.0f,  1.333505242e-01f, -9.128709292e-02f, 0.0f, 0.0f, 0.0f,  9.128709292e-02f},
    std::array{1.250000000e-01f, -1.333505242e-01f, 0.0f,  5.523559567e-02f, -9.128709292e-02f, 0.0f, 0.0f, 0.0f, -9.128709292e-02f},
    std::array{1.250000000e-01f, -1.333505242e-01f, 0.0f, -5.523559567e-02f,  9.128709292e-02f, 0.0f, 0.0f, 0.0f, -9.128709292e-02f},
    std::array{1.250000000e-01f, -5.523559567e-02f, 0.0f, -1.333505242e-01f,  9.128709292e-02f, 0.0f, 0.0f, 0.0f,  9.128709292e-02f},
    std::array{1.250000000e-01f,  5.523559567e-02f, 0.0f, -1.333505242e-01f, -9.128709292e-02f, 0.0f, 0.0f, 0.0f,  9.128709292e-02f},
    std::array{1.250000000e-01f,  1.333505242e-01f, 0.0f, -5.523559567e-02f, -9.128709292e-02f, 0.0f, 0.0f, 0.0f, -9.128709292e-02f},
    std::array{1.250000000e-01f,  1.333505242e-01f, 0.0f,  5.523559567e-02f,  9.128709292e-02f, 0.0f, 0.0f, 0.0f, -9.128709292e-02f},
    std::array{1.250000000e-01f,  5.523559567e-02f, 0.0f,  1.333505242e-01f,  9.128709292e-02f, 0.0f, 0.0f, 0.0f,  9.128709292e-02f},
};
inline auto constexpr Order2Enc2D = std::array{
    CalcAmbiCoeffs(-0.38268343237f, 0.0f,  0.92387953251f),
    CalcAmbiCoeffs(-0.92387953251f, 0.0f,  0.38268343237f),
    CalcAmbiCoeffs(-0.92387953251f, 0.0f, -0.38268343237f),
    CalcAmbiCoeffs(-0.38268343237f, 0.0f, -0.92387953251f),
    CalcAmbiCoeffs( 0.38268343237f, 0.0f, -0.92387953251f),
    CalcAmbiCoeffs( 0.92387953251f, 0.0f, -0.38268343237f),
    CalcAmbiCoeffs( 0.92387953251f, 0.0f,  0.38268343237f),
    CalcAmbiCoeffs( 0.38268343237f, 0.0f,  0.92387953251f),
};
static_assert(Order2Dec2D.size() == Order2Enc2D.size(), "Second-order 2D mismatch");


/* This calculates a third-order "upsampler" matrix. Same as the first-order
 * matrix, just using a more dense speaker array suitable for third-order
 * content.
 */
inline auto constexpr Order3Dec = std::array{
    std::array{5.000000000e-02f,  3.090169944e-02f,  8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f,  6.454972244e-02f,  9.045084972e-02f,  0.000000000e+00f, -1.232790000e-02f, -1.256118221e-01f,  0.000000000e+00f,  1.126112056e-01f,  7.944389175e-02f,  0.000000000e+00f,  2.421151497e-02f,  0.000000000e+00f},
    std::array{5.000000000e-02f, -3.090169944e-02f,  8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -6.454972244e-02f,  9.045084972e-02f,  0.000000000e+00f, -1.232790000e-02f,  1.256118221e-01f,  0.000000000e+00f, -1.126112056e-01f,  7.944389175e-02f,  0.000000000e+00f,  2.421151497e-02f,  0.000000000e+00f},
    std::array{5.000000000e-02f,  3.090169944e-02f, -8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -6.454972244e-02f,  9.045084972e-02f,  0.000000000e+00f, -1.232790000e-02f, -1.256118221e-01f,  0.000000000e+00f,  1.126112056e-01f, -7.944389175e-02f,  0.000000000e+00f, -2.421151497e-02f,  0.000000000e+00f},
    std::array{5.000000000e-02f, -3.090169944e-02f, -8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f,  6.454972244e-02f,  9.045084972e-02f,  0.000000000e+00f, -1.232790000e-02f,  1.256118221e-01f,  0.000000000e+00f, -1.126112056e-01f, -7.944389175e-02f,  0.000000000e+00f, -2.421151497e-02f,  0.000000000e+00f},
    std::array{5.000000000e-02f,  8.090169944e-02f,  0.000000000e+00f,  3.090169944e-02f,  6.454972244e-02f,  0.000000000e+00f, -5.590169944e-02f,  0.000000000e+00f, -7.216878365e-02f, -7.763237543e-02f,  0.000000000e+00f, -2.950836627e-02f,  0.000000000e+00f, -1.497759251e-01f,  0.000000000e+00f, -7.763237543e-02f},
    std::array{5.000000000e-02f,  8.090169944e-02f,  0.000000000e+00f, -3.090169944e-02f, -6.454972244e-02f,  0.000000000e+00f, -5.590169944e-02f,  0.000000000e+00f, -7.216878365e-02f, -7.763237543e-02f,  0.000000000e+00f, -2.950836627e-02f,  0.000000000e+00f,  1.497759251e-01f,  0.000000000e+00f,  7.763237543e-02f},
    std::array{5.000000000e-02f, -8.090169944e-02f,  0.000000000e+00f,  3.090169944e-02f, -6.454972244e-02f,  0.000000000e+00f, -5.590169944e-02f,  0.000000000e+00f, -7.216878365e-02f,  7.763237543e-02f,  0.000000000e+00f,  2.950836627e-02f,  0.000000000e+00f, -1.497759251e-01f,  0.000000000e+00f, -7.763237543e-02f},
    std::array{5.000000000e-02f, -8.090169944e-02f,  0.000000000e+00f, -3.090169944e-02f,  6.454972244e-02f,  0.000000000e+00f, -5.590169944e-02f,  0.000000000e+00f, -7.216878365e-02f,  7.763237543e-02f,  0.000000000e+00f,  2.950836627e-02f,  0.000000000e+00f,  1.497759251e-01f,  0.000000000e+00f,  7.763237543e-02f},
    std::array{5.000000000e-02f,  0.000000000e+00f,  3.090169944e-02f,  8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -3.454915028e-02f,  6.454972244e-02f,  8.449668365e-02f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f,  3.034486645e-02f, -6.779013272e-02f,  1.659481923e-01f,  4.797944664e-02f},
    std::array{5.000000000e-02f,  0.000000000e+00f,  3.090169944e-02f, -8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -3.454915028e-02f, -6.454972244e-02f,  8.449668365e-02f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f,  3.034486645e-02f,  6.779013272e-02f,  1.659481923e-01f, -4.797944664e-02f},
    std::array{5.000000000e-02f,  0.000000000e+00f, -3.090169944e-02f,  8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -3.454915028e-02f, -6.454972244e-02f,  8.449668365e-02f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f, -3.034486645e-02f, -6.779013272e-02f, -1.659481923e-01f,  4.797944664e-02f},
    std::array{5.000000000e-02f,  0.000000000e+00f, -3.090169944e-02f, -8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -3.454915028e-02f,  6.454972244e-02f,  8.449668365e-02f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f, -3.034486645e-02f,  6.779013272e-02f, -1.659481923e-01f, -4.797944664e-02f},
    std::array{5.000000000e-02f,  5.000000000e-02f,  5.000000000e-02f,  5.000000000e-02f,  6.454972244e-02f,  6.454972244e-02f,  0.000000000e+00f,  6.454972244e-02f,  0.000000000e+00f,  1.016220987e-01f,  6.338656910e-02f, -1.092600649e-02f, -7.364853795e-02f,  1.011266756e-01f, -7.086833869e-02f, -1.482646439e-02f},
    std::array{5.000000000e-02f,  5.000000000e-02f,  5.000000000e-02f, -5.000000000e-02f, -6.454972244e-02f,  6.454972244e-02f,  0.000000000e+00f, -6.454972244e-02f,  0.000000000e+00f,  1.016220987e-01f, -6.338656910e-02f, -1.092600649e-02f, -7.364853795e-02f, -1.011266756e-01f, -7.086833869e-02f,  1.482646439e-02f},
    std::array{5.000000000e-02f, -5.000000000e-02f,  5.000000000e-02f,  5.000000000e-02f, -6.454972244e-02f, -6.454972244e-02f,  0.000000000e+00f,  6.454972244e-02f,  0.000000000e+00f, -1.016220987e-01f, -6.338656910e-02f,  1.092600649e-02f, -7.364853795e-02f,  1.011266756e-01f, -7.086833869e-02f, -1.482646439e-02f},
    std::array{5.000000000e-02f, -5.000000000e-02f,  5.000000000e-02f, -5.000000000e-02f,  6.454972244e-02f, -6.454972244e-02f,  0.000000000e+00f, -6.454972244e-02f,  0.000000000e+00f, -1.016220987e-01f,  6.338656910e-02f,  1.092600649e-02f, -7.364853795e-02f, -1.011266756e-01f, -7.086833869e-02f,  1.482646439e-02f},
    std::array{5.000000000e-02f,  5.000000000e-02f, -5.000000000e-02f,  5.000000000e-02f,  6.454972244e-02f, -6.454972244e-02f,  0.000000000e+00f, -6.454972244e-02f,  0.000000000e+00f,  1.016220987e-01f, -6.338656910e-02f, -1.092600649e-02f,  7.364853795e-02f,  1.011266756e-01f,  7.086833869e-02f, -1.482646439e-02f},
    std::array{5.000000000e-02f,  5.000000000e-02f, -5.000000000e-02f, -5.000000000e-02f, -6.454972244e-02f, -6.454972244e-02f,  0.000000000e+00f,  6.454972244e-02f,  0.000000000e+00f,  1.016220987e-01f,  6.338656910e-02f, -1.092600649e-02f,  7.364853795e-02f, -1.011266756e-01f,  7.086833869e-02f,  1.482646439e-02f},
    std::array{5.000000000e-02f, -5.000000000e-02f, -5.000000000e-02f,  5.000000000e-02f, -6.454972244e-02f,  6.454972244e-02f,  0.000000000e+00f, -6.454972244e-02f,  0.000000000e+00f, -1.016220987e-01f,  6.338656910e-02f,  1.092600649e-02f,  7.364853795e-02f,  1.011266756e-01f,  7.086833869e-02f, -1.482646439e-02f},
    std::array{5.000000000e-02f, -5.000000000e-02f, -5.000000000e-02f, -5.000000000e-02f,  6.454972244e-02f,  6.454972244e-02f,  0.000000000e+00f,  6.454972244e-02f,  0.000000000e+00f, -1.016220987e-01f, -6.338656910e-02f,  1.092600649e-02f,  7.364853795e-02f, -1.011266756e-01f,  7.086833869e-02f,  1.482646439e-02f},
};
inline auto constexpr Order3Enc = std::array{
    CalcAmbiCoeffs( 0.35682208976f,  0.93417235897f,  0.00000000000f),
    CalcAmbiCoeffs(-0.35682208976f,  0.93417235897f,  0.00000000000f),
    CalcAmbiCoeffs( 0.35682208976f, -0.93417235897f,  0.00000000000f),
    CalcAmbiCoeffs(-0.35682208976f, -0.93417235897f,  0.00000000000f),
    CalcAmbiCoeffs( 0.93417235897f,  0.00000000000f,  0.35682208976f),
    CalcAmbiCoeffs( 0.93417235897f,  0.00000000000f, -0.35682208976f),
    CalcAmbiCoeffs(-0.93417235897f,  0.00000000000f,  0.35682208976f),
    CalcAmbiCoeffs(-0.93417235897f,  0.00000000000f, -0.35682208976f),
    CalcAmbiCoeffs( 0.00000000000f,  0.35682208976f,  0.93417235897f),
    CalcAmbiCoeffs( 0.00000000000f,  0.35682208976f, -0.93417235897f),
    CalcAmbiCoeffs( 0.00000000000f, -0.35682208976f,  0.93417235897f),
    CalcAmbiCoeffs( 0.00000000000f, -0.35682208976f, -0.93417235897f),
    CalcAmbiCoeffs(     inv_sqrt3f,      inv_sqrt3f,      inv_sqrt3f),
    CalcAmbiCoeffs(     inv_sqrt3f,      inv_sqrt3f,     -inv_sqrt3f),
    CalcAmbiCoeffs(    -inv_sqrt3f,      inv_sqrt3f,      inv_sqrt3f),
    CalcAmbiCoeffs(    -inv_sqrt3f,      inv_sqrt3f,     -inv_sqrt3f),
    CalcAmbiCoeffs(     inv_sqrt3f,     -inv_sqrt3f,      inv_sqrt3f),
    CalcAmbiCoeffs(     inv_sqrt3f,     -inv_sqrt3f,     -inv_sqrt3f),
    CalcAmbiCoeffs(    -inv_sqrt3f,     -inv_sqrt3f,      inv_sqrt3f),
    CalcAmbiCoeffs(    -inv_sqrt3f,     -inv_sqrt3f,     -inv_sqrt3f),
};
static_assert(Order3Dec.size() == Order3Enc.size(), "Third-order mismatch");

/* This calculates a 2D third-order "upsampler" matrix. Same as the third-order
 * matrix, just using a more optimized speaker array for horizontal-only
 * content.
 */
inline auto constexpr Order3Dec2D = std::array{
    std::array{1.000000000e-01f,  3.568220898e-02f, 0.0f,  1.098185471e-01f,  6.070619982e-02f, 0.0f, 0.0f, 0.0f,  8.355491589e-02f,  7.735682057e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  5.620301997e-02f},
    std::array{1.000000000e-01f,  9.341723590e-02f, 0.0f,  6.787159473e-02f,  9.822469464e-02f, 0.0f, 0.0f, 0.0f, -3.191513794e-02f,  2.954767620e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -9.093839659e-02f},
    std::array{1.000000000e-01f,  1.154700538e-01f, 0.0f,  0.000000000e+00f,  0.000000000e+00f, 0.0f, 0.0f, 0.0f, -1.032795559e-01f, -9.561828875e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000000e+00f},
    std::array{1.000000000e-01f,  9.341723590e-02f, 0.0f, -6.787159473e-02f, -9.822469464e-02f, 0.0f, 0.0f, 0.0f, -3.191513794e-02f,  2.954767620e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  9.093839659e-02f},
    std::array{1.000000000e-01f,  3.568220898e-02f, 0.0f, -1.098185471e-01f, -6.070619982e-02f, 0.0f, 0.0f, 0.0f,  8.355491589e-02f,  7.735682057e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -5.620301997e-02f},
    std::array{1.000000000e-01f, -3.568220898e-02f, 0.0f, -1.098185471e-01f,  6.070619982e-02f, 0.0f, 0.0f, 0.0f,  8.355491589e-02f, -7.735682057e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -5.620301997e-02f},
    std::array{1.000000000e-01f, -9.341723590e-02f, 0.0f, -6.787159473e-02f,  9.822469464e-02f, 0.0f, 0.0f, 0.0f, -3.191513794e-02f, -2.954767620e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  9.093839659e-02f},
    std::array{1.000000000e-01f, -1.154700538e-01f, 0.0f,  0.000000000e+00f,  0.000000000e+00f, 0.0f, 0.0f, 0.0f, -1.032795559e-01f,  9.561828875e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000000e+00f},
    std::array{1.000000000e-01f, -9.341723590e-02f, 0.0f,  6.787159473e-02f, -9.822469464e-02f, 0.0f, 0.0f, 0.0f, -3.191513794e-02f, -2.954767620e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -9.093839659e-02f},
    std::array{1.000000000e-01f, -3.568220898e-02f, 0.0f,  1.098185471e-01f, -6.070619982e-02f, 0.0f, 0.0f, 0.0f,  8.355491589e-02f, -7.735682057e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  5.620301997e-02f},
};
inline auto constexpr Order3Enc2D = std::array{
    CalcAmbiCoeffs( 3.090169944e-01f,  0.0f,  9.510565163e-01f),
    CalcAmbiCoeffs( 8.090169944e-01f,  0.0f,  5.877852523e-01f),
    CalcAmbiCoeffs( 1.000000000e+00f,  0.0f,  0.000000000e+00f),
    CalcAmbiCoeffs( 8.090169944e-01f,  0.0f, -5.877852523e-01f),
    CalcAmbiCoeffs( 3.090169944e-01f,  0.0f, -9.510565163e-01f),
    CalcAmbiCoeffs(-3.090169944e-01f,  0.0f, -9.510565163e-01f),
    CalcAmbiCoeffs(-8.090169944e-01f,  0.0f, -5.877852523e-01f),
    CalcAmbiCoeffs(-1.000000000e+00f,  0.0f,  0.000000000e+00f),
    CalcAmbiCoeffs(-8.090169944e-01f,  0.0f,  5.877852523e-01f),
    CalcAmbiCoeffs(-3.090169944e-01f,  0.0f,  9.510565163e-01f),
};
static_assert(Order3Dec2D.size() == Order3Enc2D.size(), "Third-order 2D mismatch");


/* This calculates a 2D fourth-order "upsampler" matrix. There is no 3D fourth-
 * order upsampler since fourth-order is the max order we'll be supporting for
 * the foreseeable future. This is only necessary for mixing horizontal-only
 * fourth-order content to 3D.
 */
inline auto constexpr Order4Dec2D = std::array{
    std::array{1.000000000e-01f,  3.568220898e-02f, 0.0f,  1.098185471e-01f,  6.070619982e-02f, 0.0f, 0.0f, 0.0f,  8.355491589e-02f,  7.735682057e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  5.620301997e-02f,  8.573754253e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  2.785781628e-02f},
    std::array{1.000000000e-01f,  9.341723590e-02f, 0.0f,  6.787159473e-02f,  9.822469464e-02f, 0.0f, 0.0f, 0.0f, -3.191513794e-02f,  2.954767620e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -9.093839659e-02f, -5.298871540e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -7.293270986e-02f},
    std::array{1.000000000e-01f,  1.154700538e-01f, 0.0f,  0.000000000e+00f,  0.000000000e+00f, 0.0f, 0.0f, 0.0f, -1.032795559e-01f, -9.561828875e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000000e+00f,  0.000000000e+00f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  9.014978717e-02f},
    std::array{1.000000000e-01f,  9.341723590e-02f, 0.0f, -6.787159473e-02f, -9.822469464e-02f, 0.0f, 0.0f, 0.0f, -3.191513794e-02f,  2.954767620e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  9.093839659e-02f,  5.298871540e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -7.293270986e-02f},
    std::array{1.000000000e-01f,  3.568220898e-02f, 0.0f, -1.098185471e-01f, -6.070619982e-02f, 0.0f, 0.0f, 0.0f,  8.355491589e-02f,  7.735682057e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -5.620301997e-02f, -8.573754253e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  2.785781628e-02f},
    std::array{1.000000000e-01f, -3.568220898e-02f, 0.0f, -1.098185471e-01f,  6.070619982e-02f, 0.0f, 0.0f, 0.0f,  8.355491589e-02f, -7.735682057e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -5.620301997e-02f,  8.573754253e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  2.785781628e-02f},
    std::array{1.000000000e-01f, -9.341723590e-02f, 0.0f, -6.787159473e-02f,  9.822469464e-02f, 0.0f, 0.0f, 0.0f, -3.191513794e-02f, -2.954767620e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  9.093839659e-02f, -5.298871540e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -7.293270986e-02f},
    std::array{1.000000000e-01f, -1.154700538e-01f, 0.0f,  0.000000000e+00f,  0.000000000e+00f, 0.0f, 0.0f, 0.0f, -1.032795559e-01f,  9.561828875e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000000e+00f,  0.000000000e+00f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  9.014978717e-02f},
    std::array{1.000000000e-01f, -9.341723590e-02f, 0.0f,  6.787159473e-02f, -9.822469464e-02f, 0.0f, 0.0f, 0.0f, -3.191513794e-02f, -2.954767620e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -9.093839659e-02f,  5.298871540e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -7.293270986e-02f},
    std::array{1.000000000e-01f, -3.568220898e-02f, 0.0f,  1.098185471e-01f, -6.070619982e-02f, 0.0f, 0.0f, 0.0f,  8.355491589e-02f, -7.735682057e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  5.620301997e-02f, -8.573754253e-02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  2.785781628e-02f},
};
inline auto constexpr Order4Enc2D = std::array{
    CalcAmbiCoeffs( 3.090169944e-01f,  0.000000000e+00f,  9.510565163e-01f),
    CalcAmbiCoeffs( 8.090169944e-01f,  0.000000000e+00f,  5.877852523e-01f),
    CalcAmbiCoeffs( 1.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f),
    CalcAmbiCoeffs( 8.090169944e-01f,  0.000000000e+00f, -5.877852523e-01f),
    CalcAmbiCoeffs( 3.090169944e-01f,  0.000000000e+00f, -9.510565163e-01f),
    CalcAmbiCoeffs(-3.090169944e-01f,  0.000000000e+00f, -9.510565163e-01f),
    CalcAmbiCoeffs(-8.090169944e-01f,  0.000000000e+00f, -5.877852523e-01f),
    CalcAmbiCoeffs(-1.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f),
    CalcAmbiCoeffs(-8.090169944e-01f,  0.000000000e+00f,  5.877852523e-01f),
    CalcAmbiCoeffs(-3.090169944e-01f,  0.000000000e+00f,  9.510565163e-01f),
};
static_assert(Order4Dec2D.size() == Order4Enc2D.size(), "Fourth-order 2D mismatch");


template<std::size_t N, std::size_t M> [[nodiscard]] consteval
auto CalcAmbiUpsampler(std::array<std::array<float, N>, M> const& decoder,
    std::array<AmbiChannelFloatArray, M> const& encoder) noexcept
{
    auto res = std::array<AmbiChannelFloatArray, N>{};

    for(const auto i : std::views::iota(0_uz, decoder[0].size()))
    {
        for(const auto j : std::views::iota(0_uz, encoder[0].size()))
        {
            auto sum = 0.0;
            for(const auto k : std::views::iota(0_uz, decoder.size()))
                sum += double{decoder[k][i]} * encoder[k][j];
            res[i][j] = static_cast<float>(sum);
        }
    }

    return res;
}

} // namespace detail

inline auto constexpr FirstOrderUp = detail::CalcAmbiUpsampler(detail::Order1Dec, detail::Order1Enc);
inline auto constexpr FirstOrder2DUp = detail::CalcAmbiUpsampler(detail::Order1Dec2D, detail::Order1Enc2D);
inline auto constexpr SecondOrderUp = detail::CalcAmbiUpsampler(detail::Order2Dec, detail::Order2Enc);
inline auto constexpr SecondOrder2DUp = detail::CalcAmbiUpsampler(detail::Order2Dec2D, detail::Order2Enc2D);
inline auto constexpr ThirdOrderUp = detail::CalcAmbiUpsampler(detail::Order3Dec, detail::Order3Enc);
inline auto constexpr ThirdOrder2DUp = detail::CalcAmbiUpsampler(detail::Order3Dec2D, detail::Order3Enc2D);
inline auto constexpr FourthOrder2DUp = detail::CalcAmbiUpsampler(detail::Order4Dec2D, detail::Order4Enc2D);

}

#endif /* CORE_AMBI_UTILS_HPP */
