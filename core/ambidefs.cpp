
#include "config.h"

#include "ambidefs.h"

#include <algorithm>
#include <functional>
#include <numbers>
#include <ranges>
#include <span>

#include "alnumeric.h"

namespace {

static_assert(AmbiScale::FromN3D.size() == MaxAmbiChannels);
static_assert(AmbiScale::FromSN3D.size() == MaxAmbiChannels);
static_assert(AmbiScale::FromFuMa.size() == MaxAmbiChannels);
static_assert(AmbiScale::FromUHJ.size() == MaxAmbiChannels);

static_assert(AmbiIndex::FromFuMa.size() == MaxAmbiChannels);
static_assert(AmbiIndex::FromFuMa2D.size() == MaxAmbi2DChannels);


using AmbiChannelFloatArray = std::array<float,MaxAmbiChannels>;

constexpr auto inv_sqrt3f = static_cast<float>(1.0/std::numbers::sqrt3);


/* These HF gains are derived from the same 32-point speaker array. The scale
 * factor between orders represents the same scale factors for any (regular)
 * speaker array decoder. e.g. Given a first-order source and second-order
 * output, applying an HF scale of HFScales[1][0] / HFScales[2][0] to channel 0
 * will result in that channel being subsequently decoded for second-order as
 * if it was a first-order decoder for that same speaker array.
 */
constexpr auto HFScales = std::array{
    std::array{4.000000000e+00f, 2.309401077e+00f, 1.192569588e+00f, 7.189495850e-01f, 4.784482742e-01f},
    std::array{4.000000000e+00f, 2.309401077e+00f, 1.192569588e+00f, 7.189495850e-01f, 4.784482742e-01f},
    std::array{2.981423970e+00f, 2.309401077e+00f, 1.192569588e+00f, 7.189495850e-01f, 4.784482742e-01f},
    std::array{2.359168820e+00f, 2.031565936e+00f, 1.444598386e+00f, 7.189495850e-01f, 4.784482742e-01f},
    std::array{1.947005434e+00f, 1.764337084e+00f, 1.424707344e+00f, 9.755104127e-01f, 4.784482742e-01f},
};

/* Same as above, but using a 10-point horizontal-only speaker array. Should
 * only be used when the device is mixing in 2D B-Format for horizontal-only
 * output.
 */
constexpr auto HFScales2D = std::array{
    std::array{2.236067977e+00f, 1.581138830e+00f, 9.128709292e-01f, 6.050756345e-01f, 4.370160244e-01f},
    std::array{2.236067977e+00f, 1.581138830e+00f, 9.128709292e-01f, 6.050756345e-01f, 4.370160244e-01f},
    std::array{1.825741858e+00f, 1.581138830e+00f, 9.128709292e-01f, 6.050756345e-01f, 4.370160244e-01f},
    std::array{1.581138830e+00f, 1.460781803e+00f, 1.118033989e+00f, 6.050756345e-01f, 4.370160244e-01f},
    std::array{1.414213562e+00f, 1.344997024e+00f, 1.144122806e+00f, 8.312538756e-01f, 4.370160244e-01f},
};


/* This calculates a first-order "upsampler" matrix. It combines a first-order
 * decoder matrix with a max-order encoder matrix, creating a matrix that
 * behaves as if the B-Format input signal is first decoded to a speaker array
 * at first-order, then those speaker feeds are encoded to a higher-order
 * signal. While not perfect, this should accurately encode a lower-order
 * signal into a higher-order signal.
 */
constexpr auto Order1Dec = std::array{
    std::array{1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f},
    std::array{1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f},
    std::array{1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f},
    std::array{1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f},
    std::array{1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f},
    std::array{1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f},
    std::array{1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f},
    std::array{1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f},
};
constexpr auto Order1Enc = std::array{
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
constexpr auto Order1Dec2D = std::array{
    std::array{1.666666667e-01f, -9.622504486e-02f, 0.0f,  1.666666667e-01f},
    std::array{1.666666667e-01f, -1.924500897e-01f, 0.0f,  0.000000000e+00f},
    std::array{1.666666667e-01f, -9.622504486e-02f, 0.0f, -1.666666667e-01f},
    std::array{1.666666667e-01f,  9.622504486e-02f, 0.0f, -1.666666667e-01f},
    std::array{1.666666667e-01f,  1.924500897e-01f, 0.0f,  0.000000000e+00f},
    std::array{1.666666667e-01f,  9.622504486e-02f, 0.0f,  1.666666667e-01f},
};
constexpr auto Order1Enc2D = std::array{
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
constexpr auto Order2Dec = std::array{
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
constexpr auto Order2Enc = std::array{
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
constexpr auto Order2Dec2D = std::array{
    std::array{1.250000000e-01f, -5.523559567e-02f, 0.0f,  1.333505242e-01f, -9.128709292e-02f, 0.0f, 0.0f, 0.0f,  9.128709292e-02f},
    std::array{1.250000000e-01f, -1.333505242e-01f, 0.0f,  5.523559567e-02f, -9.128709292e-02f, 0.0f, 0.0f, 0.0f, -9.128709292e-02f},
    std::array{1.250000000e-01f, -1.333505242e-01f, 0.0f, -5.523559567e-02f,  9.128709292e-02f, 0.0f, 0.0f, 0.0f, -9.128709292e-02f},
    std::array{1.250000000e-01f, -5.523559567e-02f, 0.0f, -1.333505242e-01f,  9.128709292e-02f, 0.0f, 0.0f, 0.0f,  9.128709292e-02f},
    std::array{1.250000000e-01f,  5.523559567e-02f, 0.0f, -1.333505242e-01f, -9.128709292e-02f, 0.0f, 0.0f, 0.0f,  9.128709292e-02f},
    std::array{1.250000000e-01f,  1.333505242e-01f, 0.0f, -5.523559567e-02f, -9.128709292e-02f, 0.0f, 0.0f, 0.0f, -9.128709292e-02f},
    std::array{1.250000000e-01f,  1.333505242e-01f, 0.0f,  5.523559567e-02f,  9.128709292e-02f, 0.0f, 0.0f, 0.0f, -9.128709292e-02f},
    std::array{1.250000000e-01f,  5.523559567e-02f, 0.0f,  1.333505242e-01f,  9.128709292e-02f, 0.0f, 0.0f, 0.0f,  9.128709292e-02f},
};
constexpr auto Order2Enc2D = std::array{
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
constexpr auto Order3Dec = std::array{
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
constexpr auto Order3Enc = std::array{
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
constexpr auto Order3Dec2D = std::array{
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
constexpr auto Order3Enc2D = std::array{
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
constexpr auto Order4Dec2D = std::array{
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
constexpr auto Order4Enc2D = std::array{
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


template<size_t N, size_t M>
constexpr auto CalcAmbiUpsampler(const std::array<std::array<float,N>,M> &decoder,
    const std::array<AmbiChannelFloatArray,M> &encoder) noexcept
{
    auto res = std::array<AmbiChannelFloatArray,N>{};

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

} // namespace

namespace AmbiScale {
constinit const UpsamplerArrays<4> FirstOrderUp{CalcAmbiUpsampler(Order1Dec, Order1Enc)};
constinit const UpsamplerArrays<4> FirstOrder2DUp{CalcAmbiUpsampler(Order1Dec2D, Order1Enc2D)};
constinit const UpsamplerArrays<9> SecondOrderUp{CalcAmbiUpsampler(Order2Dec, Order2Enc)};
constinit const UpsamplerArrays<9> SecondOrder2DUp{CalcAmbiUpsampler(Order2Dec2D, Order2Enc2D)};
constinit const UpsamplerArrays<16> ThirdOrderUp{CalcAmbiUpsampler(Order3Dec, Order3Enc)};
constinit const UpsamplerArrays<16> ThirdOrder2DUp{CalcAmbiUpsampler(Order3Dec2D, Order3Enc2D)};
constinit const UpsamplerArrays<25> FourthOrder2DUp{CalcAmbiUpsampler(Order4Dec2D, Order4Enc2D)};

auto GetHFOrderScales(const uint src_order, const uint dev_order, const bool horizontalOnly)
    noexcept -> std::array<float,MaxAmbiOrder+1>
{
    auto res = std::array<float,MaxAmbiOrder+1>{};

    const auto scales = horizontalOnly ? std::span{HFScales2D} : std::span{HFScales};
    std::ranges::transform(scales[src_order], scales[dev_order], res.begin(), std::divides{});

    return res;
}
} /* namespace AmbiScale */
