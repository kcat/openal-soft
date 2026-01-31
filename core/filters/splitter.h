#ifndef CORE_FILTERS_SPLITTER_H
#define CORE_FILTERS_SPLITTER_H

#include <span>


/* Band splitter. Splits a signal into two phase-matching frequency bands. */
class BandSplitter {
    float mCoeff{0.0f};
    float mLpZ1{0.0f};
    float mLpZ2{0.0f};
    float mApZ1{0.0f};

public:
    BandSplitter() = default;
    BandSplitter(BandSplitter const&) = default;
    explicit BandSplitter(float const f0norm) { init(f0norm); }
    auto operator=(BandSplitter const&) -> BandSplitter& = default;

    void init(float f0norm);
    void clear() noexcept { mLpZ1 = mLpZ2 = mApZ1 = 0.0f; }
    void process(std::span<float const> input, std::span<float> hpout, std::span<float> lpout);

    void processHfScale(std::span<float const> input, std::span<float> output, float hfscale);

    void processHfScale(std::span<float> samples, float hfscale);
    void processScale(std::span<float> samples, float hfscale, float lfscale);

    /**
     * The all-pass portion of the band splitter. Applies the same phase shift
     * without splitting or scaling the signal.
     */
    void processAllPass(std::span<float> samples);
};

#endif /* CORE_FILTERS_SPLITTER_H */
