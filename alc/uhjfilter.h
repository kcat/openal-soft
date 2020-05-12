#ifndef UHJFILTER_H
#define UHJFILTER_H

#include <array>

#include "alcmain.h"
#include "almalloc.h"


/* Encoding 2-channel UHJ from B-Format is done as:
 *
 * S = 0.9396926*W + 0.1855740*X
 * D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y
 *
 * Left = (S + D)/2.0
 * Right = (S - D)/2.0
 *
 * where j is a wide-band +90 degree phase shift.
 *
 * The phase shift is done using a FIR filter derived from an FFT'd impulse
 * with the desired shift.
 */

struct Uhj2Encoder {
    /* A particular property of the filter allows it to cover nearly twice its
     * length, so the filter size is also the effective delay (despite being
     * center-aligned).
     */
    constexpr static size_t sFilterSize{128};

    /* Delays for the unfiltered signal. */
    alignas(16) std::array<float,sFilterSize> mMidDelay{};
    alignas(16) std::array<float,sFilterSize> mSideDelay{};

    /* History for the FIR filter. */
    alignas(16) std::array<float,sFilterSize*2 - 1> mSideHistory{};

    alignas(16) std::array<float,BUFFERSIZE + sFilterSize*2> mTemp{};

    alignas(16) std::array<float,BUFFERSIZE> mMid{};
    alignas(16) std::array<float,BUFFERSIZE> mSide{};

    /**
     * Encodes a 2-channel UHJ (stereo-compatible) signal from a B-Format input
     * signal. The input must use FuMa channel ordering and scaling.
     */
    void encode(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
        const FloatBufferLine *InSamples, const size_t SamplesToDo);

    DEF_NEWDEL(Uhj2Encoder)
};

#endif /* UHJFILTER_H */
