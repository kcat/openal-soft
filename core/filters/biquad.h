#ifndef CORE_FILTERS_BIQUAD_H
#define CORE_FILTERS_BIQUAD_H

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>

#include "alnumeric.h"


/* Filters implementation is based on the "Cookbook formulae for audio
 * EQ biquad filter coefficients" by Robert Bristow-Johnson
 * http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
 */
/* Implementation note: For the shelf and peaking filters, the specified gain
 * is for the centerpoint of the transition band. This better fits EFX filter
 * behavior, which expects the shelf's reference frequency to reach the given
 * gain. To set the gain for the shelf or peak itself, use the square root of
 * the desired linear gain (or halve the dB gain).
 */

enum class BiquadType {
    /** EFX-style low-pass filter, specifying a gain and reference frequency. */
    HighShelf,
    /** EFX-style high-pass filter, specifying a gain and reference frequency. */
    LowShelf,
    /** Peaking filter, specifying a gain and reference frequency. */
    Peaking,

    /** Low-pass cut-off filter, specifying a cut-off frequency. */
    LowPass,
    /** High-pass cut-off filter, specifying a cut-off frequency. */
    HighPass,
    /** Band-pass filter, specifying a center frequency. */
    BandPass,
};

class BiquadFilter {
protected:
    struct Coefficients {
        /* Transfer function coefficients "b" (numerator) */
        f32 mB0{1.0f}, mB1{0.0f}, mB2{0.0f};
        /* Transfer function coefficients "a" (denominator; a0 is pre-applied). */
        f32 mA1{0.0f}, mA2{0.0f};
    };
    /* Last two delayed components for direct form II. */
    f32 mZ1{0.0f}, mZ2{0.0f};
    Coefficients mCoeffs;

    static auto SetParams(BiquadType type, f32 f0norm, f32 gain, f32 rcpQ, Coefficients &coeffs)
        -> bool;

    /**
     * Calculates the rcpQ (i.e. 1/Q) coefficient for shelving filters, using
     * the reference gain and shelf slope parameter.
     * \param gain 0 < gain
     * \param slope 0 < slope <= 1
     */
    static auto rcpQFromSlope(f32 const gain, f32 const slope) -> f32
    { return std::sqrt((gain + 1.0f/gain)*(1.0f/slope - 1.0f) + 2.0f); }

    /**
     * Calculates the rcpQ (i.e. 1/Q) coefficient for filters, using the
     * normalized reference frequency and bandwidth.
     * \param f0norm 0 < f0norm < 0.5.
     * \param bandwidth 0 < bandwidth
     */
    static auto rcpQFromBandwidth(f32 const f0norm, f32 const bandwidth) -> f32
    {
        const auto w0 = std::numbers::pi_v<f32>*2.0f * f0norm;
        return 2.0f*std::sinh(std::log(2.0f)/2.0f*bandwidth*w0/std::sin(w0));
    }

public:
    void clear() noexcept { mZ1 = mZ2 = 0.0f; }

    /**
     * Sets the filter state for the specified filter type and its parameters.
     *
     * \param type The type of filter to apply.
     * \param f0norm The normalized reference frequency (ref / sample_rate).
     * This is the center point for the Shelf, Peaking, and BandPass filter
     * types, or the cutoff frequency for the LowPass and HighPass filter
     * types.
     * \param gain The gain for the reference frequency response. Only used by
     * the Shelf and Peaking filter types.
     * \param slope Slope steepness of the transition band.
     */
    void setParamsFromSlope(BiquadType const type, f32 const f0norm, f32 gain, f32 const slope)
    {
        gain = std::max(gain, 0.001f); /* Limit -60dB */
        SetParams(type, f0norm, gain, rcpQFromSlope(gain, slope), mCoeffs);
    }

    /**
     * Sets the filter state for the specified filter type and its parameters.
     *
     * \param type The type of filter to apply.
     * \param f0norm The normalized reference frequency (ref / sample_rate).
     * This is the center point for the Shelf, Peaking, and BandPass filter
     * types, or the cutoff frequency for the LowPass and HighPass filter
     * types.
     * \param gain The gain for the reference frequency response. Only used by
     * the Shelf and Peaking filter types.
     * \param bandwidth Normalized bandwidth of the transition band.
     */
    void setParamsFromBandwidth(BiquadType const type, f32 const f0norm, f32 const gain,
        f32 const bandwidth)
    { SetParams(type, f0norm, gain, rcpQFromBandwidth(f0norm, bandwidth), mCoeffs); }

