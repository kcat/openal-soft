
#include "config.h"

#include "converter.h"

#include "fpu_modes.h"
#include "mixer/defs.h"


namespace {

/* Base template left undefined. Should be marked =delete, but Clang 3.8.1
 * chokes on that given the inline specializations.
 */
template<DevFmtType T>
inline ALfloat LoadSample(typename DevFmtTypeTraits<T>::Type val);

template<> inline ALfloat LoadSample<DevFmtByte>(DevFmtTypeTraits<DevFmtByte>::Type val)
{ return val * (1.0f/128.0f); }
template<> inline ALfloat LoadSample<DevFmtShort>(DevFmtTypeTraits<DevFmtShort>::Type val)
{ return val * (1.0f/32768.0f); }
template<> inline ALfloat LoadSample<DevFmtInt>(DevFmtTypeTraits<DevFmtInt>::Type val)
{ return (val>>7) * (1.0f/16777216.0f); }
template<> inline ALfloat LoadSample<DevFmtFloat>(DevFmtTypeTraits<DevFmtFloat>::Type val)
{ return val; }

template<> inline ALfloat LoadSample<DevFmtUByte>(DevFmtTypeTraits<DevFmtUByte>::Type val)
{ return LoadSample<DevFmtByte>(val - 128); }
template<> inline ALfloat LoadSample<DevFmtUShort>(DevFmtTypeTraits<DevFmtUShort>::Type val)
{ return LoadSample<DevFmtByte>(val - 32768); }
template<> inline ALfloat LoadSample<DevFmtUInt>(DevFmtTypeTraits<DevFmtUInt>::Type val)
{ return LoadSample<DevFmtByte>(val - 2147483648u); }


template<DevFmtType T>
inline void LoadSampleArray(ALfloat *RESTRICT dst, const void *src, ALint srcstep, ALsizei samples)
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(ALsizei i{0};i < samples;i++)
        dst[i] = LoadSample<T>(ssrc[i*srcstep]);
}

void LoadSamples(ALfloat *dst, const ALvoid *src, ALint srcstep, enum DevFmtType srctype,
                 ALsizei samples)
{
#define HANDLE_FMT(T)                                                         \
    case T: LoadSampleArray<T>(dst, src, srcstep, samples); break
    switch(srctype)
    {
        HANDLE_FMT(DevFmtByte);
        HANDLE_FMT(DevFmtUByte);
        HANDLE_FMT(DevFmtShort);
        HANDLE_FMT(DevFmtUShort);
        HANDLE_FMT(DevFmtInt);
        HANDLE_FMT(DevFmtUInt);
        HANDLE_FMT(DevFmtFloat);
    }
#undef HANDLE_FMT
}


template<DevFmtType T>
inline typename DevFmtTypeTraits<T>::Type StoreSample(ALfloat);

template<> inline ALfloat StoreSample<DevFmtFloat>(ALfloat val)
{ return val; }
template<> inline ALint StoreSample<DevFmtInt>(ALfloat val)
{ return fastf2i(clampf(val*16777216.0f, -16777216.0f, 16777215.0f))<<7; }
template<> inline ALshort StoreSample<DevFmtShort>(ALfloat val)
{ return fastf2i(clampf(val*32768.0f, -32768.0f, 32767.0f)); }
template<> inline ALbyte StoreSample<DevFmtByte>(ALfloat val)
{ return fastf2i(clampf(val*128.0f, -128.0f, 127.0f)); }

/* Define unsigned output variations. */
template<> inline ALuint StoreSample<DevFmtUInt>(ALfloat val)
{ return StoreSample<DevFmtInt>(val) + 2147483648u; }
template<> inline ALushort StoreSample<DevFmtUShort>(ALfloat val)
{ return StoreSample<DevFmtShort>(val) + 32768; }
template<> inline ALubyte StoreSample<DevFmtUByte>(ALfloat val)
{ return StoreSample<DevFmtByte>(val) + 128; }

