#ifndef CONVERTER_H
#define CONVERTER_H

#include <memory>

#include "alMain.h"
#include "alu.h"
#include "almalloc.h"

struct SampleConverter {
    DevFmtType mSrcType{};
    DevFmtType mDstType{};
    ALsizei mNumChannels{};
    ALsizei mSrcTypeSize{};
    ALsizei mDstTypeSize{};

    ALint mSrcPrepCount{};

    ALsizei mFracOffset{};
    ALsizei mIncrement{};
    InterpState mState{};
    ResamplerFunc mResample{};

    alignas(16) ALfloat mSrcSamples[BUFFERSIZE]{};
    alignas(16) ALfloat mDstSamples[BUFFERSIZE]{};

    struct {
        alignas(16) ALfloat mPrevSamples[MAX_RESAMPLE_PADDING*2];
    } Chan[];

    DEF_PLACE_NEWDEL()
};
using SampleConverterPtr = std::unique_ptr<SampleConverter>;

SampleConverterPtr CreateSampleConverter(DevFmtType srcType, DevFmtType dstType, ALsizei numchans,
    ALsizei srcRate, ALsizei dstRate, Resampler resampler);

ALsizei SampleConverterInput(SampleConverter *converter, const ALvoid **src, ALsizei *srcframes, ALvoid *dst, ALsizei dstframes);
ALsizei SampleConverterAvailableOut(SampleConverter *converter, ALsizei srcframes);


struct ChannelConverter {
    DevFmtType mSrcType;
    DevFmtChannels mSrcChans;
    DevFmtChannels mDstChans;

    ChannelConverter(DevFmtType srctype, DevFmtChannels srcchans, DevFmtChannels dstchans)
      : mSrcType(srctype), mSrcChans(srcchans), mDstChans(dstchans)
    { }
    DEF_NEWDEL(ChannelConverter)
};
using ChannelConverterPtr = std::unique_ptr<ChannelConverter>;

ChannelConverterPtr CreateChannelConverter(DevFmtType srcType, DevFmtChannels srcChans,
    DevFmtChannels dstChans);

void ChannelConverterInput(ChannelConverter *converter, const ALvoid *src, ALfloat *dst,
    ALsizei frames);

#endif /* CONVERTER_H */
