#ifndef CORE_CONVERTER_H
#define CORE_CONVERTER_H

#include <chrono>
#include <memory>

#include "almalloc.h"
#include "altypes.hpp"
#include "devformat.h"
#include "flexarray.h"
#include "mixer/defs.h"
#include "resampler_limits.h"


class SampleConverter {
    explicit SampleConverter(usize const numchans) : mChan{numchans} { }

public:
    DevFmtType mSrcType{};
    DevFmtType mDstType{};
    u32 mSrcTypeSize{};
    u32 mDstTypeSize{};

    u32 mSrcPrepCount{};

    u32 mFracOffset{};
    u32 mIncrement{};
    InterpState mState;
    ResamplerFunc mResample{};

    alignas(16) FloatBufferLine mSrcSamples{};
    alignas(16) FloatBufferLine mDstSamples{};

    struct ChanSamples {
        alignas(16) std::array<f32, MaxResamplerPadding> PrevSamples;
    };
    al::FlexArray<ChanSamples> mChan;

    [[nodiscard]] auto convert(const void **src, u32 *srcframes, void *dst, u32 dstframes) -> u32;
    [[nodiscard]] auto convertPlanar(const void **src, u32 *srcframes, void *const*dst, u32 dstframes) -> u32;
    [[nodiscard]] auto availableOut(u32 srcframes) const -> u32;

    using SampleOffset = std::chrono::duration<i64, std::ratio<1,MixerFracOne>>;
    [[nodiscard]] auto currentInputDelay() const noexcept -> SampleOffset
    {
        auto const prep = i64{mSrcPrepCount} - MaxResamplerEdge;
        return SampleOffset{(prep<<MixerFracBits) + mFracOffset};
    }

    static auto Create(DevFmtType srcType, DevFmtType dstType, usize numchans, u32 srcRate,
        u32 dstRate, Resampler resampler) -> std::unique_ptr<SampleConverter>;

    DEF_FAM_NEWDEL(SampleConverter, mChan)
};
using SampleConverterPtr = std::unique_ptr<SampleConverter>;

struct ChannelConverter {
    DevFmtType mSrcType{};
    u32 mSrcStep{};
    u32 mChanMask{};
    DevFmtChannels mDstChans{};

    [[nodiscard]] auto is_active() const noexcept -> bool { return mChanMask != 0; }

    void convert(const void *src, f32 *dst, u32 frames) const;
};

#endif /* CORE_CONVERTER_H */
