#ifndef BFORMATDEC_H
#define BFORMATDEC_H

#include "alMain.h"

struct AmbDecConf;
struct BFormatDec;

enum BFormatDecFlags {
    BFDF_DistanceComp = 1<<0
};

struct BFormatDec *bformatdec_alloc();
void bformatdec_free(struct BFormatDec *dec);
int bformatdec_getOrder(const struct BFormatDec *dec);
void bformatdec_reset(struct BFormatDec *dec, const struct AmbDecConf *conf, ALuint chancount, ALuint srate, const ALuint chanmap[MAX_OUTPUT_CHANNELS], int flags);

/* Decodes the ambisonic input to the given output channels. */
void bformatdec_process(struct BFormatDec *dec, ALfloat (*restrict OutBuffer)[BUFFERSIZE], ALuint OutChannels, ALfloat (*restrict InSamples)[BUFFERSIZE], ALuint SamplesToDo);

/* Up-samples a first-order input to the decoder's configuration. */
void bformatdec_upSample(struct BFormatDec *dec, ALfloat (*restrict OutBuffer)[BUFFERSIZE], ALfloat (*restrict InSamples)[BUFFERSIZE], ALuint InChannels, ALuint SamplesToDo);

#endif /* BFORMATDEC_H */
