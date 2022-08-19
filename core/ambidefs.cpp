
#include "config.h"

#include "ambidefs.h"


namespace {

using AmbiChannelFloatArray = std::array<float,MaxAmbiChannels>;


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
