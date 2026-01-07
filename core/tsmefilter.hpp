#ifndef CORE_TSMEFILTER_HPP
#define CORE_TSMEFILTER_HPP

#include <array>
#include <span>
#include <string_view>

#include "allpass_iir.hpp"
#include "altypes.hpp"
#include "bufferline.h"
#include "encoderbase.hpp"


struct TsmeEncoderIIR final : EncoderBase {
    struct Tag { using encoder_t = TsmeEncoderIIR; };

    static constexpr auto sFilterDelay = 1_uz;

    static consteval auto TypeName() noexcept -> std::string_view
    { return "IIR"; }

    /* Processing storage for the input signal. */
    alignas(16) std::array<float,BufferLineSize+1> mS{};
    alignas(16) std::array<float,BufferLineSize+1> mD{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mWX{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mTemp{};
    float mDelayWXZ{}, mDelayY{};

    AllPassFilter mFilter1WXZ;
    AllPassFilter mFilter2WX;
    AllPassFilter mFilter1Y;

    std::array<AllPassFilter,2> mFilter1Direct;
    std::array<float,2> mDirectDelay{};

    auto getDelay() noexcept -> usize final { return sFilterDelay; }

    /**
     * Encodes a 2-channel tetraphonic surround matrix-encoded (stereo
     * compatible) signal from a B-Format input signal. The input must use ACN
     * channel ordering and N3D scaling.
     */
    auto encode(std::span<float> LeftOut, std::span<float> RightOut,
        std::span<const std::span<const float>> InSamples) -> void final;
};

#endif /* CORE_TSMEFILTER_HPP */
