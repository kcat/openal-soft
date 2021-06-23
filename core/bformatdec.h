#ifndef CORE_BFORMATDEC_H
#define CORE_BFORMATDEC_H

#include <array>
#include <cstddef>
#include <memory>

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

    struct ChannelDecoder {
        union MatrixU {
            float Dual[sNumBands][MAX_OUTPUT_CHANNELS];
            float Single[MAX_OUTPUT_CHANNELS];
        } mGains{};

        /* NOTE: BandSplitter filter is unused with single-band decoding. */
        BandSplitter mXOver;
    };

    alignas(16) std::array<FloatBufferLine,2> mSamples;

    const std::unique_ptr<FrontStablizer> mStablizer;
    const bool mDualBand{false};

    al::FlexArray<ChannelDecoder> mChannelDec;

    BFormatDec(const size_t inchans, const al::span<const ChannelDec> coeffs,
        const al::span<const ChannelDec> coeffslf, const float xover_f0norm,
        std::unique_ptr<FrontStablizer> stablizer);

public:
    bool hasStablizer() const noexcept { return mStablizer != nullptr; };

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

    DEF_FAM_NEWDEL(BFormatDec, mChannelDec)
};

#endif /* CORE_BFORMATDEC_H */
