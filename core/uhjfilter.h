#ifndef CORE_UHJFILTER_H
#define CORE_UHJFILTER_H

#include <array>

#include "almalloc.h"
#include "bufferline.h"
#include "resampler_limits.h"


struct UhjEncoder {
    /* The filter delay is half it's effective size, so a delay of 128 has a
     * FIR length of 256.
     */
    constexpr static size_t sFilterDelay{128};

    /* Delays and processing storage for the unfiltered signal. */
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mS{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mD{};

    /* History for the FIR filter. */
    alignas(16) std::array<float,sFilterDelay*2 - 1> mWXHistory{};

    alignas(16) std::array<float,BufferLineSize + sFilterDelay*2> mTemp{};

    /**
     * Encodes a 2-channel UHJ (stereo-compatible) signal from a B-Format input
     * signal. The input must use FuMa channel ordering and UHJ scaling (FuMa
     * with an additional +3dB boost).
     */
    void encode(const FloatBufferSpan LeftOut, const FloatBufferSpan RightOut,
        const FloatBufferLine *InSamples, const size_t SamplesToDo);

    DEF_NEWDEL(UhjEncoder)
};


struct UhjDecoder {
    constexpr static size_t sFilterDelay{128};

    constexpr static size_t sLineSize{BufferLineSize+MaxResamplerPadding+sFilterDelay};
    using BufferLine = std::array<float,sLineSize>;

    alignas(16) std::array<float,BufferLineSize+MaxResamplerEdge+sFilterDelay> mS{};
    alignas(16) std::array<float,BufferLineSize+MaxResamplerEdge+sFilterDelay> mD{};
    alignas(16) std::array<float,BufferLineSize+MaxResamplerEdge+sFilterDelay> mT{};

    alignas(16) std::array<float,sFilterDelay-1> mDTHistory{};
    alignas(16) std::array<float,sFilterDelay-1> mSHistory{};

    alignas(16) std::array<float,BufferLineSize+MaxResamplerEdge + sFilterDelay*2> mTemp{};

    /**
     * Decodes a 3- or 4-channel UHJ signal into a B-Format signal with FuMa
     * channel ordering and UHJ scaling. For 3-channel, the 3rd channel may be
     * attenuated by 'n', where 0 <= n <= 1. So 2-channel UHJ can be decoded by
     * leaving the 3rd channel input silent (n=0).
     */
    void decode(const al::span<BufferLine> samples, const size_t offset, const size_t samplesToDo,
        const size_t forwardSamples);

    DEF_NEWDEL(UhjDecoder)
};

#endif /* CORE_UHJFILTER_H */
