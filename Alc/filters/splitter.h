#ifndef FILTER_SPLITTER_H
#define FILTER_SPLITTER_H

#include "alMain.h"
#include "almalloc.h"


/* Band splitter. Splits a signal into two phase-matching frequency bands. */
struct BandSplitter {
    float coeff{0.0f};
    float lp_z1{0.0f};
    float lp_z2{0.0f};
    float hp_z1{0.0f};
};

void bandsplit_init(BandSplitter *splitter, float f0norm);
void bandsplit_clear(BandSplitter *splitter);
void bandsplit_process(BandSplitter *splitter, float *RESTRICT hpout, float *RESTRICT lpout,
                       const float *input, int count);

/* The all-pass portion of the band splitter. Applies the same phase shift
 * without splitting the signal.
 */
struct SplitterAllpass {
    float coeff{0.0f};
    float z1{0.0f};
};

void splitterap_init(SplitterAllpass *splitter, float f0norm);
void splitterap_clear(SplitterAllpass *splitter);
void splitterap_process(SplitterAllpass *splitter, float *RESTRICT samples, int count);


struct FrontStablizer {
    SplitterAllpass APFilter[MAX_OUTPUT_CHANNELS];
    BandSplitter LFilter, RFilter;
    alignas(16) float LSplit[2][BUFFERSIZE];
    alignas(16) float RSplit[2][BUFFERSIZE];

    DEF_NEWDEL(FrontStablizer)
};

#endif /* FILTER_SPLITTER_H */
