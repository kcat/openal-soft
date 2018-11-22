#ifndef FILTER_SPLITTER_H
#define FILTER_SPLITTER_H

#include "alMain.h"
#include "almalloc.h"


/* Band splitter. Splits a signal into two phase-matching frequency bands. */
struct BandSplitter {
    ALfloat coeff{0.0f};
    ALfloat lp_z1{0.0f};
    ALfloat lp_z2{0.0f};
    ALfloat hp_z1{0.0f};
};

void bandsplit_init(BandSplitter *splitter, ALfloat f0norm);
void bandsplit_clear(BandSplitter *splitter);
void bandsplit_process(BandSplitter *splitter, ALfloat *RESTRICT hpout, ALfloat *RESTRICT lpout,
                       const ALfloat *input, ALsizei count);

/* The all-pass portion of the band splitter. Applies the same phase shift
 * without splitting the signal.
 */
struct SplitterAllpass {
    ALfloat coeff{0.0f};
    ALfloat z1{0.0f};
};

void splitterap_init(SplitterAllpass *splitter, ALfloat f0norm);
void splitterap_clear(SplitterAllpass *splitter);
void splitterap_process(SplitterAllpass *splitter, ALfloat *RESTRICT samples, ALsizei count);


struct FrontStablizer {
    SplitterAllpass APFilter[MAX_OUTPUT_CHANNELS];
    BandSplitter LFilter, RFilter;
    alignas(16) ALfloat LSplit[2][BUFFERSIZE];
    alignas(16) ALfloat RSplit[2][BUFFERSIZE];

    DEF_NEWDEL(FrontStablizer)
};

#endif /* FILTER_SPLITTER_H */
