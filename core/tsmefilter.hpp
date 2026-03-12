#ifndef CORE_TSMEFILTER_HPP
#define CORE_TSMEFILTER_HPP

#include <array>
#include <span>
#include <string_view>

#include "allpass_iir.hpp"
#include "altypes.hpp"
#include "bufferline.h"
#include "decoderbase.hpp"
#include "encoderbase.hpp"


inline constexpr auto TsmeLength256 = 256_uz;
inline constexpr auto TsmeLength512 = 512_uz;

enum class TsmeQualityType : u8::value_t {
    IIR = 0,
    FIR256,
    FIR512,
    Default = IIR
};

inline auto TsmeDecodeQuality = TsmeQualityType::Default;
inline auto TsmeEncodeQuality = TsmeQualityType::Default;


template<std::size_t N>
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
    std::size_t mFifoPos{}, mCurrentSegment{};
    alignas(16) std::array<float,sFftLength> mWXInOut{};
    alignas(16) std::array<float,sFftLength> mFftBuffer{};
    alignas(16) std::array<float,sFftLength> mWorkData{};
    alignas(16) std::array<float,sFftLength*sNumSegments> mWXHistory{};

    alignas(16) std::array<std::array<float,sFilterDelay>,2> mDirectDelay{};

    auto getDelay() noexcept -> std::size_t final { return sFilterDelay; }

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

    auto getDelay() noexcept -> std::size_t final { return sFilterDelay; }

    /**
     * Encodes a 2-channel tetraphonic surround matrix-encoded (stereo
     * compatible) signal from a B-Format input signal. The input must use ACN
     * channel ordering and N3D scaling.
     */
    auto encode(std::span<float> LeftOut, std::span<float> RightOut,
        std::span<const std::span<const float>> InSamples) -> void final;
};

template<std::size_t N>
struct TsmeStereoDecoder final : DecoderBase {
    struct Tag { using decoder_t = TsmeStereoDecoder; };

    static constexpr auto sInputPadding = N/2_uz;

    float mCurrentWidth{-1.0f};

    alignas(16) std::array<float,BufferLineSize+sInputPadding> mS{};
    alignas(16) std::array<float,BufferLineSize+sInputPadding> mD{};

    alignas(16) std::array<float,sInputPadding-1> mDTHistory{};
    alignas(16) std::array<float,sInputPadding-1> mSHistory{};

    alignas(16) std::array<float,BufferLineSize + sInputPadding*2_uz> mTemp{};

    /**
     * Applies Super Stereo processing on a stereo signal to create a B-Format
     * signal with FuMa channel ordering and N3D scaling. The samples span
     * should contain 3 channels, the first two being the left and right stereo
     * channels, and the third left empty.
     */
    void decode(std::span<std::span<float>> samples, bool updateState) final;
};

struct TsmeStereoDecoderIIR final : DecoderBase {
    struct Tag { using decoder_t = TsmeStereoDecoderIIR; };

    static constexpr auto sInputPadding = 1_uz;

    bool mFirstRun{true};
    float mCurrentWidth{-1.0f};

    alignas(16) std::array<float,BufferLineSize+sInputPadding> mS{};
    alignas(16) std::array<float,BufferLineSize+sInputPadding> mD{};
    alignas(16) std::array<float,BufferLineSize> mTemp{};

    AllPassFilter mFilter1S;
    AllPassFilter mFilter2D;
    AllPassFilter mFilter1D;
    AllPassFilter mFilter2S;

    void decode(std::span<std::span<float>> samples, bool updateState) final;
};

#endif /* CORE_TSMEFILTER_HPP */
