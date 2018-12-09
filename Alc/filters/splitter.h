#ifndef FILTER_SPLITTER_H
#define FILTER_SPLITTER_H

#include "alMain.h"
#include "almalloc.h"


/* Band splitter. Splits a signal into two phase-matching frequency bands. */
class BandSplitter {
    float coeff{0.0f};
    float lp_z1{0.0f};
    float lp_z2{0.0f};
    float ap_z1{0.0f};

public:
    void init(float f0norm);
    void clear() noexcept { lp_z1 = lp_z2 = ap_z1 = 0.0f; }
    void process(float *RESTRICT hpout, float *RESTRICT lpout, const float *input, int count);
};

/* The all-pass portion of the band splitter. Applies the same phase shift
 * without splitting the signal.
 */
class SplitterAllpass {
    float coeff{0.0f};
    float z1{0.0f};

public:
    void init(float f0norm);
    void clear() noexcept { z1 = 0.0f; }
    void process(float *RESTRICT samples, int count);
};


struct FrontStablizer {
    SplitterAllpass APFilter[MAX_OUTPUT_CHANNELS];
    BandSplitter LFilter, RFilter;
    alignas(16) float LSplit[2][BUFFERSIZE];
    alignas(16) float RSplit[2][BUFFERSIZE];

    DEF_NEWDEL(FrontStablizer)
};

#endif /* FILTER_SPLITTER_H */
