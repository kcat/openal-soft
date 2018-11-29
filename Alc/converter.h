#ifndef CONVERTER_H
#define CONVERTER_H

#include "alMain.h"
#include "alu.h"
#include "almalloc.h"

struct SampleConverter {
    enum DevFmtType mSrcType;
    enum DevFmtType mDstType;
    ALsizei mNumChannels;
    ALsizei mSrcTypeSize;
    ALsizei mDstTypeSize;

    ALint mSrcPrepCount;

    ALsizei mFracOffset;
    ALsizei mIncrement;
    InterpState mState;
    ResamplerFunc mResample;

    alignas(16) ALfloat mSrcSamples[BUFFERSIZE];
    alignas(16) ALfloat mDstSamples[BUFFERSIZE];

    struct {
        alignas(16) ALfloat mPrevSamples[MAX_RESAMPLE_PADDING*2];
    } Chan[];

    DEF_PLACE_NEWDEL()
};

SampleConverter *CreateSampleConverter(enum DevFmtType srcType, enum DevFmtType dstType,
                                       ALsizei numchans, ALsizei srcRate, ALsizei dstRate,
                                       Resampler resampler);
void DestroySampleConverter(SampleConverter **converter);

ALsizei SampleConverterInput(SampleConverter *converter, const ALvoid **src, ALsizei *srcframes, ALvoid *dst, ALsizei dstframes);
ALsizei SampleConverterAvailableOut(SampleConverter *converter, ALsizei srcframes);


struct ChannelConverter {
    enum DevFmtType mSrcType;
    enum DevFmtChannels mSrcChans;
    enum DevFmtChannels mDstChans;

    DEF_PLACE_NEWDEL()
};

ChannelConverter *CreateChannelConverter(enum DevFmtType srcType, enum DevFmtChannels srcChans, enum DevFmtChannels dstChans);
void DestroyChannelConverter(ChannelConverter **converter);

void ChannelConverterInput(ChannelConverter *converter, const ALvoid *src, ALfloat *dst, ALsizei frames);

#endif /* CONVERTER_H */
