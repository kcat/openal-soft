#ifndef ALU_H
#define ALU_H

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "al/buffer.h"
#include "alcmain.h"
#include "almalloc.h"
#include "alspan.h"
#include "ambidefs.h"
#include "devformat.h"
#include "filters/biquad.h"
#include "filters/nfc.h"
#include "filters/splitter.h"
#include "hrtf.h"
#include "logging.h"

struct ALbufferlistitem;
struct ALeffectslot;

enum class DistanceModel;


#define MAX_PITCH  255
#define MAX_SENDS  16


#define DITHER_RNG_SEED 22222


using MixerFunc = void(*)(const al::span<const float> InSamples,
    const al::span<FloatBufferLine> OutBuffer, float *CurrentGains, const float *TargetGains,
    const size_t Counter, const size_t OutPos);
using RowMixerFunc = void(*)(const al::span<float> OutBuffer, const al::span<const float> Gains,
    const float *InSamples, const size_t InStride);
using HrtfDirectMixerFunc = void(*)(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State,
    const size_t BufferSize);

extern MixerFunc MixSamples;
extern RowMixerFunc MixRowSamples;


#define GAIN_MIX_MAX  (1000.0f) /* +60dB */

#define GAIN_SILENCE_THRESHOLD  (0.00001f) /* -100dB */

#define SPEEDOFSOUNDMETRESPERSEC  (343.3f)
#define AIRABSORBGAINHF           (0.99426f) /* -0.05dB */

/* Target gain for the reverb decay feedback reaching the decay time. */
#define REVERB_DECAY_GAIN  (0.001f) /* -60 dB */

#define FRACTIONBITS (12)
#define FRACTIONONE  (1<<FRACTIONBITS)
#define FRACTIONMASK (FRACTIONONE-1)


inline ALfloat lerp(ALfloat val1, ALfloat val2, ALfloat mu) noexcept
{ return val1 + (val2-val1)*mu; }
inline ALfloat cubic(ALfloat val1, ALfloat val2, ALfloat val3, ALfloat val4, ALfloat mu) noexcept
{
    ALfloat mu2 = mu*mu, mu3 = mu2*mu;
    ALfloat a0 = -0.5f*mu3 +       mu2 + -0.5f*mu;
    ALfloat a1 =  1.5f*mu3 + -2.5f*mu2            + 1.0f;
    ALfloat a2 = -1.5f*mu3 +  2.0f*mu2 +  0.5f*mu;
    ALfloat a3 =  0.5f*mu3 + -0.5f*mu2;
    return val1*a0 + val2*a1 + val3*a2 + val4*a3;
}


enum HrtfRequestMode {
    Hrtf_Default = 0,
    Hrtf_Enable = 1,
    Hrtf_Disable = 2,
};

void aluInit(void);

void aluInitMixer(void);

/* aluInitRenderer
 *
 * Set up the appropriate panning method and mixing method given the device
 * properties.
 */
void aluInitRenderer(ALCdevice *device, ALint hrtf_id, HrtfRequestMode hrtf_appreq, HrtfRequestMode hrtf_userreq);

void aluInitEffectPanning(ALeffectslot *slot, ALCdevice *device);

/**
 * Calculates ambisonic encoder coefficients using the X, Y, and Z direction
 * components, which must represent a normalized (unit length) vector, and the
 * spread is the angular width of the sound (0...tau).
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
void CalcAmbiCoeffs(const ALfloat y, const ALfloat z, const ALfloat x, const ALfloat spread,
                    ALfloat (&coeffs)[MAX_AMBI_CHANNELS]);

/**
 * CalcDirectionCoeffs
 *
 * Calculates ambisonic coefficients based on an OpenAL direction vector. The
 * vector must be normalized (unit length), and the spread is the angular width
 * of the sound (0...tau).
 */
inline void CalcDirectionCoeffs(const ALfloat (&dir)[3], ALfloat spread, ALfloat (&coeffs)[MAX_AMBI_CHANNELS])
{
    /* Convert from OpenAL coords to Ambisonics. */
    CalcAmbiCoeffs(-dir[0], dir[1], -dir[2], spread, coeffs);
}

/**
 * CalcAngleCoeffs
 *
 * Calculates ambisonic coefficients based on azimuth and elevation. The
 * azimuth and elevation parameters are in radians, going right and up
 * respectively.
 */
inline void CalcAngleCoeffs(ALfloat azimuth, ALfloat elevation, ALfloat spread, ALfloat (&coeffs)[MAX_AMBI_CHANNELS])
{
    ALfloat x = -std::sin(azimuth) * std::cos(elevation);
    ALfloat y = std::sin(elevation);
    ALfloat z = std::cos(azimuth) * std::cos(elevation);

    CalcAmbiCoeffs(x, y, z, spread, coeffs);
}


/**
 * ComputePanGains
 *
 * Computes panning gains using the given channel decoder coefficients and the
 * pre-calculated direction or angle coefficients. For B-Format sources, the
 * coeffs are a 'slice' of a transform matrix for the input channel, used to
 * scale and orient the sound samples.
 */
void ComputePanGains(const MixParams *mix, const ALfloat*RESTRICT coeffs, ALfloat ingain, ALfloat (&gains)[MAX_OUTPUT_CHANNELS]);


inline std::array<ALfloat,MAX_AMBI_CHANNELS> GetAmbiIdentityRow(size_t i) noexcept
{
    std::array<ALfloat,MAX_AMBI_CHANNELS> ret{};
    ret[i] = 1.0f;
    return ret;
}


void aluMixData(ALCdevice *device, ALvoid *OutBuffer, const ALuint NumSamples);
/* Caller must lock the device state, and the mixer must not be running. */
void aluHandleDisconnect(ALCdevice *device, const char *msg, ...) DECL_FORMAT(printf, 2, 3);

extern const ALfloat ConeScale;
extern const ALfloat ZScale;

#endif
