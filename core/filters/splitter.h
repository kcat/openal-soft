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
    BandSplitter(const BandSplitter&) = default;
    explicit BandSplitter(float f0norm) { init(f0norm); }
    BandSplitter& operator=(const BandSplitter&) = default;

    void init(float f0norm);
    void clear() noexcept { mLpZ1 = mLpZ2 = mApZ1 = 0.0f; }
    void process(const std::span<const float> input, const std::span<float> hpout,
        const std::span<float> lpout);

    void processHfScale(const std::span<const float> input, const std::span<float> output,
        const float hfscale);

    void processHfScale(const std::span<float> samples, const float hfscale);
    void processScale(const std::span<float> samples, const float hfscale, const float lfscale);

    /**
     * The all-pass portion of the band splitter. Applies the same phase shift
     * without splitting or scaling the signal.
     */
    void processAllPass(const std::span<float> samples);
};

#endif /* CORE_FILTERS_SPLITTER_H */
