#ifndef BFORMATDEC_H
#define BFORMATDEC_H

#include "alMain.h"
#include "filters/splitter.h"
#include "almalloc.h"


struct AmbDecConf;


/* These are the necessary scales for first-order HF responses to play over
 * higher-order 2D (non-periphonic) decoders.
 */
#define W_SCALE_2H0P   1.224744871f /* sqrt(1.5) */
#define XYZ_SCALE_2H0P 1.0f
#define W_SCALE_3H0P   1.414213562f /* sqrt(2) */
#define XYZ_SCALE_3H0P 1.082392196f

/* These are the necessary scales for first-order HF responses to play over
 * higher-order 3D (periphonic) decoders.
 */
#define W_SCALE_2H2P   1.341640787f /* sqrt(1.8) */
#define XYZ_SCALE_2H2P 1.0f
#define W_SCALE_3H3P   1.695486018f
#define XYZ_SCALE_3H3P 1.136697713f


/* NOTE: These are scale factors as applied to Ambisonics content. Decoder
 * coefficients should be divided by these values to get proper N3D scalings.
 */
struct AmbiScale {
    static constexpr float N3D2N3D[MAX_AMBI_COEFFS]{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
    };
    static constexpr float SN3D2N3D[MAX_AMBI_COEFFS]{
        1.000000000f, /* ACN  0 (W), sqrt(1) */
        1.732050808f, /* ACN  1 (Y), sqrt(3) */
        1.732050808f, /* ACN  2 (Z), sqrt(3) */
        1.732050808f, /* ACN  3 (X), sqrt(3) */
        2.236067978f, /* ACN  4 (V), sqrt(5) */
        2.236067978f, /* ACN  5 (T), sqrt(5) */
        2.236067978f, /* ACN  6 (R), sqrt(5) */
        2.236067978f, /* ACN  7 (S), sqrt(5) */
        2.236067978f, /* ACN  8 (U), sqrt(5) */
        2.645751311f, /* ACN  9 (Q), sqrt(7) */
        2.645751311f, /* ACN 10 (O), sqrt(7) */
        2.645751311f, /* ACN 11 (M), sqrt(7) */
        2.645751311f, /* ACN 12 (K), sqrt(7) */
        2.645751311f, /* ACN 13 (L), sqrt(7) */
        2.645751311f, /* ACN 14 (N), sqrt(7) */
        2.645751311f, /* ACN 15 (P), sqrt(7) */
    };
    static constexpr float FuMa2N3D[MAX_AMBI_COEFFS]{
        1.414213562f, /* ACN  0 (W), sqrt(2) */
        1.732050808f, /* ACN  1 (Y), sqrt(3) */
        1.732050808f, /* ACN  2 (Z), sqrt(3) */
        1.732050808f, /* ACN  3 (X), sqrt(3) */
        1.936491673f, /* ACN  4 (V), sqrt(15)/2 */
        1.936491673f, /* ACN  5 (T), sqrt(15)/2 */
        2.236067978f, /* ACN  6 (R), sqrt(5) */
        1.936491673f, /* ACN  7 (S), sqrt(15)/2 */
        1.936491673f, /* ACN  8 (U), sqrt(15)/2 */
        2.091650066f, /* ACN  9 (Q), sqrt(35/8) */
        1.972026594f, /* ACN 10 (O), sqrt(35)/3 */
        2.231093404f, /* ACN 11 (M), sqrt(224/45) */
        2.645751311f, /* ACN 12 (K), sqrt(7) */
        2.231093404f, /* ACN 13 (L), sqrt(224/45) */
        1.972026594f, /* ACN 14 (N), sqrt(35)/3 */
        2.091650066f, /* ACN 15 (P), sqrt(35/8) */
    };
};


class BFormatDec {
public:
    static constexpr size_t sNumBands{2};

private:
    ALuint mEnabled; /* Bitfield of enabled channels. */

    union MatrixU {
        alignas(16) ALfloat Dual[MAX_OUTPUT_CHANNELS][sNumBands][MAX_AMBI_COEFFS];
        alignas(16) ALfloat Single[MAX_OUTPUT_CHANNELS][MAX_AMBI_COEFFS];
    } mMatrix;

    /* NOTE: BandSplitter filters are unused with single-band decoding */
    BandSplitter mXOver[MAX_AMBI_COEFFS];

    al::vector<std::array<ALfloat,BUFFERSIZE>, 16> mSamples;
    /* These two alias into Samples */
    std::array<ALfloat,BUFFERSIZE> *mSamplesHF;
    std::array<ALfloat,BUFFERSIZE> *mSamplesLF;

    alignas(16) ALfloat mChannelMix[BUFFERSIZE];

    struct {
        BandSplitter XOver;
        ALfloat Gains[sNumBands];
    } mUpSampler[4];

    ALsizei mNumChannels;
    ALboolean mDualBand;

public:
    void reset(const AmbDecConf *conf, ALsizei chancount, ALuint srate, const ALsizei (&chanmap)[MAX_OUTPUT_CHANNELS]);

    /* Decodes the ambisonic input to the given output channels. */
    void process(ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], const ALsizei OutChannels, const ALfloat (*RESTRICT InSamples)[BUFFERSIZE], const ALsizei SamplesToDo);

    /* Up-samples a first-order input to the decoder's configuration. */
    void upSample(ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], const ALfloat (*RESTRICT InSamples)[BUFFERSIZE], const ALsizei InChannels, const ALsizei SamplesToDo);

    DEF_NEWDEL(BFormatDec)
};


/* Stand-alone first-order upsampler. Kept here because it shares some stuff
 * with bformatdec. Assumes a periphonic (4-channel) input mix!
 */
class AmbiUpsampler {
public:
    static constexpr size_t sNumBands{2};

private:
    alignas(16) ALfloat mSamples[sNumBands][BUFFERSIZE];

    BandSplitter mXOver[4];

    std::array<std::array<std::array<ALfloat,sNumBands>,MAX_OUTPUT_CHANNELS>,4> mGains;

public:
    void reset(const ALCdevice *device, const ALfloat w_scale, const ALfloat xyz_scale);
    void process(ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], const ALsizei OutChannels, const ALfloat (*RESTRICT InSamples)[BUFFERSIZE], const ALsizei SamplesToDo);

    DEF_NEWDEL(AmbiUpsampler)
};

#endif /* BFORMATDEC_H */
