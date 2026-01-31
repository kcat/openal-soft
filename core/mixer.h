#ifndef CORE_MIXER_H
#define CORE_MIXER_H

#include <array>
#include <cmath>
#include <span>

#include "alnumeric.h"
#include "ambidefs.h"
#include "bufferline.h"
#include "opthelpers.h"

struct MixParams;

void Mix_C(std::span<float const> InSamples, std::span<FloatBufferLine> OutBuffer,
    std::span<float> CurrentGains, std::span<float const> TargetGains, usize Counter,
    usize OutPos);
void Mix_C(std::span<float const> InSamples, std::span<float> OutBuffer, float &CurrentGain,
    float TargetGain, usize Counter);

/* Mixer functions that handle one input and multiple output channels. */
using MixerOutFunc = void(*)(std::span<float const> InSamples,
    std::span<FloatBufferLine> OutBuffer, std::span<float> CurrentGains,
    std::span<float const> TargetGains, usize Counter, usize OutPos);

inline constinit auto MixSamplesOut = MixerOutFunc{Mix_C};
inline void MixSamples(std::span<float const> const InSamples,
    std::span<FloatBufferLine> const OutBuffer, std::span<float> const CurrentGains,
    std::span<float const> const TargetGains, usize const Counter, usize const OutPos)
{ MixSamplesOut(InSamples, OutBuffer, CurrentGains, TargetGains, Counter, OutPos); }

/* Mixer functions that handle one input and one output channel. */
using MixerOneFunc = void(*)(std::span<float const> InSamples, std::span<float> OutBuffer,
    float &CurrentGain, float TargetGain, usize Counter);

inline constinit auto MixSamplesOne = MixerOneFunc{Mix_C};
inline void MixSamples(std::span<float const> const InSamples, std::span<float> const OutBuffer,
    float &CurrentGain, float const TargetGain, usize const Counter)
{ MixSamplesOne(InSamples, OutBuffer, CurrentGain, TargetGain, Counter); }


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
std::array<float,MaxAmbiChannels> CalcAmbiCoeffs(float y, float z, float x, float spread);

/**
 * CalcDirectionCoeffs
 *
 * Calculates ambisonic coefficients based on an OpenAL direction vector. The
 * vector must be normalized (unit length), and the spread is the angular width
 * of the sound (0...tau).
 */
inline auto CalcDirectionCoeffs(const std::span<const float,3> dir, const float spread)
    -> std::array<float,MaxAmbiChannels>
{
    /* Convert from OpenAL coords to Ambisonics. */
    return CalcAmbiCoeffs(-dir[0], dir[1], -dir[2], spread);
}

/**
 * CalcDirectionCoeffs
 *
 * Calculates ambisonic coefficients based on an OpenAL direction vector. The
 * vector must be normalized (unit length).
 */
constexpr auto CalcDirectionCoeffs(const std::span<const float,3> dir)
    -> std::array<float,MaxAmbiChannels>
{
    /* Convert from OpenAL coords to Ambisonics. */
    return CalcAmbiCoeffs(-dir[0], dir[1], -dir[2]);
}

/**
 * CalcAngleCoeffs
 *
 * Calculates ambisonic coefficients based on azimuth and elevation. The
 * azimuth and elevation parameters are in radians, going right and up
 * respectively.
 */
inline auto CalcAngleCoeffs(const float azimuth, const float elevation, const float spread)
    -> std::array<float,MaxAmbiChannels>
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
void ComputePanGains(const MixParams *mix, const std::span<const float,MaxAmbiChannels> coeffs,
    const float ingain, const std::span<float,MaxAmbiChannels> gains);

#endif /* CORE_MIXER_H */
