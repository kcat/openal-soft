
#include "config.h"

#include "nfc.h"

#include <algorithm>

#include "alnumeric.h"


/* Near-field control filters are the basis for handling the near-field effect.
 * The near-field effect is a bass-boost present in the directional components
 * of a recorded signal, created as a result of the wavefront curvature (itself
 * a function of sound distance). Proper reproduction dictates this be
 * compensated for using a bass-cut given the playback speaker distance, to
 * avoid excessive bass in the playback.
 *
 * For real-time rendered audio, emulating the near-field effect based on the
 * sound source's distance, and subsequently compensating for it at output
 * based on the speaker distances, can create a more realistic perception of
 * sound distance beyond a simple 1/r attenuation.
 *
 * These filters do just that. Each one applies a low-shelf filter, created as
 * the combination of a bass-boost for a given sound source distance (near-
 * field emulation) along with a bass-cut for a given control/speaker distance
 * (near-field compensation).
 *
 * Note that it is necessary to apply a cut along with the boost, since the
 * boost alone is unstable in higher-order ambisonics as it causes an infinite
 * DC gain (even first-order ambisonics requires there to be no DC offset for
 * the boost to work). Consequently, ambisonics requires a control parameter to
 * be used to avoid an unstable boost-only filter. NFC-HOA defines this control
 * as a reference delay, calculated with:
 *
 * reference_delay = control_distance / speed_of_sound
 *
 * This means w0 (for input) or w1 (for output) should be set to:
 *
 * wN = 1 / (reference_delay * sample_rate)
 *
 * when dealing with NFC-HOA content. For FOA input content, which does not
 * specify a reference_delay variable, w0 should be set to 0 to apply only
 * near-field compensation for output. It's important that w1 be a finite,
 * positive, non-0 value or else the bass-boost will become unstable again.
 * Also, w0 should not be too large compared to w1, to avoid excessively loud
 * low frequencies.
 */

namespace {

constexpr auto B1 = std::array{   1.0f};
constexpr auto B2 = std::array{   3.0f,     3.0f};
constexpr auto B3 = std::array{3.6778f,  6.4595f, 2.3222f};
constexpr auto B4 = std::array{4.2076f, 11.4877f, 5.7924f, 9.1401f};

auto NfcFilterCreate1(f32 const w1) noexcept -> NfcFilter1
{
    auto nfc = NfcFilter1{};

    /* Calculate bass-cut coefficients. */
    auto const r = 0.5f * w1;
    auto const b_00 = B1[0] * r;
    auto const g_0 = 1.0f + b_00;

    nfc.mBaseGain = 1.0f / g_0;
    nfc.mCoeffs.a1 = 2.0f * b_00 / g_0;

    /* Calculate bass-boost coefficients (matches bass-cut for passthrough). */
    nfc.mCoeffs.a0 = 1.0f;
    nfc.mCoeffs.b1 = nfc.mCoeffs.a1;

    return nfc;
}

void NfcFilterAdjust1(NfcFilter1 *const nfc, f32 const w0) noexcept
{
    auto const r = 0.5f * w0;
    auto const b_00 = B1[0] * r;
    auto const g_0 = 1.0f + b_00;

    nfc->mCoeffs.a0 = nfc->mBaseGain * g_0;
    nfc->mCoeffs.b1 = 2.0f * b_00 / g_0;
}


auto NfcFilterCreate2(f32 const w1) noexcept -> NfcFilter2
{
    auto nfc = NfcFilter2{};

    auto const r = 0.5f * w1;
    auto const b_10 = B2[0] * r;
    auto const b_11 = B2[1] * (r*r);
    auto const g_1 = 1.0f + b_10 + b_11;

    nfc.mBaseGain = 1.0f / g_1;
    nfc.mCoeffs.a1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc.mCoeffs.a2 = 4.0f * b_11 / g_1;

    nfc.mCoeffs.a0 = 1.0f;
    nfc.mCoeffs.b1 = nfc.mCoeffs.a1;
    nfc.mCoeffs.b2 = nfc.mCoeffs.a2;

    return nfc;
}

void NfcFilterAdjust2(NfcFilter2 *const nfc, f32 const w0) noexcept
{
    const auto r = 0.5f * w0;
    const auto b_10 = B2[0] * r;
    const auto b_11 = B2[1] * (r*r);
    const auto g_1 = 1.0f + b_10 + b_11;

    nfc->mCoeffs.a0 = nfc->mBaseGain * g_1;
    nfc->mCoeffs.b1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc->mCoeffs.b2 = 4.0f * b_11 / g_1;
}


auto NfcFilterCreate3(f32 const w1) noexcept -> NfcFilter3
{
    auto nfc = NfcFilter3{};

    auto const r = 0.5f * w1;
    auto const b_10 = B3[0] * r;
    auto const b_11 = B3[1] * (r*r);
    auto const b_00 = B3[2] * r;
    auto const g_1 = 1.0f + b_10 + b_11;
    auto const g_0 = 1.0f + b_00;

    nfc.mBaseGain = 1.0f / (g_1 * g_0);
    nfc.mCoeffs.a1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc.mCoeffs.a2 = 4.0f * b_11 / g_1;
    nfc.mCoeffs.a3 = 2.0f * b_00 / g_0;

    nfc.mCoeffs.a0 = 1.0f;
    nfc.mCoeffs.b1 = nfc.mCoeffs.a1;
    nfc.mCoeffs.b2 = nfc.mCoeffs.a2;
    nfc.mCoeffs.b3 = nfc.mCoeffs.a3;

    return nfc;
}

void NfcFilterAdjust3(NfcFilter3 *const nfc, f32 const w0) noexcept
{
    auto const r = 0.5f * w0;
    auto const b_10 = B3[0] * r;
    auto const b_11 = B3[1] * (r*r);
    auto const b_00 = B3[2] * r;
    auto const g_1 = 1.0f + b_10 + b_11;
    auto const g_0 = 1.0f + b_00;

    nfc->mCoeffs.a0 = nfc->mBaseGain * (g_1 * g_0);
    nfc->mCoeffs.b1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc->mCoeffs.b2 = 4.0f * b_11 / g_1;
    nfc->mCoeffs.b3 = 2.0f * b_00 / g_0;
}


auto NfcFilterCreate4(f32 const w1) noexcept -> NfcFilter4
{
    auto nfc = NfcFilter4{};

    auto const r = 0.5f * w1;
    auto const b_10 = B4[0] * r;
    auto const b_11 = B4[1] * (r*r);
    auto const b_00 = B4[2] * r;
    auto const b_01 = B4[3] * (r*r);
    auto const g_1 = 1.0f + b_10 + b_11;
    auto const g_0 = 1.0f + b_00 + b_01;

    nfc.mBaseGain = 1.0f / (g_1 * g_0);
    nfc.mCoeffs.a1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc.mCoeffs.a2 = 4.0f * b_11 / g_1;
    nfc.mCoeffs.a3 = (2.0f*b_00 + 4.0f*b_01) / g_0;
    nfc.mCoeffs.a4 = 4.0f * b_01 / g_0;

    nfc.mCoeffs.a0 = 1.0f;
    nfc.mCoeffs.b1 = nfc.mCoeffs.a1;
    nfc.mCoeffs.b2 = nfc.mCoeffs.a2;
    nfc.mCoeffs.b3 = nfc.mCoeffs.a3;
    nfc.mCoeffs.b4 = nfc.mCoeffs.a4;

    return nfc;
}

void NfcFilterAdjust4(NfcFilter4 *const nfc, f32 const w0) noexcept
{
    auto const r = 0.5f * w0;
    auto const b_10 = B4[0] * r;
    auto const b_11 = B4[1] * (r*r);
    auto const b_00 = B4[2] * r;
    auto const b_01 = B4[3] * (r*r);
    auto const g_1 = 1.0f + b_10 + b_11;
    auto const g_0 = 1.0f + b_00 + b_01;

    nfc->mCoeffs.a0 = nfc->mBaseGain * (g_1 * g_0);
    nfc->mCoeffs.b1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc->mCoeffs.b2 = 4.0f * b_11 / g_1;
    nfc->mCoeffs.b3 = (2.0f*b_00 + 4.0f*b_01) / g_0;
    nfc->mCoeffs.b4 = 4.0f * b_01 / g_0;
}

} // namespace

