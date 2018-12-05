
#include "config.h"

#include "nfc.h"

#include <algorithm>

#include "alMain.h"

#include <string.h>


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

constexpr float B[4][3] = {
    {    0.0f                             },
    {    1.0f                             },
    {    3.0f,     3.0f                   },
    { 3.6778f,  6.4595f, 2.3222f          },
  /*{ 4.2076f, 11.4877f, 5.7924f, 9.1401f }*/
};

NfcFilter1 NfcFilterCreate1(const float w0, const float w1) noexcept
{
    NfcFilter1 nfc{};
    float b_00, g_0;
    float r;

    nfc.base_gain = 1.0f;
    nfc.gain = 1.0f;

    /* Calculate bass-boost coefficients. */
    r = 0.5f * w0;
    b_00 = B[1][0] * r;
    g_0 = 1.0f + b_00;

    nfc.gain *= g_0;
    nfc.b1 = 2.0f * b_00 / g_0;

    /* Calculate bass-cut coefficients. */
    r = 0.5f * w1;
    b_00 = B[1][0] * r;
    g_0 = 1.0f + b_00;

    nfc.base_gain /= g_0;
    nfc.gain /= g_0;
    nfc.a1 = 2.0f * b_00 / g_0;

    return nfc;
}

void NfcFilterAdjust1(NfcFilter1 *nfc, const float w0) noexcept
{
    float b_00, g_0;
    float r;

    r = 0.5f * w0;
    b_00 = B[1][0] * r;
    g_0 = 1.0f + b_00;

    nfc->gain = nfc->base_gain * g_0;
    nfc->b1 = 2.0f * b_00 / g_0;
}


NfcFilter2 NfcFilterCreate2(const float w0, const float w1) noexcept
{
    NfcFilter2 nfc{};
    float b_10, b_11, g_1;
    float r;

    nfc.base_gain = 1.0f;
    nfc.gain = 1.0f;

    /* Calculate bass-boost coefficients. */
    r = 0.5f * w0;
    b_10 = B[2][0] * r;
    b_11 = B[2][1] * r * r;
    g_1 = 1.0f + b_10 + b_11;

    nfc.gain *= g_1;
    nfc.b1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc.b2 = 4.0f * b_11 / g_1;

    /* Calculate bass-cut coefficients. */
    r = 0.5f * w1;
    b_10 = B[2][0] * r;
    b_11 = B[2][1] * r * r;
    g_1 = 1.0f + b_10 + b_11;

    nfc.base_gain /= g_1;
    nfc.gain /= g_1;
    nfc.a1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc.a2 = 4.0f * b_11 / g_1;

    return nfc;
}

void NfcFilterAdjust2(NfcFilter2 *nfc, const float w0) noexcept
{
    float b_10, b_11, g_1;
    float r;

    r = 0.5f * w0;
    b_10 = B[2][0] * r;
    b_11 = B[2][1] * r * r;
    g_1 = 1.0f + b_10 + b_11;

    nfc->gain = nfc->base_gain * g_1;
    nfc->b1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc->b2 = 4.0f * b_11 / g_1;
}


NfcFilter3 NfcFilterCreate3(const float w0, const float w1) noexcept
{
    NfcFilter3 nfc{};
    float b_10, b_11, g_1;
    float b_00, g_0;
    float r;

    nfc.base_gain = 1.0f;
    nfc.gain = 1.0f;

    /* Calculate bass-boost coefficients. */
    r = 0.5f * w0;
    b_10 = B[3][0] * r;
    b_11 = B[3][1] * r * r;
    g_1 = 1.0f + b_10 + b_11;

    nfc.gain *= g_1;
    nfc.b1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc.b2 = 4.0f * b_11 / g_1;

    b_00 = B[3][2] * r;
    g_0 = 1.0f + b_00;

    nfc.gain *= g_0;
    nfc.b3 = 2.0f * b_00 / g_0;

    /* Calculate bass-cut coefficients. */
    r = 0.5f * w1;
    b_10 = B[3][0] * r;
    b_11 = B[3][1] * r * r;
    g_1 = 1.0f + b_10 + b_11;

    nfc.base_gain /= g_1;
    nfc.gain /= g_1;
    nfc.a1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc.a2 = 4.0f * b_11 / g_1;

    b_00 = B[3][2] * r;
    g_0 = 1.0f + b_00;

    nfc.base_gain /= g_0;
    nfc.gain /= g_0;
    nfc.a3 = 2.0f * b_00 / g_0;

    return nfc;
}

