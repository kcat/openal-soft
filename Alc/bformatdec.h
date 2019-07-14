#ifndef BFORMATDEC_H
#define BFORMATDEC_H

#include "alMain.h"
#include "filters/splitter.h"
#include "ambidefs.h"
#include "almalloc.h"


struct AmbDecConf;


using ChannelDec = ALfloat[MAX_AMBI_CHANNELS];

class BFormatDec {
    static constexpr size_t sHFBand{0};
    static constexpr size_t sLFBand{1};
    static constexpr size_t sNumBands{2};

    ALuint mEnabled{0u}; /* Bitfield of enabled channels. */

    union MatrixU {
        ALfloat Dual[MAX_OUTPUT_CHANNELS][sNumBands][MAX_AMBI_CHANNELS];
        ALfloat Single[MAX_OUTPUT_CHANNELS][MAX_AMBI_CHANNELS];
    } mMatrix{};

    /* NOTE: BandSplitter filters are unused with single-band decoding */
    BandSplitter mXOver[MAX_AMBI_CHANNELS];

    al::vector<std::array<ALfloat,BUFFERSIZE>, 16> mSamples;
    /* These two alias into Samples */
    std::array<ALfloat,BUFFERSIZE> *mSamplesHF{nullptr};
    std::array<ALfloat,BUFFERSIZE> *mSamplesLF{nullptr};

    ALuint mNumChannels{0u};
    bool mDualBand{false};

public:
    BFormatDec(const AmbDecConf *conf, const bool allow_2band, const ALuint inchans,
        const ALuint srate, const ALsizei (&chanmap)[MAX_OUTPUT_CHANNELS]);
    BFormatDec(const ALuint inchans, const ALsizei chancount,
        const ChannelDec (&chancoeffs)[MAX_OUTPUT_CHANNELS],
        const ALsizei (&chanmap)[MAX_OUTPUT_CHANNELS]);

    /* Decodes the ambisonic input to the given output channels. */
    void process(const al::span<FloatBufferLine> OutBuffer, const FloatBufferLine *InSamples,
        const ALsizei SamplesToDo);

    /* Retrieves per-order HF scaling factors for "upsampling" ambisonic data. */
    static std::array<ALfloat,MAX_AMBI_ORDER+1> GetHFOrderScales(const ALsizei in_order,
        const ALsizei out_order) noexcept;

    DEF_NEWDEL(BFormatDec)
};

#endif /* BFORMATDEC_H */
