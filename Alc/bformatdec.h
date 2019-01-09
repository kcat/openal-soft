#ifndef BFORMATDEC_H
#define BFORMATDEC_H

#include "alMain.h"
#include "filters/splitter.h"
#include "ambidefs.h"
#include "almalloc.h"


struct AmbDecConf;


using ChannelDec = ALfloat[MAX_AMBI_COEFFS];

class BFormatDec {
    static constexpr size_t sHFBand{0};
    static constexpr size_t sLFBand{1};
    static constexpr size_t sNumBands{2};

    ALuint mEnabled; /* Bitfield of enabled channels. */

    union MatrixU {
        ALfloat Dual[MAX_OUTPUT_CHANNELS][sNumBands][MAX_AMBI_COEFFS];
        ALfloat Single[MAX_OUTPUT_CHANNELS][MAX_AMBI_COEFFS];
    } mMatrix;

    /* NOTE: BandSplitter filters are unused with single-band decoding */
    BandSplitter mXOver[MAX_AMBI_COEFFS];

    al::vector<std::array<ALfloat,BUFFERSIZE>, 16> mSamples;
    /* These two alias into Samples */
    std::array<ALfloat,BUFFERSIZE> *mSamplesHF;
    std::array<ALfloat,BUFFERSIZE> *mSamplesLF;

    SplitterAllpass mUpAllpass[MAX_OUTPUT_CHANNELS];
    struct {
        BandSplitter Splitter;
        ALfloat Gains[sNumBands];
    } mUpsampler[4];

    ALsizei mNumChannels;
    ALboolean mDualBand;

public:
    void reset(const AmbDecConf *conf, bool allow_2band, ALsizei inchans, ALuint srate, const ALsizei (&chanmap)[MAX_OUTPUT_CHANNELS]);

    void reset(const ALsizei inchans, const ALfloat xover_norm, const ALsizei chancount, const ChannelDec (&chancoeffs)[MAX_OUTPUT_CHANNELS], const ALsizei (&chanmap)[MAX_OUTPUT_CHANNELS]);

    /* Decodes the ambisonic input to the given output channels. */
    void process(ALfloat (*OutBuffer)[BUFFERSIZE], const ALsizei OutChannels, const ALfloat (*InSamples)[BUFFERSIZE], const ALsizei SamplesToDo);

    /* Up-samples a first-order input to the decoder's configuration. */
    void upSample(ALfloat (*OutBuffer)[BUFFERSIZE], const ALsizei OutChannels, const ALfloat (*InSamples)[BUFFERSIZE], const ALsizei InChannels, const ALsizei SamplesToDo);

    DEF_NEWDEL(BFormatDec)
};


/* Stand-alone first-order upsampler. Kept here because it shares some stuff
 * with bformatdec.
 */
class AmbiUpsampler {
    static constexpr size_t sHFBand{0};
    static constexpr size_t sLFBand{1};
    static constexpr size_t sNumBands{2};

    SplitterAllpass mAllpass[MAX_OUTPUT_CHANNELS];
    alignas(16) ALfloat mSamples[sNumBands][BUFFERSIZE];
    struct {
        BandSplitter Splitter;
        ALfloat Gains[sNumBands];
    } mInput[4];

public:
    void reset(const ALsizei out_order, const ALfloat xover_norm);
    void process(ALfloat (*OutBuffer)[BUFFERSIZE], const ALsizei OutChannels, const ALfloat (*InSamples)[BUFFERSIZE], const ALsizei InChannels, const ALsizei SamplesToDo);

    static std::array<ALfloat,MAX_AMBI_ORDER+1> GetHFOrderScales(const ALsizei in_order, const ALsizei out_order) noexcept;

    DEF_NEWDEL(AmbiUpsampler)
};

#endif /* BFORMATDEC_H */