void NfcFilterAdjust3(NfcFilter3 *nfc, const float w0) noexcept
{
    float b_10, b_11, g_1;
    float b_00, g_0;
    float r;

    r = 0.5f * w0;
    b_10 = B[3][0] * r;
    b_11 = B[3][1] * r * r;
    g_1 = 1.0f + b_10 + b_11;

    nfc->gain = nfc->base_gain * g_1;
    nfc->b1 = (2.0f*b_10 + 4.0f*b_11) / g_1;
    nfc->b2 = 4.0f * b_11 / g_1;

    b_00 = B[3][2] * r;
    g_0 = 1.0f + b_00;

    nfc->gain *= g_0;
    nfc->b3 = 2.0f * b_00 / g_0;
}

} // namespace

void NfcFilter::init(const float w0, const float w1) noexcept
{
    first = NfcFilterCreate1(w0, w1);
    second = NfcFilterCreate2(w0, w1);
    third = NfcFilterCreate3(w0, w1);
}

void NfcFilter::adjust(const float w0) noexcept
{
    NfcFilterAdjust1(&first, w0);
    NfcFilterAdjust2(&second, w0);
    NfcFilterAdjust3(&third, w0);
}


void NfcFilter::process1(float *RESTRICT dst, const float *RESTRICT src, const int count)
{
    ASSUME(count > 0);

    const float gain{first.gain};
    const float b1{first.b1};
    const float a1{first.a1};
    float z1{first.z[0]};
    auto proc_sample = [gain,b1,a1,&z1](const float in) noexcept -> float
    {
        const float y{in*gain - a1*z1};
        const float out{y + b1*z1};
        z1 += y;
        return out;
    };
    std::transform(src, src+count, dst, proc_sample);
    first.z[0] = z1;
}

void NfcFilter::process2(float *RESTRICT dst, const float *RESTRICT src, const int count)
{
    ASSUME(count > 0);

    const float gain{second.gain};
    const float b1{second.b1};
    const float b2{second.b2};
    const float a1{second.a1};
    const float a2{second.a2};
    float z1{second.z[0]};
    float z2{second.z[1]};
    auto proc_sample = [gain,b1,b2,a1,a2,&z1,&z2](const float in) noexcept -> float
    {
        const float y{in*gain - a1*z1 - a2*z2};
        const float out{y + b1*z1 + b2*z2};
        z2 += z1;
        z1 += y;
        return out;
    };
    std::transform(src, src+count, dst, proc_sample);
    second.z[0] = z1;
    second.z[1] = z2;
}

void NfcFilter::process3(float *RESTRICT dst, const float *RESTRICT src, const int count)
{
    ASSUME(count > 0);

    const float gain{third.gain};
    const float b1{third.b1};
    const float b2{third.b2};
    const float b3{third.b3};
    const float a1{third.a1};
    const float a2{third.a2};
    const float a3{third.a3};
    float z1{third.z[0]};
    float z2{third.z[1]};
    float z3{third.z[2]};
    auto proc_sample = [gain,b1,b2,b3,a1,a2,a3,&z1,&z2,&z3](const float in) noexcept -> float
    {
        float y{in*gain - a1*z1 - a2*z2};
        float out{y + b1*z1 + b2*z2};
        z2 += z1;
        z1 += y;

        y = out - a3*z3;
        out = y + b3*z3;
        z3 += y;
        return out;
    };
    std::transform(src, src+count, dst, proc_sample);
    third.z[0] = z1;
    third.z[1] = z2;
    third.z[2] = z3;
}

