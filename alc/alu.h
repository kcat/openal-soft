#ifndef ALU_H
#define ALU_H

#include <array>
#include <cmath>
#include <cstddef>

#include "AL/al.h"

#include "alcmain.h"
#include "alspan.h"

struct ALbufferlistitem;
struct ALeffectslot;


#define MAX_PITCH  255
#define MAX_SENDS  6


using MixerFunc = void(*)(const al::span<const float> InSamples,
    const al::span<FloatBufferLine> OutBuffer, float *CurrentGains, const float *TargetGains,
    const size_t Counter, const size_t OutPos);

extern MixerFunc MixSamples;


#define GAIN_MIX_MAX  1000.0f /* +60dB */

#define GAIN_SILENCE_THRESHOLD  0.00001f /* -100dB */

#define SPEEDOFSOUNDMETRESPERSEC  343.3f
#define AIRABSORBGAINHF           0.99426f /* -0.05dB */

/* Target gain for the reverb decay feedback reaching the decay time. */
#define REVERB_DECAY_GAIN  0.001f /* -60 dB */

#define FRACTIONBITS 12
#define FRACTIONONE  (1<<FRACTIONBITS)
#define FRACTIONMASK (FRACTIONONE-1)


inline float lerp(float val1, float val2, float mu) noexcept
{ return val1 + (val2-val1)*mu; }
inline float cubic(float val1, float val2, float val3, float val4, float mu) noexcept
{
    const float mu2{mu*mu}, mu3{mu2*mu};
    const float a0{-0.5f*mu3 +       mu2 + -0.5f*mu};
    const float a1{ 1.5f*mu3 + -2.5f*mu2            + 1.0f};
    const float a2{-1.5f*mu3 +  2.0f*mu2 +  0.5f*mu};
    const float a3{ 0.5f*mu3 + -0.5f*mu2};
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
void aluInitRenderer(ALCdevice *device, int hrtf_id, HrtfRequestMode hrtf_appreq,
    HrtfRequestMode hrtf_userreq);

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
std::array<float,MAX_AMBI_CHANNELS> CalcAmbiCoeffs(const float y, const float z, const float x,
    const float spread);

/**
 * CalcDirectionCoeffs
 *
 * Calculates ambisonic coefficients based on an OpenAL direction vector. The
 * vector must be normalized (unit length), and the spread is the angular width
 * of the sound (0...tau).
 */
inline std::array<float,MAX_AMBI_CHANNELS> CalcDirectionCoeffs(const float (&dir)[3],
    const float spread)
{
    /* Convert from OpenAL coords to Ambisonics. */
    return CalcAmbiCoeffs(-dir[0], dir[1], -dir[2], spread);
}

/**
 * CalcAngleCoeffs
 *
 * Calculates ambisonic coefficients based on azimuth and elevation. The
 * azimuth and elevation parameters are in radians, going right and up
 * respectively.
 */
inline std::array<float,MAX_AMBI_CHANNELS> CalcAngleCoeffs(const float azimuth,
    const float elevation, const float spread)
{
    const float x{-std::sin(azimuth) * std::cos(elevation)};
    const float y{ std::sin(elevation)};
    const float z{ std::cos(azimuth) * std::cos(elevation)};

    return CalcAmbiCoeffs(x, y, z, spread);
}


/**
 * ComputePanGains
 *
 * Computes panning gains using the given channel decoder coefficients and the
 * pre-calculated direction or angle coefficients. For B-Format sources, the
 * coeffs are a 'slice' of a transform matrix for the input channel, used to
 * scale and orient the sound samples.
 */
void ComputePanGains(const MixParams *mix, const float*RESTRICT coeffs, const float ingain,
    const al::span<float,MAX_OUTPUT_CHANNELS> gains);


inline std::array<float,MAX_AMBI_CHANNELS> GetAmbiIdentityRow(size_t i) noexcept
{
    std::array<float,MAX_AMBI_CHANNELS> ret{};
    ret[i] = 1.0f;
    return ret;
}


void aluMixData(ALCdevice *device, void *OutBuffer, const ALuint NumSamples,
    const size_t FrameStep);
/* Caller must lock the device state, and the mixer must not be running. */
[[gnu::format(printf,2,3)]] void aluHandleDisconnect(ALCdevice *device, const char *msg, ...);

extern const float ConeScale;
extern const float ZScale;

#endif
