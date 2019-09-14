#ifndef BFORMATDEC_H
#define BFORMATDEC_H

#include <array>
#include <cstddef>

#include "AL/al.h"

#include "alcmain.h"
#include "almalloc.h"
#include "alspan.h"
#include "ambidefs.h"
#include "devformat.h"
#include "filters/splitter.h"
#include "vector.h"

struct AmbDecConf;


using ChannelDec = ALfloat[MAX_AMBI_CHANNELS];

class BFormatDec {
    static constexpr size_t sHFBand{0};
    static constexpr size_t sLFBand{1};
    static constexpr size_t sNumBands{2};

    bool mDualBand{false};
    ALuint mEnabled{0u}; /* Bitfield of enabled channels. */

    ALuint mNumChannels{0u};
    union MatrixU {
        ALfloat Dual[MAX_OUTPUT_CHANNELS][sNumBands][MAX_AMBI_CHANNELS];
        ALfloat Single[MAX_OUTPUT_CHANNELS][MAX_AMBI_CHANNELS];
    } mMatrix{};

    /* NOTE: BandSplitter filters are unused with single-band decoding */
    BandSplitter mXOver[MAX_AMBI_CHANNELS];

    al::vector<FloatBufferLine, 16> mSamples;
    /* These two alias into Samples */
    FloatBufferLine *mSamplesHF{nullptr};
    FloatBufferLine *mSamplesLF{nullptr};

public:
    BFormatDec(const AmbDecConf *conf, const bool allow_2band, const ALuint inchans,
        const ALuint srate, const ALuint (&chanmap)[MAX_OUTPUT_CHANNELS]);
    BFormatDec(const ALuint inchans, const ALsizei chancount,
        const ChannelDec (&chancoeffs)[MAX_OUTPUT_CHANNELS],
        const ALuint (&chanmap)[MAX_OUTPUT_CHANNELS]);

    /* Decodes the ambisonic input to the given output channels. */
    void process(const al::span<FloatBufferLine> OutBuffer, const FloatBufferLine *InSamples,
        const size_t SamplesToDo);

    /* Retrieves per-order HF scaling factors for "upsampling" ambisonic data. */
    static std::array<ALfloat,MAX_AMBI_ORDER+1> GetHFOrderScales(const ALuint in_order,
        const ALuint out_order) noexcept;

    DEF_NEWDEL(BFormatDec)
};

#endif /* BFORMATDEC_H */
