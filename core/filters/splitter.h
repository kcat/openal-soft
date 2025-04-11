#ifndef CORE_FILTERS_SPLITTER_H
#define CORE_FILTERS_SPLITTER_H

#include <span>


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
    explicit BandSplitterR(Real f0norm) { init(f0norm); }
    BandSplitterR& operator=(const BandSplitterR&) = default;

    void init(Real f0norm);
    void clear() noexcept { mLpZ1 = mLpZ2 = mApZ1 = 0.0f; }
    void process(const std::span<const Real> input, const std::span<Real> hpout,
        const std::span<Real> lpout);

    void processHfScale(const std::span<const Real> input, const std::span<Real> output,
        const Real hfscale);

    void processHfScale(const std::span<Real> samples, const Real hfscale);
    void processScale(const std::span<Real> samples, const Real hfscale, const Real lfscale);

    /**
     * The all-pass portion of the band splitter. Applies the same phase shift
     * without splitting or scaling the signal.
     */
    void processAllPass(const std::span<Real> samples);
};
using BandSplitter = BandSplitterR<float>;

#endif /* CORE_FILTERS_SPLITTER_H */
