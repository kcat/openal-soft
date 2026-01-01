#ifndef CORE_ENCODERBASE_HPP
#define CORE_ENCODERBASE_HPP

#include <span>

#include "altypes.hpp"


struct EncoderBase {
    EncoderBase() = default;
    EncoderBase(const EncoderBase&) = delete;
    EncoderBase(EncoderBase&&) = delete;
    virtual ~EncoderBase() = default;

    auto operator=(const EncoderBase&) -> void = delete;
    auto operator=(EncoderBase&&) -> void = delete;

    virtual auto getDelay() noexcept -> usize = 0;

    /** Encodes a 2-channel output signal from a B-Format input signal. */
    virtual auto encode(std::span<float> LeftOut, std::span<float> RightOut,
        std::span<std::span<float const> const, 3> InSamples) -> void = 0;
};

#endif /* CORE_ENCODERBASE_HPP */
