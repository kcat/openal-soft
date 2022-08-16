
#include "config.h"

#include "ambidefs.h"

#include <cassert>

#include "alnumbers.h"
#include "opthelpers.h"


namespace {

using AmbiChannelFloatArray = std::array<float,MaxAmbiChannels>;

/* Copied from mixer.cpp. */
constexpr auto CalcAmbiCoeffs(const float y, const float z, const float x)
{
    const float xx{x*x}, yy{y*y}, zz{z*z}, xy{x*y}, yz{y*z}, xz{x*z};

    return AmbiChannelFloatArray{{
        /* Zeroth-order */
        1.0f, /* ACN 0 = 1 */
        /* First-order */
        al::numbers::sqrt3_v<float> * y, /* ACN 1 = sqrt(3) * Y */
        al::numbers::sqrt3_v<float> * z, /* ACN 2 = sqrt(3) * Z */
        al::numbers::sqrt3_v<float> * x, /* ACN 3 = sqrt(3) * X */
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
        /* ACN 16 = sqrt(35)*3/2 * X * Y * (X*X - Y*Y) */
        /* ACN 17 = sqrt(35/2)*3/2 * (3*X*X - Y*Y) * Y * Z */
        /* ACN 18 = sqrt(5)*3/2 * X * Y * (7*Z*Z - 1) */
        /* ACN 19 = sqrt(5/2)*3/2 * Y * Z * (7*Z*Z - 3)  */
        /* ACN 20 = 3/8 * (35*Z*Z*Z*Z - 30*Z*Z + 3) */
        /* ACN 21 = sqrt(5/2)*3/2 * X * Z * (7*Z*Z - 3) */
        /* ACN 22 = sqrt(5)*3/4 * (X*X - Y*Y) * (7*Z*Z - 1) */
        /* ACN 23 = sqrt(35/2)*3/2 * (X*X - 3*Y*Y) * X * Z */
        /* ACN 24 = sqrt(35)*3/8 * (X*X*X*X - 6*X*X*Y*Y + Y*Y*Y*Y) */
    }};
}


constexpr std::array<std::array<float,4>,8> FirstOrderDecoder{{
    {{ 1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f, }},
    {{ 1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f, }},
    {{ 1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f, }},
    {{ 1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f, }},
    {{ 1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f, }},
    {{ 1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f, }},
    {{ 1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f, }},
    {{ 1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f, }},
}};

constexpr std::array<AmbiChannelFloatArray,8> FirstOrderEncoder{{
    CalcAmbiCoeffs( 0.57735026919f,  0.57735026919f,  0.57735026919f),
    CalcAmbiCoeffs( 0.57735026919f,  0.57735026919f, -0.57735026919f),
    CalcAmbiCoeffs(-0.57735026919f,  0.57735026919f,  0.57735026919f),
    CalcAmbiCoeffs(-0.57735026919f,  0.57735026919f, -0.57735026919f),
    CalcAmbiCoeffs( 0.57735026919f, -0.57735026919f,  0.57735026919f),
    CalcAmbiCoeffs( 0.57735026919f, -0.57735026919f, -0.57735026919f),
    CalcAmbiCoeffs(-0.57735026919f, -0.57735026919f,  0.57735026919f),
    CalcAmbiCoeffs(-0.57735026919f, -0.57735026919f, -0.57735026919f),
}};
static_assert(FirstOrderDecoder.size() == FirstOrderEncoder.size(), "First-order mismatch");

/* This calculates a first-order "upsampler" matrix. It combines a first-order
 * decoder matrix with a max-order encoder matrix, creating a matrix that
 * behaves as if the B-Format input signal is first decoded to a speaker array
 * at first-order, then those speaker feeds are encoded to a higher-order
 * signal. While not perfect, this should accurately encode a lower-order
 * signal into a higher-order signal.
 */
auto CalcFirstOrderUp()
{
    std::array<AmbiChannelFloatArray,4> res{};

    for(size_t i{0};i < FirstOrderDecoder[0].size();++i)
    {
        for(size_t j{0};j < FirstOrderEncoder[0].size();++j)
        {
            double sum{0.0};
            for(size_t k{0};k < FirstOrderDecoder.size();++k)
                sum += double{FirstOrderDecoder[k][i]} * FirstOrderEncoder[k][j];
            res[i][j] = static_cast<float>(sum);
        }
    }

    return res;
}


constexpr std::array<std::array<float,9>,14> SecondOrderDecoder{{
    {{ 7.142857143e-02f,  0.000000000e+00f,  0.000000000e+00f,  1.237179148e-01f,  0.000000000e+00f,  0.000000000e+00f, -7.453559925e-02f,  0.000000000e+00f,  1.290994449e-01f, }},
    {{ 7.142857143e-02f,  0.000000000e+00f,  0.000000000e+00f, -1.237179148e-01f,  0.000000000e+00f,  0.000000000e+00f, -7.453559925e-02f,  0.000000000e+00f,  1.290994449e-01f, }},
    {{ 7.142857143e-02f,  1.237179148e-01f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f, -7.453559925e-02f,  0.000000000e+00f, -1.290994449e-01f, }},
    {{ 7.142857143e-02f, -1.237179148e-01f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f, -7.453559925e-02f,  0.000000000e+00f, -1.290994449e-01f, }},
    {{ 7.142857143e-02f,  0.000000000e+00f,  1.237179148e-01f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f,  1.490711985e-01f,  0.000000000e+00f,  0.000000000e+00f, }},
    {{ 7.142857143e-02f,  0.000000000e+00f, -1.237179148e-01f,  0.000000000e+00f,  0.000000000e+00f,  0.000000000e+00f,  1.490711985e-01f,  0.000000000e+00f,  0.000000000e+00f, }},
    {{ 7.142857143e-02f,  7.142857143e-02f,  7.142857143e-02f,  7.142857143e-02f,  9.682458366e-02f,  9.682458366e-02f,  0.000000000e+00f,  9.682458366e-02f,  0.000000000e+00f, }},
    {{ 7.142857143e-02f,  7.142857143e-02f,  7.142857143e-02f, -7.142857143e-02f, -9.682458366e-02f,  9.682458366e-02f,  0.000000000e+00f, -9.682458366e-02f,  0.000000000e+00f, }},
    {{ 7.142857143e-02f, -7.142857143e-02f,  7.142857143e-02f,  7.142857143e-02f, -9.682458366e-02f, -9.682458366e-02f,  0.000000000e+00f,  9.682458366e-02f,  0.000000000e+00f, }},
    {{ 7.142857143e-02f, -7.142857143e-02f,  7.142857143e-02f, -7.142857143e-02f,  9.682458366e-02f, -9.682458366e-02f,  0.000000000e+00f, -9.682458366e-02f,  0.000000000e+00f, }},
    {{ 7.142857143e-02f,  7.142857143e-02f, -7.142857143e-02f,  7.142857143e-02f,  9.682458366e-02f, -9.682458366e-02f,  0.000000000e+00f, -9.682458366e-02f,  0.000000000e+00f, }},
    {{ 7.142857143e-02f,  7.142857143e-02f, -7.142857143e-02f, -7.142857143e-02f, -9.682458366e-02f, -9.682458366e-02f,  0.000000000e+00f,  9.682458366e-02f,  0.000000000e+00f, }},
    {{ 7.142857143e-02f, -7.142857143e-02f, -7.142857143e-02f,  7.142857143e-02f, -9.682458366e-02f,  9.682458366e-02f,  0.000000000e+00f, -9.682458366e-02f,  0.000000000e+00f, }},
    {{ 7.142857143e-02f, -7.142857143e-02f, -7.142857143e-02f, -7.142857143e-02f,  9.682458366e-02f,  9.682458366e-02f,  0.000000000e+00f,  9.682458366e-02f,  0.000000000e+00f, }},
}};

constexpr std::array<AmbiChannelFloatArray,14> SecondOrderEncoder{{
    CalcAmbiCoeffs( 0.00000000000f,  0.00000000000f,  1.00000000000f),
    CalcAmbiCoeffs( 0.00000000000f,  0.00000000000f, -1.00000000000f),
    CalcAmbiCoeffs( 1.00000000000f,  0.00000000000f,  0.00000000000f),
    CalcAmbiCoeffs(-1.00000000000f,  0.00000000000f,  0.00000000000f),
    CalcAmbiCoeffs( 0.00000000000f,  1.00000000000f,  0.00000000000f),
    CalcAmbiCoeffs( 0.00000000000f, -1.00000000000f,  0.00000000000f),
    CalcAmbiCoeffs( 0.57735026919f,  0.57735026919f,  0.57735026919f),
    CalcAmbiCoeffs( 0.57735026919f,  0.57735026919f, -0.57735026919f),
    CalcAmbiCoeffs(-0.57735026919f,  0.57735026919f,  0.57735026919f),
    CalcAmbiCoeffs(-0.57735026919f,  0.57735026919f, -0.57735026919f),
    CalcAmbiCoeffs( 0.57735026919f, -0.57735026919f,  0.57735026919f),
    CalcAmbiCoeffs( 0.57735026919f, -0.57735026919f, -0.57735026919f),
    CalcAmbiCoeffs(-0.57735026919f, -0.57735026919f,  0.57735026919f),
    CalcAmbiCoeffs(-0.57735026919f, -0.57735026919f, -0.57735026919f),
}};
static_assert(SecondOrderDecoder.size() == SecondOrderEncoder.size(), "Second-order mismatch");

/* This calculates a second-order "upsampler" matrix. Same as the first-order
 * matrix, just using a slightly more dense speaker array suitable for second-
 * order content.
 */
auto CalcSecondOrderUp()
{
    std::array<AmbiChannelFloatArray,9> res{};

    for(size_t i{0};i < SecondOrderDecoder[0].size();++i)
    {
        for(size_t j{0};j < SecondOrderEncoder[0].size();++j)
        {
            double sum{0.0};
            for(size_t k{0};k < SecondOrderDecoder.size();++k)
                sum += double{SecondOrderDecoder[k][i]} * SecondOrderEncoder[k][j];
            res[i][j] = static_cast<float>(sum);
        }
    }

    return res;
}

/* TODO: When fourth-order is properly supported, fill this out. */
auto CalcThirdOrderUp()
{
    std::array<AmbiChannelFloatArray,16> res{};

    for(size_t i{0};i < res.size();++i)
        res[i][i] = 1.0f;

    return res;
}

} // namespace

const std::array<AmbiChannelFloatArray,4> AmbiScale::FirstOrderUp{CalcFirstOrderUp()};
const std::array<AmbiChannelFloatArray,9> AmbiScale::SecondOrderUp{CalcSecondOrderUp()};
const std::array<AmbiChannelFloatArray,16> AmbiScale::ThirdOrderUp{CalcThirdOrderUp()};

const std::array<float,MaxAmbiOrder+1> AmbiScale::DecoderHFScale10{{
    2.000000000e+00f, 1.154700538e+00f
}};
const std::array<float,MaxAmbiOrder+1> AmbiScale::DecoderHFScale2O{{
    1.972026594e+00f, 1.527525232e+00f, 7.888106377e-01f
}};
/* TODO: Set properly when making the third-order upsampler decoder. */
const std::array<float,MaxAmbiOrder+1> AmbiScale::DecoderHFScale3O{{
    1.000000000e+00f, 1.000000000e+00f, 1.000000000e+00f, 1.000000000e+00f
}};