template<DevFmtType T>
inline void StoreSampleArray(void *dst, const ALfloat *RESTRICT src, ALint dststep,
                             ALsizei samples)
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    SampleType *sdst = static_cast<SampleType*>(dst);
    for(ALsizei i{0};i < samples;i++)
        sdst[i*dststep] = StoreSample<T>(src[i]);
}


void StoreSamples(ALvoid *dst, const ALfloat *src, ALint dststep, enum DevFmtType dsttype, ALsizei samples)
{
#define HANDLE_FMT(T)                                                         \
    case T: StoreSampleArray<T>(dst, src, dststep, samples); break
    switch(dsttype)
    {
        HANDLE_FMT(DevFmtByte);
        HANDLE_FMT(DevFmtUByte);
        HANDLE_FMT(DevFmtShort);
        HANDLE_FMT(DevFmtUShort);
        HANDLE_FMT(DevFmtInt);
        HANDLE_FMT(DevFmtUInt);
        HANDLE_FMT(DevFmtFloat);
    }
#undef HANDLE_FMT
}


template<DevFmtType T>
void Mono2Stereo(ALfloat *RESTRICT dst, const void *src, ALsizei frames)
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(ALsizei i{0};i < frames;i++)
        dst[i*2 + 1] = dst[i*2 + 0] = LoadSample<T>(ssrc[i]) * 0.707106781187f;
}

template<DevFmtType T>
void Stereo2Mono(ALfloat *RESTRICT dst, const void *src, ALsizei frames)
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(ALsizei i{0};i < frames;i++)
        dst[i] = (LoadSample<T>(ssrc[i*2 + 0])+LoadSample<T>(ssrc[i*2 + 1])) *
                 0.707106781187f;
}

} // namespace

SampleConverter *CreateSampleConverter(enum DevFmtType srcType, enum DevFmtType dstType, ALsizei numchans, ALsizei srcRate, ALsizei dstRate)
{
    SampleConverter *converter;
    ALsizei step;

    if(numchans <= 0 || srcRate <= 0 || dstRate <= 0)
        return NULL;

    converter = static_cast<SampleConverter*>(al_calloc(16,
        FAM_SIZE(SampleConverter, Chan, numchans)));
    converter->mSrcType = srcType;
    converter->mDstType = dstType;
    converter->mNumChannels = numchans;
    converter->mSrcTypeSize = BytesFromDevFmt(srcType);
    converter->mDstTypeSize = BytesFromDevFmt(dstType);

    converter->mSrcPrepCount = 0;
    converter->mFracOffset = 0;

    /* Have to set the mixer FPU mode since that's what the resampler code expects. */
    FPUCtl mixer_mode{};
    step = (ALsizei)mind(((ALdouble)srcRate/dstRate*FRACTIONONE) + 0.5,
                         MAX_PITCH * FRACTIONONE);
    converter->mIncrement = maxi(step, 1);
    if(converter->mIncrement == FRACTIONONE)
        converter->mResample = Resample_copy_C;
    else
    {
        /* TODO: Allow other resamplers. */
        BsincPrepare(converter->mIncrement, &converter->mState.bsinc, &bsinc12);
        converter->mResample = SelectResampler(BSinc12Resampler);
    }

    return converter;
}

void DestroySampleConverter(SampleConverter **converter)
{
    if(converter)
    {
        al_free(*converter);
        *converter = NULL;
    }
}


ALsizei SampleConverterAvailableOut(SampleConverter *converter, ALsizei srcframes)
{
    ALint prepcount = converter->mSrcPrepCount;
    ALsizei increment = converter->mIncrement;
    ALsizei DataPosFrac = converter->mFracOffset;
    ALuint64 DataSize64;

    if(prepcount < 0)
    {
        /* Negative prepcount means we need to skip that many input samples. */
        if(-prepcount >= srcframes)
            return 0;
        srcframes += prepcount;
        prepcount = 0;
    }

    if(srcframes < 1)
    {
        /* No output samples if there's no input samples. */
        return 0;
    }

    if(prepcount < MAX_RESAMPLE_PADDING*2 &&
       MAX_RESAMPLE_PADDING*2 - prepcount >= srcframes)
    {
        /* Not enough input samples to generate an output sample. */
        return 0;
    }

    DataSize64  = prepcount;
    DataSize64 += srcframes;
    DataSize64 -= MAX_RESAMPLE_PADDING*2;
    DataSize64 <<= FRACTIONBITS;
    DataSize64 -= DataPosFrac;

    /* If we have a full prep, we can generate at least one sample. */
    return (ALsizei)clampu64((DataSize64 + increment-1)/increment, 1, BUFFERSIZE);
}

