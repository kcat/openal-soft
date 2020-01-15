#ifndef BFORMATDEC_H
#define BFORMATDEC_H

#include <array>
#include <cstddef>
#include <memory>

#include "AL/al.h"

#include "alcmain.h"
#include "almalloc.h"
#include "alspan.h"
#include "ambidefs.h"
#include "devformat.h"
#include "filters/splitter.h"

struct AmbDecConf;


using ChannelDec = float[MAX_AMBI_CHANNELS];

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

    bool mDualBand{false};

    al::FlexArray<ChannelDecoder> mChannelDec;

public:
    BFormatDec(const AmbDecConf *conf, const bool allow_2band, const ALuint inchans,
        const ALuint srate, const ALuint (&chanmap)[MAX_OUTPUT_CHANNELS]);
    BFormatDec(const ALuint inchans, const al::span<const ChannelDec> chancoeffs);

    /* Decodes the ambisonic input to the given output channels. */
    void process(const al::span<FloatBufferLine> OutBuffer, const FloatBufferLine *InSamples,
        const size_t SamplesToDo);

    /* Retrieves per-order HF scaling factors for "upsampling" ambisonic data. */
    static std::array<float,MAX_AMBI_ORDER+1> GetHFOrderScales(const ALuint in_order,
        const ALuint out_order) noexcept;

    static std::unique_ptr<BFormatDec> Create(const AmbDecConf *conf, const bool allow_2band,
        const ALuint inchans, const ALuint srate, const ALuint (&chanmap)[MAX_OUTPUT_CHANNELS])
    {
        return std::unique_ptr<BFormatDec>{new(FamCount{inchans})
            BFormatDec{conf, allow_2band, inchans, srate, chanmap}};
    }
    static std::unique_ptr<BFormatDec> Create(const ALuint inchans,
        const al::span<const ChannelDec> chancoeffs)
    {
        return std::unique_ptr<BFormatDec>{new(FamCount{inchans}) BFormatDec{inchans, chancoeffs}};
    }

    DEF_FAM_NEWDEL(BFormatDec, mChannelDec)
};

#endif /* BFORMATDEC_H */
