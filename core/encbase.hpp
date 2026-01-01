#ifndef CORE_ENCBASE_HPP
#define CORE_ENCBASE_HPP

#include <span>

#include "altypes.hpp"


struct UhjEncoderBase {
    UhjEncoderBase() = default;
    UhjEncoderBase(const UhjEncoderBase&) = delete;
    UhjEncoderBase(UhjEncoderBase&&) = delete;
    virtual ~UhjEncoderBase() = default;

    auto operator=(const UhjEncoderBase&) -> void = delete;
    auto operator=(UhjEncoderBase&&) -> void = delete;

    virtual auto getDelay() noexcept -> usize = 0;

    /** Encodes a 2-channel output signal from a B-Format input signal. */
    virtual auto encode(std::span<float> LeftOut, std::span<float> RightOut,
        std::span<std::span<float const> const, 3> InSamples) -> void = 0;
};

#endif /* CORE_ENCBASE_HPP */
