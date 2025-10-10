
#include "config.h"

#include "mixer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "core/ambidefs.h"
#include "device.h"
#include "mixer/defs.h"


auto CalcAmbiCoeffs(const float y, const float z, const float x, const float spread)
    -> std::array<float,MaxAmbiChannels>
{
    auto coeffs = CalcAmbiCoeffs(y, z, x);

    if(spread > 0.0f)
    {
        /* Implement the spread by using a spherical source that subtends the
         * angle spread. See:
         * http://www.ppsloan.org/publications/StupidSH36.pdf - Appendix A3
         *
         * When adjusted for N3D normalization instead of SN3D, these
         * calculations are:
         *
         * ZH0 = -sqrt(pi) * (-1+ca);
         * ZH1 =  0.5*sqrt(pi) * sa*sa;
         * ZH2 = -0.5*sqrt(pi) * ca*(-1+ca)*(ca+1);
         * ZH3 = -0.125*sqrt(pi) * (-1+ca)*(ca+1)*(5*ca*ca - 1);
         * ZH4 = -0.125*sqrt(pi) * ca*(-1+ca)*(ca+1)*(7*ca*ca - 3);
         * ZH5 = -0.0625*sqrt(pi) * (-1+ca)*(ca+1)*(21*ca*ca*ca*ca - 14*ca*ca + 1);
         *
         * The gain of the source is compensated for size, so that the
         * loudness doesn't depend on the spread. Thus:
         *
         * ZH0 = 1.0f;
         * ZH1 = 0.5f * (ca+1.0f);
         * ZH2 = 0.5f * (ca+1.0f)*ca;
         * ZH3 = 0.125f * (ca+1.0f)*(5.0f*ca*ca - 1.0f);
         * ZH4 = 0.125f * (ca+1.0f)*(7.0f*ca*ca - 3.0f)*ca;
         * ZH5 = 0.0625f * (ca+1.0f)*(21.0f*ca*ca*ca*ca - 14.0f*ca*ca + 1.0f);
         */
        const auto ca = std::cos(spread * 0.5f);
        /* Increase the source volume by up to +3dB for a full spread. */
        const auto scale = std::sqrt(1.0f + std::numbers::inv_pi_v<float>*0.5f*spread);
        const auto caca = ca*ca;

        const auto ZH0_norm = scale;
        const auto ZH1_norm = scale * 0.5f * (ca+1.0f);
        const auto ZH2_norm = scale * 0.5f * ((ca+1.0f)*ca);
        const auto ZH3_norm = scale * 0.125f * ((ca+1.0f)*(5.0f*caca - 1.0f));
        const auto ZH4_norm = scale * 0.125f * ((ca+1.0f)*(7.0f*caca - 3.0f)*ca);

        /* Zeroth-order */
        coeffs[0]  *= ZH0_norm;
        /* First-order */
        coeffs[1]  *= ZH1_norm;
        coeffs[2]  *= ZH1_norm;
        coeffs[3]  *= ZH1_norm;
        /* Second-order */
        coeffs[4]  *= ZH2_norm;
        coeffs[5]  *= ZH2_norm;
        coeffs[6]  *= ZH2_norm;
        coeffs[7]  *= ZH2_norm;
        coeffs[8]  *= ZH2_norm;
        /* Third-order */
        coeffs[9]  *= ZH3_norm;
        coeffs[10] *= ZH3_norm;
        coeffs[11] *= ZH3_norm;
        coeffs[12] *= ZH3_norm;
        coeffs[13] *= ZH3_norm;
        coeffs[14] *= ZH3_norm;
        coeffs[15] *= ZH3_norm;
        /* Fourth-order */
        coeffs[16] *= ZH4_norm;
        coeffs[17] *= ZH4_norm;
        coeffs[18] *= ZH4_norm;
        coeffs[19] *= ZH4_norm;
        coeffs[20] *= ZH4_norm;
        coeffs[21] *= ZH4_norm;
        coeffs[22] *= ZH4_norm;
        coeffs[23] *= ZH4_norm;
        coeffs[24] *= ZH4_norm;
    }

    return coeffs;
}

void ComputePanGains(const MixParams *mix, const std::span<const float,MaxAmbiChannels> coeffs,
    const float ingain, const std::span<float,MaxAmbiChannels> gains)
{
    auto ambimap = std::span{std::as_const(mix->AmbiMap)}.first(mix->Buffer.size());

    const auto iter = std::ranges::transform(ambimap, gains.begin(),
        [coeffs,ingain](const BFChannelConfig &chanmap) noexcept -> float
        { return chanmap.Scale * coeffs[chanmap.Index] * ingain; });
    std::fill(iter.out, gains.end(), 0.0f);
}
