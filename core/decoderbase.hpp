#ifndef CORE_DECODERBASE_HPP
#define CORE_DECODERBASE_HPP

#include <cstddef>
#include <span>


struct DecoderBase {
    static constexpr auto sMaxPadding = std::size_t{256};

    /* For 2-channel UHJ, shelf filters should use these LF responses. */
    static constexpr auto sWLFScale = 0.661f;
    static constexpr auto sXYLFScale = 1.293f;

    DecoderBase() = default;
    DecoderBase(const DecoderBase&) = delete;
    DecoderBase(DecoderBase&&) = delete;
    virtual ~DecoderBase() = default;

    void operator=(const DecoderBase&) = delete;
    void operator=(DecoderBase&&) = delete;

    virtual void decode(std::span<std::span<float>> samples, bool updateState) = 0;

    /**
     * The width factor for Super Stereo processing. Can be changed in between
     * calls to decode, with valid values being between 0...0.7.
     *
     * 0.46 seems to produce the least amount of channel bleed when the output
     * is subsequently UHJ encoded (given a stereo sound with a noise on the
     * left buffer channel, for instance, when decoded with UhjStereoDecoder
     * and then encoded with UhjEncoder, the right output channel was at its
     * quietest).
     */
    float mWidthControl{0.46f};
};

#endif /* CORE_DECODERBASE_HPP */