ALsizei SampleConverterInput(SampleConverter *converter, const ALvoid **src, ALsizei *srcframes, ALvoid *dst, ALsizei dstframes)
{
    const ALsizei SrcFrameSize = converter->mNumChannels * converter->mSrcTypeSize;
    const ALsizei DstFrameSize = converter->mNumChannels * converter->mDstTypeSize;
    const ALsizei increment = converter->mIncrement;
    ALsizei pos = 0;

    FPUCtl mixer_mode{};
    while(pos < dstframes && *srcframes > 0)
    {
        ALfloat *RESTRICT SrcData = converter->mSrcSamples;
        ALfloat *RESTRICT DstData = converter->mDstSamples;
        ALint prepcount = converter->mSrcPrepCount;
        ALsizei DataPosFrac = converter->mFracOffset;
        ALuint64 DataSize64;
        ALsizei DstSize;
        ALint toread;
        ALsizei chan;

        if(prepcount < 0)
        {
            /* Negative prepcount means we need to skip that many input samples. */
            if(-prepcount >= *srcframes)
            {
                converter->mSrcPrepCount = prepcount + *srcframes;
                *srcframes = 0;
                break;
            }
            *src = (const ALbyte*)*src + SrcFrameSize*-prepcount;
            *srcframes += prepcount;
            converter->mSrcPrepCount = 0;
            continue;
        }
        toread = mini(*srcframes, BUFFERSIZE - MAX_RESAMPLE_PADDING*2);

        if(prepcount < MAX_RESAMPLE_PADDING*2 &&
           MAX_RESAMPLE_PADDING*2 - prepcount >= toread)
        {
            /* Not enough input samples to generate an output sample. Store
             * what we're given for later.
             */
            for(chan = 0;chan < converter->mNumChannels;chan++)
                LoadSamples(&converter->Chan[chan].mPrevSamples[prepcount],
                    (const ALbyte*)*src + converter->mSrcTypeSize*chan,
                    converter->mNumChannels, converter->mSrcType, toread
                );

            converter->mSrcPrepCount = prepcount + toread;
            *srcframes = 0;
            break;
        }

        DataSize64  = prepcount;
        DataSize64 += toread;
        DataSize64 -= MAX_RESAMPLE_PADDING*2;
        DataSize64 <<= FRACTIONBITS;
        DataSize64 -= DataPosFrac;

        /* If we have a full prep, we can generate at least one sample. */
        DstSize = (ALsizei)clampu64((DataSize64 + increment-1)/increment, 1, BUFFERSIZE);
        DstSize = mini(DstSize, dstframes-pos);

        for(chan = 0;chan < converter->mNumChannels;chan++)
        {
            const ALbyte *SrcSamples = (const ALbyte*)*src + converter->mSrcTypeSize*chan;
            ALbyte *DstSamples = (ALbyte*)dst + converter->mDstTypeSize*chan;
            const ALfloat *ResampledData;
            ALsizei SrcDataEnd;

            /* Load the previous samples into the source data first, then the
             * new samples from the input buffer.
             */
            memcpy(SrcData, converter->Chan[chan].mPrevSamples,
                   prepcount*sizeof(ALfloat));
            LoadSamples(SrcData + prepcount, SrcSamples,
                converter->mNumChannels, converter->mSrcType, toread
            );

            /* Store as many prep samples for next time as possible, given the
             * number of output samples being generated.
             */
            SrcDataEnd = (DataPosFrac + increment*DstSize)>>FRACTIONBITS;
            if(SrcDataEnd >= prepcount+toread)
                memset(converter->Chan[chan].mPrevSamples, 0,
                       sizeof(converter->Chan[chan].mPrevSamples));
            else
            {
                size_t len = mini(MAX_RESAMPLE_PADDING*2, prepcount+toread-SrcDataEnd);
                memcpy(converter->Chan[chan].mPrevSamples, &SrcData[SrcDataEnd],
                       len*sizeof(ALfloat));
                memset(converter->Chan[chan].mPrevSamples+len, 0,
                       sizeof(converter->Chan[chan].mPrevSamples) - len*sizeof(ALfloat));
            }

            /* Now resample, and store the result in the output buffer. */
            ResampledData = converter->mResample(&converter->mState,
                SrcData+MAX_RESAMPLE_PADDING, DataPosFrac, increment,
                DstData, DstSize
            );

            StoreSamples(DstSamples, ResampledData, converter->mNumChannels,
                         converter->mDstType, DstSize);
        }

        /* Update the number of prep samples still available, as well as the
         * fractional offset.
         */
        DataPosFrac += increment*DstSize;
        converter->mSrcPrepCount = mini(prepcount + toread - (DataPosFrac>>FRACTIONBITS),
                                        MAX_RESAMPLE_PADDING*2);
        converter->mFracOffset = DataPosFrac & FRACTIONMASK;

        /* Update the src and dst pointers in case there's still more to do. */
        *src = (const ALbyte*)*src + SrcFrameSize*(DataPosFrac>>FRACTIONBITS);
        *srcframes -= mini(*srcframes, (DataPosFrac>>FRACTIONBITS));

        dst = (ALbyte*)dst + DstFrameSize*DstSize;
        pos += DstSize;
    }

    return pos;
}


