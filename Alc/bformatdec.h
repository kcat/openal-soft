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
extern const ALfloat N3D2N3DScale[MAX_AMBI_COEFFS];
extern const ALfloat SN3D2N3DScale[MAX_AMBI_COEFFS];
extern const ALfloat FuMa2N3DScale[MAX_AMBI_COEFFS];


struct BFormatDec {
    static constexpr size_t NumBands{2};

    ALuint Enabled; /* Bitfield of enabled channels. */

    union {
        alignas(16) ALfloat Dual[MAX_OUTPUT_CHANNELS][NumBands][MAX_AMBI_COEFFS];
        alignas(16) ALfloat Single[MAX_OUTPUT_CHANNELS][MAX_AMBI_COEFFS];
    } Matrix;

    /* NOTE: BandSplitter filters are unused with single-band decoding */
    BandSplitter XOver[MAX_AMBI_COEFFS];

    al::vector<std::array<ALfloat,BUFFERSIZE>, 16> Samples;
    /* These two alias into Samples */
    std::array<ALfloat,BUFFERSIZE> *SamplesHF;
    std::array<ALfloat,BUFFERSIZE> *SamplesLF;

    alignas(16) ALfloat ChannelMix[BUFFERSIZE];

    struct {
        BandSplitter XOver;
        ALfloat Gains[NumBands];
    } UpSampler[4];

    ALsizei NumChannels;
    ALboolean DualBand;

    DEF_NEWDEL(BFormatDec)
};

void bformatdec_reset(BFormatDec *dec, const AmbDecConf *conf, ALsizei chancount, ALuint srate, const ALsizei chanmap[MAX_OUTPUT_CHANNELS]);

/* Decodes the ambisonic input to the given output channels. */
void bformatdec_process(BFormatDec *dec, ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], ALsizei OutChannels, const ALfloat (*RESTRICT InSamples)[BUFFERSIZE], ALsizei SamplesToDo);

/* Up-samples a first-order input to the decoder's configuration. */
void bformatdec_upSample(BFormatDec *dec, ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], const ALfloat (*RESTRICT InSamples)[BUFFERSIZE], ALsizei InChannels, ALsizei SamplesToDo);


/* Stand-alone first-order upsampler. Kept here because it shares some stuff
 * with bformatdec. Assumes a periphonic (4-channel) input mix!
 */
struct AmbiUpsampler {
    static constexpr size_t NumBands{2};

    alignas(16) ALfloat Samples[NumBands][BUFFERSIZE];

    BandSplitter XOver[4];

    ALfloat Gains[4][MAX_OUTPUT_CHANNELS][NumBands];

    DEF_NEWDEL(AmbiUpsampler)
};

void ambiup_reset(AmbiUpsampler *ambiup, const ALCdevice *device, ALfloat w_scale, ALfloat xyz_scale);
void ambiup_process(AmbiUpsampler *ambiup, ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], ALsizei OutChannels, const ALfloat (*RESTRICT InSamples)[BUFFERSIZE], ALsizei SamplesToDo);

#endif /* BFORMATDEC_H */
