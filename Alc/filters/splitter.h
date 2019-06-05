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
    BandSplitterR() = default;
    BandSplitterR(const BandSplitterR&) = default;
    BandSplitterR(Real f0norm) { init(f0norm); }

    void init(Real f0norm);
    void clear() noexcept { lp_z1 = lp_z2 = ap_z1 = 0.0f; }
    void process(Real *hpout, Real *lpout, const Real *input, const int count);

    void applyHfScale(Real *samples, const Real hfscale, const int count);

    /* The all-pass portion of the band splitter. Applies the same phase shift
     * without splitting the signal. Note that each use of this method is
     * indepedent, it does not track history between calls.
     */
    void applyAllpass(Real *samples, const int count) const;
};
using BandSplitter = BandSplitterR<float>;


struct FrontStablizer {
    static constexpr size_t DelayLength{256u};

    alignas(16) float DelayBuf[MAX_OUTPUT_CHANNELS][DelayLength];

    BandSplitter LFilter, RFilter;
    alignas(16) float LSplit[2][BUFFERSIZE];
    alignas(16) float RSplit[2][BUFFERSIZE];

    alignas(16) float TempBuf[BUFFERSIZE + DelayLength];

    DEF_NEWDEL(FrontStablizer)
};

#endif /* FILTER_SPLITTER_H */
