#ifndef CORE_UHJFILTER_H
#define CORE_UHJFILTER_H

#include <array>

#include "almalloc.h"
#include "alspan.h"
#include "bufferline.h"


static constexpr size_t UhjLength256{256};
static constexpr size_t UhjLength512{512};

enum class UhjQualityType : uint8_t {
    IIR = 0,
    FIR256,
    FIR512,
    Default = IIR
};

extern UhjQualityType UhjDecodeQuality;
extern UhjQualityType UhjEncodeQuality;


struct UhjAllPassFilter {
    struct AllPassState {
        /* Last two delayed components for direct form II. */
        float z[2];
    };
    std::array<AllPassState,4> mState;

    void process(const al::span<const float,4> coeffs, const al::span<const float> src,
        const bool update, float *RESTRICT dst);
};


struct UhjEncoderBase {
    virtual ~UhjEncoderBase() = default;

    virtual size_t getDelay() noexcept = 0;

    /**
     * Encodes a 2-channel UHJ (stereo-compatible) signal from a B-Format input
     * signal. The input must use FuMa channel ordering and UHJ scaling (FuMa
     * with an additional +3dB boost).
     */
    virtual void encode(float *LeftOut, float *RightOut,
        const al::span<const float*const,3> InSamples, const size_t SamplesToDo) = 0;
};

template<size_t N>
struct UhjEncoder final : public UhjEncoderBase {
    static constexpr size_t sFilterDelay{N/2};

