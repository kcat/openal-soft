#ifndef FILTER_SPLITTER_H
#define FILTER_SPLITTER_H

#include <cstddef>


/* Band splitter. Splits a signal into two phase-matching frequency bands. */
template<typename Real>
class BandSplitterR {
    Real mCoeff{0.0f};
    Real mLpZ1{0.0f};
    Real mLpZ2{0.0f};
    Real mApZ1{0.0f};

public:
    BandSplitterR() = default;
    BandSplitterR(const BandSplitterR&) = default;
    BandSplitterR(Real f0norm) { init(f0norm); }

    void init(Real f0norm);
    void clear() noexcept { mLpZ1 = mLpZ2 = mApZ1 = 0.0f; }
    void process(Real *hpout, Real *lpout, const Real *input, const size_t count);

    void applyHfScale(Real *samples, const Real hfscale, const size_t count);

    /* The all-pass portion of the band splitter. Applies the same phase shift
     * without splitting the signal. Note that each use of this method is
     * indepedent, it does not track history between calls.
     */
    void applyAllpass(Real *samples, const size_t count) const;
};
using BandSplitter = BandSplitterR<float>;

#endif /* FILTER_SPLITTER_H */
