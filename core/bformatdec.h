#ifndef CORE_BFORMATDEC_H
#define CORE_BFORMATDEC_H

#include <array>
#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

#include "almalloc.h"
#include "alspan.h"
#include "ambidefs.h"
#include "bufferline.h"
#include "devformat.h"
#include "filters/splitter.h"

struct FrontStablizer;


using ChannelDec = std::array<float,MaxAmbiChannels>;

class BFormatDec {
    static constexpr size_t sHFBand{0};
    static constexpr size_t sLFBand{1};
    static constexpr size_t sNumBands{2};

    struct ChannelDecoderSingle {
        float mGains[MAX_OUTPUT_CHANNELS];
    };

    struct ChannelDecoderDual {
        BandSplitter mXOver;
        float mGains[sNumBands][MAX_OUTPUT_CHANNELS];
    };

    alignas(16) std::array<FloatBufferLine,2> mSamples;

    const std::unique_ptr<FrontStablizer> mStablizer;

    std::variant<std::vector<ChannelDecoderSingle>,std::vector<ChannelDecoderDual>> mChannelDec;

public:
    BFormatDec(const size_t inchans, const al::span<const ChannelDec> coeffs,
        const al::span<const ChannelDec> coeffslf, const float xover_f0norm,
        std::unique_ptr<FrontStablizer> stablizer);

    bool hasStablizer() const noexcept { return mStablizer != nullptr; }

    /* Decodes the ambisonic input to the given output channels. */
    void process(const al::span<FloatBufferLine> OutBuffer, const FloatBufferLine *InSamples,
        const size_t SamplesToDo);

    /* Decodes the ambisonic input to the given output channels with stablization. */
    void processStablize(const al::span<FloatBufferLine> OutBuffer,
        const FloatBufferLine *InSamples, const size_t lidx, const size_t ridx, const size_t cidx,
        const size_t SamplesToDo);

    static std::unique_ptr<BFormatDec> Create(const size_t inchans,
        const al::span<const ChannelDec> coeffs, const al::span<const ChannelDec> coeffslf,
        const float xover_f0norm, std::unique_ptr<FrontStablizer> stablizer);

    DEF_NEWDEL(BFormatDec)
};

#endif /* CORE_BFORMATDEC_H */