ChannelConverter *CreateChannelConverter(enum DevFmtType srcType, enum DevFmtChannels srcChans, enum DevFmtChannels dstChans)
{
    ChannelConverter *converter;

    if(srcChans != dstChans && !((srcChans == DevFmtMono && dstChans == DevFmtStereo) ||
                                 (srcChans == DevFmtStereo && dstChans == DevFmtMono)))
        return NULL;

    converter = static_cast<ChannelConverter*>(al_calloc(DEF_ALIGN, sizeof(*converter)));
    converter->mSrcType = srcType;
    converter->mSrcChans = srcChans;
    converter->mDstChans = dstChans;

    return converter;
}

void DestroyChannelConverter(ChannelConverter **converter)
{
    if(converter)
    {
        al_free(*converter);
        *converter = NULL;
    }
}

void ChannelConverterInput(ChannelConverter *converter, const ALvoid *src, ALfloat *dst, ALsizei frames)
{
    if(converter->mSrcChans == converter->mDstChans)
    {
        LoadSamples(dst, src, 1, converter->mSrcType,
                    frames*ChannelsFromDevFmt(converter->mSrcChans, 0));
        return;
    }

    if(converter->mSrcChans == DevFmtStereo && converter->mDstChans == DevFmtMono)
    {
        switch(converter->mSrcType)
        {
#define HANDLE_FMT(T) case T: Stereo2Mono<T>(dst, src, frames); break
        HANDLE_FMT(DevFmtByte);
        HANDLE_FMT(DevFmtUByte);
        HANDLE_FMT(DevFmtShort);
        HANDLE_FMT(DevFmtUShort);
        HANDLE_FMT(DevFmtInt);
        HANDLE_FMT(DevFmtUInt);
        HANDLE_FMT(DevFmtFloat);
#undef HANDLE_FMT
        }
    }
    else /*if(converter->mSrcChans == DevFmtMono && converter->mDstChans == DevFmtStereo)*/
    {
        switch(converter->mSrcType)
        {
#define HANDLE_FMT(T) case T: Mono2Stereo<T>(dst, src, frames); break
        HANDLE_FMT(DevFmtByte);
        HANDLE_FMT(DevFmtUByte);
        HANDLE_FMT(DevFmtShort);
        HANDLE_FMT(DevFmtUShort);
        HANDLE_FMT(DevFmtInt);
        HANDLE_FMT(DevFmtUInt);
        HANDLE_FMT(DevFmtFloat);
#undef HANDLE_FMT
        }
    }
}