    /* Delays and processing storage for the input signal. */
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mW{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mX{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mY{};

    alignas(16) std::array<float,BufferLineSize> mS{};
    alignas(16) std::array<float,BufferLineSize> mD{};

    /* History and temp storage for the FIR filter. New samples should be
     * written to index sFilterDelay*2 - 1.
     */
    static constexpr size_t sWXInOffset{sFilterDelay*2 - 1};
    alignas(16) std::array<float,BufferLineSize + sFilterDelay*2> mWX{};

    alignas(16) std::array<std::array<float,sFilterDelay>,2> mDirectDelay{};

    size_t getDelay() noexcept override { return sFilterDelay; }

    /**
     * Encodes a 2-channel UHJ (stereo-compatible) signal from a B-Format input
     * signal. The input must use FuMa channel ordering and UHJ scaling (FuMa
     * with an additional +3dB boost).
     */
    void encode(float *LeftOut, float *RightOut, const al::span<const float*const,3> InSamples,
        const size_t SamplesToDo) override;

    DEF_NEWDEL(UhjEncoder)
};

struct UhjEncoderIIR final : public UhjEncoderBase {
    static constexpr size_t sFilterDelay{1};

    /* Processing storage for the input signal. */
    alignas(16) std::array<float,BufferLineSize+1> mS{};
    alignas(16) std::array<float,BufferLineSize+1> mD{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mWX{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mTemp{};
    float mDelayWX{}, mDelayY{};

    UhjAllPassFilter mFilter1WX;
    UhjAllPassFilter mFilter2WX;
    UhjAllPassFilter mFilter1Y;

    std::array<UhjAllPassFilter,2> mFilter1Direct;
    std::array<float,2> mDirectDelay{};

    size_t getDelay() noexcept override { return sFilterDelay; }

    /**
     * Encodes a 2-channel UHJ (stereo-compatible) signal from a B-Format input
     * signal. The input must use FuMa channel ordering and UHJ scaling (FuMa
     * with an additional +3dB boost).
     */
    void encode(float *LeftOut, float *RightOut, const al::span<const float*const,3> InSamples,
        const size_t SamplesToDo) override;

    DEF_NEWDEL(UhjEncoderIIR)
};


struct DecoderBase {
    static constexpr size_t sMaxPadding{256};

    /* For 2-channel UHJ, shelf filters should use these LF responses. */
    static constexpr float sWLFScale{0.661f};
    static constexpr float sXYLFScale{1.293f};

    virtual ~DecoderBase() = default;

    virtual void decode(const al::span<float*> samples, const size_t samplesToDo,
        const bool updateState) = 0;

    /**
     * The width factor for Super Stereo processing. Can be changed in between
     * calls to decode, with valid values being between 0...0.7.
     */
    float mWidthControl{0.593f};
};

template<size_t N>
struct UhjDecoder final : public DecoderBase {
    /* The number of extra sample frames needed for input. */
    static constexpr size_t sInputPadding{N/2};

    alignas(16) std::array<float,BufferLineSize+sInputPadding> mS{};
    alignas(16) std::array<float,BufferLineSize+sInputPadding> mD{};
    alignas(16) std::array<float,BufferLineSize+sInputPadding> mT{};

    alignas(16) std::array<float,sInputPadding-1> mDTHistory{};
    alignas(16) std::array<float,sInputPadding-1> mSHistory{};

    alignas(16) std::array<float,BufferLineSize + sInputPadding*2> mTemp{};

    /**
     * Decodes a 3- or 4-channel UHJ signal into a B-Format signal with FuMa
     * channel ordering and UHJ scaling. For 3-channel, the 3rd channel may be
     * attenuated by 'n', where 0 <= n <= 1. So to decode 2-channel UHJ, supply
     * 3 channels with the 3rd channel silent (n=0). The B-Format signal
     * reconstructed from 2-channel UHJ should not be run through a normal
     * B-Format decoder, as it needs different shelf filters.
     */
    void decode(const al::span<float*> samples, const size_t samplesToDo,
        const bool updateState) override;

    DEF_NEWDEL(UhjDecoder)
};

struct UhjDecoderIIR final : public DecoderBase {
    /* FIXME: These IIR decoder filters actually have a 1-sample delay on the
     * non-filtered components, which is not reflected in the source latency
     * value. sInputPadding is 0, however, because it doesn't need any extra
     * input samples.
     */
    static constexpr size_t sInputPadding{0};

    alignas(16) std::array<float,BufferLineSize> mS{};
    alignas(16) std::array<float,BufferLineSize> mD{};
    alignas(16) std::array<float,BufferLineSize+1> mTemp{};
    float mDelayS{}, mDelayDT{}, mDelayQ{};

    UhjAllPassFilter mFilter1S;
    UhjAllPassFilter mFilter2DT;
    UhjAllPassFilter mFilter1DT;
    UhjAllPassFilter mFilter2S;
    UhjAllPassFilter mFilter1Q;

    void decode(const al::span<float*> samples, const size_t samplesToDo,
        const bool updateState) override;

    DEF_NEWDEL(UhjDecoderIIR)
};

template<size_t N>
struct UhjStereoDecoder final : public DecoderBase {
    static constexpr size_t sInputPadding{N/2};

    float mCurrentWidth{-1.0f};

    alignas(16) std::array<float,BufferLineSize+sInputPadding> mS{};
    alignas(16) std::array<float,BufferLineSize+sInputPadding> mD{};

    alignas(16) std::array<float,sInputPadding-1> mDTHistory{};
    alignas(16) std::array<float,sInputPadding-1> mSHistory{};

    alignas(16) std::array<float,BufferLineSize + sInputPadding*2> mTemp{};

    /**
     * Applies Super Stereo processing on a stereo signal to create a B-Format
     * signal with FuMa channel ordering and UHJ scaling. The samples span
     * should contain 3 channels, the first two being the left and right stereo
     * channels, and the third left empty.
     */
    void decode(const al::span<float*> samples, const size_t samplesToDo,
        const bool updateState) override;

    DEF_NEWDEL(UhjStereoDecoder)
};

struct UhjStereoDecoderIIR final : public DecoderBase {
    static constexpr size_t sInputPadding{0};

    float mCurrentWidth{-1.0f};

    alignas(16) std::array<float,BufferLineSize> mS{};
    alignas(16) std::array<float,BufferLineSize> mD{};
    alignas(16) std::array<float,BufferLineSize+1> mTemp{};
    float mDelayS{}, mDelayD{};

    UhjAllPassFilter mFilter1S;
    UhjAllPassFilter mFilter2D;
    UhjAllPassFilter mFilter1D;
    UhjAllPassFilter mFilter2S;

    void decode(const al::span<float*> samples, const size_t samplesToDo,
        const bool updateState) override;

    DEF_NEWDEL(UhjStereoDecoderIIR)
};

#endif /* CORE_UHJFILTER_H */
