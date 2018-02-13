#ifndef BFORMATDEC_H
#define BFORMATDEC_H

#include "alMain.h"


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
const ALfloat N3D2N3DScale[MAX_AMBI_COEFFS];
const ALfloat SN3D2N3DScale[MAX_AMBI_COEFFS];
const ALfloat FuMa2N3DScale[MAX_AMBI_COEFFS];


struct AmbDecConf;
struct BFormatDec;
struct AmbiUpsampler;


struct BFormatDec *bformatdec_alloc();
void bformatdec_free(struct BFormatDec **dec);
void bformatdec_reset(struct BFormatDec *dec, const struct AmbDecConf *conf, ALsizei chancount, ALuint srate, const ALsizei chanmap[MAX_OUTPUT_CHANNELS]);

/* Decodes the ambisonic input to the given output channels. */
void bformatdec_process(struct BFormatDec *dec, ALfloat (*restrict OutBuffer)[BUFFERSIZE], ALsizei OutChannels, const ALfloat (*restrict InSamples)[BUFFERSIZE], ALsizei SamplesToDo);

/* Up-samples a first-order input to the decoder's configuration. */
void bformatdec_upSample(struct BFormatDec *dec, ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat (*restrict InSamples)[BUFFERSIZE], ALsizei InChannels, ALsizei SamplesToDo);


/* Stand-alone first-order upsampler. Kept here because it shares some stuff
 * with bformatdec. Assumes a periphonic (4-channel) input mix!
 */
struct AmbiUpsampler *ambiup_alloc();
void ambiup_free(struct AmbiUpsampler **ambiup);
void ambiup_reset(struct AmbiUpsampler *ambiup, const ALCdevice *device, ALfloat w_scale, ALfloat xyz_scale);

void ambiup_process(struct AmbiUpsampler *ambiup, ALfloat (*restrict OutBuffer)[BUFFERSIZE], ALsizei OutChannels, const ALfloat (*restrict InSamples)[BUFFERSIZE], ALsizei SamplesToDo);


/* Band splitter. Splits a signal into two phase-matching frequency bands. */
typedef struct BandSplitter {
    ALfloat coeff;
    ALfloat lp_z1;
    ALfloat lp_z2;
    ALfloat hp_z1;
} BandSplitter;

void bandsplit_init(BandSplitter *splitter, ALfloat f0norm);
void bandsplit_clear(BandSplitter *splitter);
void bandsplit_process(BandSplitter *splitter, ALfloat *restrict hpout, ALfloat *restrict lpout,
                       const ALfloat *input, ALsizei count);

/* The all-pass portion of the band splitter. Applies the same phase shift
 * without splitting the signal.
 */
typedef struct SplitterAllpass {
    ALfloat coeff;
    ALfloat z1;
} SplitterAllpass;

void splitterap_init(SplitterAllpass *splitter, ALfloat f0norm);
void splitterap_clear(SplitterAllpass *splitter);
void splitterap_process(SplitterAllpass *splitter, ALfloat *restrict samples, ALsizei count);


typedef struct FrontStablizer {
    SplitterAllpass APFilter[MAX_OUTPUT_CHANNELS];
    BandSplitter LFilter, RFilter;
    alignas(16) ALfloat LSplit[2][BUFFERSIZE];
    alignas(16) ALfloat RSplit[2][BUFFERSIZE];
} FrontStablizer;

#endif /* BFORMATDEC_H */
