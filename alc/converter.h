#ifndef CONVERTER_H
#define CONVERTER_H

#include <cstddef>
#include <memory>

#include "AL/al.h"

#include "alcmain.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "alu.h"
#include "devformat.h"


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

    ALuint convert(const ALvoid **src, ALuint *srcframes, ALvoid *dst, ALuint dstframes);
    ALuint availableOut(ALuint srcframes) const;

    DEF_FAM_NEWDEL(SampleConverter, mChan)
};
using SampleConverterPtr = std::unique_ptr<SampleConverter>;

SampleConverterPtr CreateSampleConverter(DevFmtType srcType, DevFmtType dstType, ALsizei numchans,
    ALsizei srcRate, ALsizei dstRate, Resampler resampler);


struct ChannelConverter {
    DevFmtType mSrcType;
    DevFmtChannels mSrcChans;
    DevFmtChannels mDstChans;

    bool is_active() const noexcept { return mSrcChans != mDstChans; }

    void convert(const ALvoid *src, ALfloat *dst, ALuint frames) const;
};

#endif /* CONVERTER_H */