#if 0 /* Original methods the above are derived from. */
static void NfcFilterCreate(NfcFilter *nfc, const ALsizei order, const float src_dist, const float ctl_dist, const float rate)
{
    static const float B[4][5] = {
        {                                     },
        {    1.0f                             },
        {    3.0f,     3.0f                   },
        { 3.6778f,  6.4595f, 2.3222f          },
        { 4.2076f, 11.4877f, 5.7924f, 9.1401f }
    };
    float w0 = SPEEDOFSOUNDMETRESPERSEC / (src_dist * rate);
    float w1 = SPEEDOFSOUNDMETRESPERSEC / (ctl_dist * rate);
    ALsizei i;
    float r;

    nfc->g = 1.0f;
    nfc->coeffs[0] = 1.0f;

    /* NOTE: Slight adjustment from the literature to raise the center
     * frequency a bit (0.5 -> 1.0).
     */
    r = 1.0f * w0;
    for(i = 0; i < (order-1);i += 2)
    {
        float b_10 = B[order][i  ] * r;
        float b_11 = B[order][i+1] * r * r;
        float g_1 = 1.0f + b_10 + b_11;

        nfc->b[i] = b_10;
        nfc->b[i + 1] = b_11;
        nfc->coeffs[0] *= g_1;
        nfc->coeffs[i+1] = ((2.0f * b_10) + (4.0f * b_11)) / g_1;
        nfc->coeffs[i+2] = (4.0f * b_11) / g_1;
    }
    if(i < order)
    {
        float b_00 = B[order][i] * r;
        float g_0 = 1.0f + b_00;

        nfc->b[i] = b_00;
        nfc->coeffs[0] *= g_0;
        nfc->coeffs[i+1] = (2.0f * b_00) / g_0;
    }

    r = 1.0f * w1;
    for(i = 0;i < (order-1);i += 2)
    {
        float b_10 = B[order][i  ] * r;
        float b_11 = B[order][i+1] * r * r;
        float g_1 = 1.0f + b_10 + b_11;

        nfc->g /= g_1;
        nfc->coeffs[0] /= g_1;
        nfc->coeffs[order+i+1] = ((2.0f * b_10) + (4.0f * b_11)) / g_1;
        nfc->coeffs[order+i+2] = (4.0f * b_11) / g_1;
    }
    if(i < order)
    {
        float b_00 = B[order][i] * r;
        float g_0 = 1.0f + b_00;

        nfc->g /= g_0;
        nfc->coeffs[0] /= g_0;
        nfc->coeffs[order+i+1] = (2.0f * b_00) / g_0;
    }

    for(i = 0; i < MAX_AMBI_ORDER; i++)
        nfc->history[i] = 0.0f;
}

static void NfcFilterAdjust(NfcFilter *nfc, const float distance)
{
    int i;

    nfc->coeffs[0] = nfc->g;

    for(i = 0;i < (nfc->order-1);i += 2)
    {
        float b_10 = nfc->b[i] / distance;
        float b_11 = nfc->b[i+1] / (distance * distance);
        float g_1 = 1.0f + b_10 + b_11;

        nfc->coeffs[0] *= g_1;
        nfc->coeffs[i+1] = ((2.0f * b_10) + (4.0f * b_11)) / g_1;
        nfc->coeffs[i+2] = (4.0f * b_11) / g_1;
    }
    if(i < nfc->order)
    {
        float b_00 = nfc->b[i] / distance;
        float g_0 = 1.0f + b_00;

        nfc->coeffs[0] *= g_0;
        nfc->coeffs[i+1] = (2.0f * b_00) / g_0;
    }
}

static float NfcFilterProcess(const float in, NfcFilter *nfc)
{
    int i;
    float out = in * nfc->coeffs[0];

    for(i = 0;i < (nfc->order-1);i += 2)
    {
        float y = out - (nfc->coeffs[nfc->order+i+1] * nfc->history[i]) -
                        (nfc->coeffs[nfc->order+i+2] * nfc->history[i+1]) + 1.0e-30f;
        out = y + (nfc->coeffs[i+1]*nfc->history[i]) + (nfc->coeffs[i+2]*nfc->history[i+1]);

        nfc->history[i+1] += nfc->history[i];
        nfc->history[i] += y;
    }
    if(i < nfc->order)
    {
        float y = out - (nfc->coeffs[nfc->order+i+1] * nfc->history[i]) + 1.0e-30f;

        out = y + (nfc->coeffs[i+1] * nfc->history[i]);
        nfc->history[i] += y;
    }

    return out;
}
#endif
