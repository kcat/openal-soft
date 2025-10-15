#ifndef CORE_FILTERS_SPLITTER_H
#define CORE_FILTERS_SPLITTER_H

#include <span>

#include "alnumeric.h"


/* Band splitter. Splits a signal into two phase-matching frequency bands. */
class BandSplitter {
    f32 mCoeff{0.0f};
    f32 mLpZ1{0.0f};
    f32 mLpZ2{0.0f};
    f32 mApZ1{0.0f};

public:
    BandSplitter() = default;
    BandSplitter(BandSplitter const&) = default;
    explicit BandSplitter(f32 const f0norm) { init(f0norm); }
    auto operator=(BandSplitter const&) -> BandSplitter& = default;

    void init(f32 f0norm);
    void clear() noexcept { mLpZ1 = mLpZ2 = mApZ1 = 0.0f; }
    void process(std::span<f32 const> input, std::span<f32> hpout, std::span<f32> lpout);

    void processHfScale(std::span<f32 const> input, std::span<f32> output, f32 hfscale);

    void processHfScale(std::span<f32> samples, f32 hfscale);
    void processScale(std::span<f32> samples, f32 hfscale, f32 lfscale);

    /**
     * The all-pass portion of the band splitter. Applies the same phase shift
     * without splitting or scaling the signal.
     */
    void processAllPass(std::span<f32> samples);
};

#endif /* CORE_FILTERS_SPLITTER_H */