    void copyParamsFrom(BiquadFilter const &other) noexcept
    { mCoeffs = other.mCoeffs; }

    void process(std::span<f32 const> src, std::span<f32> dst);
    /** Processes this filter and the other at the same time. */
    void dualProcess(BiquadFilter &other, std::span<f32 const> src, std::span<f32> dst);

    /* Rather hacky. It's just here to support "manual" processing. */
    [[nodiscard]] auto getComponents() const noexcept -> std::array<f32, 2> { return {mZ1,mZ2}; }
    void setComponents(f32 const z1, f32 const z2) noexcept { mZ1 = z1; mZ2 = z2; }
    [[nodiscard]] auto processOne(f32 const in, f32 &z1, f32 &z2) const noexcept -> f32
    {
        const auto out = in*mCoeffs.mB0 + z1;
        z1 = in*mCoeffs.mB1 - out*mCoeffs.mA1 + z2;
        z2 = in*mCoeffs.mB2 - out*mCoeffs.mA2;
        return out;
    }
};

class BiquadInterpFilter : protected BiquadFilter {
    Coefficients mTargetCoeffs;
    int mCounter{-1};

    void setParams(BiquadType type, f32 f0norm, f32 gain, f32 rcpQ);

public:
    void reset() noexcept
    {
        BiquadFilter::clear();
        mTargetCoeffs = Coefficients{};
        mCoeffs = mTargetCoeffs;
        mCounter = -1;
    }

    void clear() noexcept
    {
        BiquadFilter::clear();
        mCoeffs = mTargetCoeffs;
        mCounter = 0;
    }

    /**
     * Sets the filter state for the specified filter type and its parameters.
     *
     * \param type The type of filter to apply.
     * \param f0norm The normalized reference frequency (ref / sample_rate).
     * This is the center point for the Shelf, Peaking, and BandPass filter
     * types, or the cutoff frequency for the LowPass and HighPass filter
     * types.
     * \param gain The gain for the reference frequency response. Only used by
     * the Shelf and Peaking filter types.
     * \param slope Slope steepness of the transition band.
     */
    void setParamsFromSlope(BiquadType const type, f32 const f0norm, f32 gain, f32 const slope)
    {
        gain = std::max(gain, 0.001f); /* Limit -60dB */
        setParams(type, f0norm, gain, rcpQFromSlope(gain, slope));
    }

    /**
     * Sets the filter state for the specified filter type and its parameters.
     *
     * \param type The type of filter to apply.
     * \param f0norm The normalized reference frequency (ref / sample_rate).
     * This is the center point for the Shelf, Peaking, and BandPass filter
     * types, or the cutoff frequency for the LowPass and HighPass filter
     * types.
     * \param gain The gain for the reference frequency response. Only used by
     * the Shelf and Peaking filter types.
     * \param bandwidth Normalized bandwidth of the transition band.
     */
    void setParamsFromBandwidth(BiquadType const type, f32 const f0norm, f32 const gain,
        f32 const bandwidth)
    { setParams(type, f0norm, gain, rcpQFromBandwidth(f0norm, bandwidth)); }

    void copyParamsFrom(const BiquadInterpFilter &other) noexcept;

    void process(std::span<f32 const> src, std::span<f32> dst);
    /** Processes this filter and the other at the same time. */
    void dualProcess(BiquadInterpFilter &other, std::span<f32 const> src, std::span<f32> dst);
};


struct DualBiquad {
    BiquadFilter &f0, &f1;

    void process(std::span<f32 const> const src, std::span<f32> const dst) const
    { f0.dualProcess(f1, src, dst); }
};

struct DualBiquadInterp {
    BiquadInterpFilter &f0, &f1;

    void process(std::span<f32 const> const src, std::span<f32> const dst) const
    { f0.dualProcess(f1, src, dst); }
};

#endif /* CORE_FILTERS_BIQUAD_H */
