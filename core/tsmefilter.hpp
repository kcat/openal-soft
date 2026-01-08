#ifndef CORE_TSMEFILTER_HPP
#define CORE_TSMEFILTER_HPP

#include <array>
#include <span>
#include <string_view>

#include "allpass_iir.hpp"
#include "altypes.hpp"
#include "bufferline.h"
#include "encoderbase.hpp"


inline constexpr auto TsmeLength256 = 256_uz;
inline constexpr auto TsmeLength512 = 512_uz;

enum class TsmeQualityType : u8 {
    IIR = 0,
    FIR256,
    FIR512,
    Default = IIR
};

inline auto TsmeDecodeQuality = TsmeQualityType::Default;
inline auto TsmeEncodeQuality = TsmeQualityType::Default;


template<usize N>
struct TsmeEncoder final : EncoderBase {
    struct Tag { using encoder_t = TsmeEncoder; };

    static constexpr auto sFftLength = 256_uz;
    static constexpr auto sSegmentSize = sFftLength/2_uz;
    static constexpr auto sNumSegments = N/sSegmentSize;
    static constexpr auto sFilterDelay = N/2_uz + sSegmentSize;

    static consteval auto TypeName() noexcept -> std::string_view
    {
        if constexpr(N == 256)
            return "FIR-256";
        else if constexpr(N == 512)
            return "FIR-512";
    }

    /* Delays and processing storage for the input signal. */
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mW{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mY{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mZ{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mX{};

    alignas(16) std::array<float,BufferLineSize> mS{};
    alignas(16) std::array<float,BufferLineSize> mD{};

    /* History and temp storage for the convolution filter. */
    usize mFifoPos{}, mCurrentSegment{};
    alignas(16) std::array<float,sFftLength> mWXInOut{};
    alignas(16) std::array<float,sFftLength> mFftBuffer{};
    alignas(16) std::array<float,sFftLength> mWorkData{};
    alignas(16) std::array<float,sFftLength*sNumSegments> mWXHistory{};

    alignas(16) std::array<std::array<float,sFilterDelay>,2> mDirectDelay{};

    auto getDelay() noexcept -> usize final { return sFilterDelay; }

    /**
     * Encodes a 2-channel tetraphonic surround matrix-encoded (stereo
     * compatible) signal from a B-Format input signal. The input must use ACN
     * channel ordering and N3D scaling.
     */
    auto encode(std::span<float> LeftOut, std::span<float> RightOut,
        std::span<const std::span<const float>> InSamples) -> void final;
};

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
