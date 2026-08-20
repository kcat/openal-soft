#ifndef CORE_BFORMATDEC_H
#define CORE_BFORMATDEC_H

#include <array>
#include <cstddef>
#include <span>
#include <variant>
#include <vector>

#include "ambidefs.h"
#include "bufferline.h"
#include "devformat.h"
#include "filters/splitter.h"
#include "opthelpers.h"


using ChannelDec = std::array<float,MaxAmbiChannels>;

class BFormatDec {
    static constexpr size_t sHFBand{0};
    static constexpr size_t sLFBand{1};
    static constexpr size_t sNumBands{2};

    struct ChannelDecoderSingle {
        std::array<float,MaxOutputChannels> mGains{};
    };
    using SBandDecoderVector = std::vector<ChannelDecoderSingle>;

    struct ChannelDecoderDual {
        BandSplitter mXOver;
        std::array<std::array<float,MaxOutputChannels>,sNumBands> mGains{};
    };
    using DBandDecoderVector = std::vector<ChannelDecoderDual>;

    alignas(16) std::array<FloatBufferLine,2> mSamples{};

    std::variant<SBandDecoderVector,DBandDecoderVector> mChannelDec;

public:
    BFormatDec(size_t inchans, std::span<const ChannelDec> coeffs,
        std::span<const ChannelDec> coeffslf, float xover_f0norm);

    /* Decodes the ambisonic input to the given output channels. */
    void process(std::span<FloatBufferLine> OutBuffer,
        std::span<const FloatBufferLine> InSamples, size_t SamplesToDo) noexcept NONBLOCKING;
};

#endif /* CORE_BFORMATDEC_H */
