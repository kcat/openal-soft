#ifndef CONVERTER_H
#define CONVERTER_H

#include <memory>

#include "alMain.h"
#include "alu.h"
#include "almalloc.h"

struct SampleConverter {
    DevFmtType mSrcType{};
    DevFmtType mDstType{};
    ALsizei mSrcTypeSize{};
    ALsizei mDstTypeSize{};

    ALint mSrcPrepCount{};

    ALsizei mFracOffset{};
    ALsizei mIncrement{};
    InterpState mState{};
    ResamplerFunc mResample{};

    alignas(16) ALfloat mSrcSamples[BUFFERSIZE]{};
    alignas(16) ALfloat mDstSamples[BUFFERSIZE]{};

    struct ChanSamples {
        alignas(16) ALfloat PrevSamples[MAX_RESAMPLE_PADDING*2];
    };
    al::FlexArray<ChanSamples> mChan;

    SampleConverter(size_t numchans) : mChan{numchans} { }
    SampleConverter(const SampleConverter&) = delete;
    SampleConverter& operator=(const SampleConverter&) = delete;

    ALsizei convert(const ALvoid **src, ALsizei *srcframes, ALvoid *dst, ALsizei dstframes);
    ALsizei availableOut(ALsizei srcframes) const;

    static constexpr size_t Sizeof(size_t length) noexcept
    {
        return maxz(sizeof(SampleConverter),
            al::FlexArray<ChanSamples>::Sizeof(length, offsetof(SampleConverter, mChan)));
    }

    DEF_PLACE_NEWDEL()
};
using SampleConverterPtr = std::unique_ptr<SampleConverter>;

SampleConverterPtr CreateSampleConverter(DevFmtType srcType, DevFmtType dstType, ALsizei numchans,
    ALsizei srcRate, ALsizei dstRate, Resampler resampler);


struct ChannelConverter {
    DevFmtType mSrcType;
    DevFmtChannels mSrcChans;
    DevFmtChannels mDstChans;

    ChannelConverter(DevFmtType srctype, DevFmtChannels srcchans, DevFmtChannels dstchans)
      : mSrcType(srctype), mSrcChans(srcchans), mDstChans(dstchans)
    { }

    void convert(const ALvoid *src, ALfloat *dst, ALsizei frames) const;

    DEF_NEWDEL(ChannelConverter)
};
using ChannelConverterPtr = std::unique_ptr<ChannelConverter>;

ChannelConverterPtr CreateChannelConverter(DevFmtType srcType, DevFmtChannels srcChans,
    DevFmtChannels dstChans);

#endif /* CONVERTER_H */