void NfcFilter::init(f32 const w1) noexcept
{
    first = NfcFilterCreate1(w1);
    second = NfcFilterCreate2(w1);
    third = NfcFilterCreate3(w1);
    fourth = NfcFilterCreate4(w1);
}

void NfcFilter::adjust(f32 const w0) noexcept
{
    NfcFilterAdjust1(&first, w0);
    NfcFilterAdjust2(&second, w0);
    NfcFilterAdjust3(&third, w0);
    NfcFilterAdjust4(&fourth, w0);
}


void NfcFilter1::process(std::span<f32 const> const src, std::span<f32> const dst)
{
    auto z = mZ;
    std::ranges::transform(src, dst.begin(), [coeffs0=mCoeffs, &z](f32 const in) noexcept -> f32
    {
        auto const y = in*coeffs0.a0 - coeffs0.a1*z[0];
        auto const out = y + coeffs0.b1*z[0];
        z[0] += y;
        return out;
    });
    mZ = z;
}

void NfcFilter2::process(std::span<f32 const> const src, std::span<f32> const dst)
{
    auto z = mZ;
    std::ranges::transform(src, dst.begin(), [coeffs=mCoeffs,&z](f32 const in) noexcept -> f32
    {
        auto const y = in*coeffs.a0 - coeffs.a1*z[0] - coeffs.a2*z[1];
        auto const out = y + coeffs.b1*z[0] + coeffs.b2*z[1];
        z[1] += z[0];
        z[0] += y;
        return out;
    });
    mZ = z;
}

void NfcFilter3::process(std::span<f32 const> const src, std::span<f32> const dst)
{
    auto z = mZ;
    std::ranges::transform(src, dst.begin(), [coeffs=mCoeffs,&z](f32 const in) noexcept -> f32
    {
        auto const y0 = in*coeffs.a0 - coeffs.a1*z[0] - coeffs.a2*z[1];
        auto const out0 = y0 + coeffs.b1*z[0] + coeffs.b2*z[1];
        z[1] += z[0];
        z[0] += y0;

        auto const y1 = out0 - coeffs.a3*z[2];
        auto const out1 = y1 + coeffs.b3*z[2];
        z[2] += y1;
        return out1;
    });
    mZ = z;
}

void NfcFilter4::process(std::span<f32 const> const src, std::span<f32> const dst)
{
    auto z = mZ;
    std::ranges::transform(src, dst.begin(), [coeffs=mCoeffs,&z](f32 const in) noexcept -> f32
    {
        auto const y0 = in*coeffs.a0 - coeffs.a1*z[0] - coeffs.a2*z[1];
        auto const out0 = y0 + coeffs.b1*z[0] + coeffs.b2*z[1];
        z[1] += z[0];
        z[0] += y0;

        auto const y1 = out0 - coeffs.a3*z[2] - coeffs.a4*z[3];
        auto const out1 = y1 + coeffs.b3*z[2] + coeffs.b4*z[3];
        z[3] += z[2];
        z[2] += y1;
        return out1;
    });
    mZ = z;
}
