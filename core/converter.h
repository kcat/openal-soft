#ifndef CORE_CONVERTER_H
#define CORE_CONVERTER_H

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>

#include "almalloc.h"
#include "devformat.h"
#include "flexarray.h"
#include "mixer/defs.h"
#include "resampler_limits.h"


class SampleConverter {
    explicit SampleConverter(std::size_t const numchans) : mChan{numchans} { }

public:
    DevFmtType mSrcType{};
    DevFmtType mDstType{};
    unsigned mSrcTypeSize{};
    unsigned mDstTypeSize{};

    unsigned mSrcPrepCount{};

    unsigned mFracOffset{};
    unsigned mIncrement{};
    InterpState mState;
    ResamplerFunc mResample{};

    alignas(16) FloatBufferLine mSrcSamples{};
    alignas(16) FloatBufferLine mDstSamples{};

    struct ChanSamples {
        alignas(16) std::array<float, MaxResamplerPadding> PrevSamples;
    };
    al::FlexArray<ChanSamples> mChan;

    [[nodiscard]] auto convert(const void **src, unsigned *srcframes, void *dst, unsigned dstframes) -> unsigned;
    [[nodiscard]] auto convertPlanar(const void **src, unsigned *srcframes, void *const*dst, unsigned dstframes) -> unsigned;
    [[nodiscard]] auto availableOut(unsigned srcframes) const -> unsigned;

    using SampleOffset = std::chrono::duration<std::int64_t, std::ratio<1,MixerFracOne>>;
    [[nodiscard]] auto currentInputDelay() const noexcept -> SampleOffset
    {
        auto const prep = std::int64_t{mSrcPrepCount} - MaxResamplerEdge;
        return SampleOffset{(prep<<MixerFracBits) + mFracOffset};
    }

    static auto Create(DevFmtType srcType, DevFmtType dstType, std::size_t numchans,
        unsigned srcRate, unsigned dstRate, Resampler resampler)
        -> std::unique_ptr<SampleConverter>;

    DEF_FAM_NEWDEL(SampleConverter, mChan)
};
using SampleConverterPtr = std::unique_ptr<SampleConverter>;

struct ChannelConverter {
    DevFmtType mSrcType{};
    unsigned mSrcStep{};
    unsigned mChanMask{};
    DevFmtChannels mDstChans{};

    [[nodiscard]] auto is_active() const noexcept -> bool { return mChanMask != 0; }

    void convert(const void *src, float *dst, unsigned frames) const;
};

#endif /* CORE_CONVERTER_H */
