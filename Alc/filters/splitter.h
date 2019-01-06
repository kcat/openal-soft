#ifndef FILTER_SPLITTER_H
#define FILTER_SPLITTER_H

#include "alMain.h"
#include "almalloc.h"


/* Band splitter. Splits a signal into two phase-matching frequency bands. */
template<typename Real>
class BandSplitterR {
    Real coeff{0.0f};
    Real lp_z1{0.0f};
    Real lp_z2{0.0f};
    Real ap_z1{0.0f};

public:
    void init(Real f0norm);
    void clear() noexcept { lp_z1 = lp_z2 = ap_z1 = 0.0f; }
    void process(Real *RESTRICT hpout, Real *RESTRICT lpout, const Real *input, int count);
};
using BandSplitter = BandSplitterR<float>;

/* The all-pass portion of the band splitter. Applies the same phase shift
 * without splitting the signal.
 */
template<typename Real>
class SplitterAllpassR {
    Real coeff{0.0f};
    Real z1{0.0f};

public:
    void init(Real f0norm);
    void clear() noexcept { z1 = 0.0f; }
    void process(Real *RESTRICT samples, int count);
};
using SplitterAllpass = SplitterAllpassR<float>;


struct FrontStablizer {
    SplitterAllpass APFilter[MAX_OUTPUT_CHANNELS];
    BandSplitter LFilter, RFilter;
    alignas(16) float LSplit[2][BUFFERSIZE];
    alignas(16) float RSplit[2][BUFFERSIZE];

    DEF_NEWDEL(FrontStablizer)
};

#endif /* FILTER_SPLITTER_H */
