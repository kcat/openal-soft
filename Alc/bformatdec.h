#ifndef BFORMATDEC_H
#define BFORMATDEC_H

#include "alMain.h"

struct AmbDecConf;
struct BFormatDec;

struct BFormatDec *bformatdec_alloc();
void bformatdec_free(struct BFormatDec *dec);
void bformatdec_reset(struct BFormatDec *dec, const struct AmbDecConf *conf, ALuint chancount, ALuint srate, const ALuint chanmap[MAX_OUTPUT_CHANNELS]);
void bformatdec_process(struct BFormatDec *dec, ALfloat (*restrict OutBuffer)[BUFFERSIZE], ALuint OutChannels, ALfloat (*restrict InSamples)[BUFFERSIZE], ALuint SamplesToDo);

#endif /* BFORMATDEC_H */
