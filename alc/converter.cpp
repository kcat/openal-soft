
#include "config.h"

#include "converter.h"

#include <algorithm>

#include "fpu_modes.h"
#include "mixer/defs.h"


namespace {

/* Base template left undefined. Should be marked =delete, but Clang 3.8.1
 * chokes on that given the inline specializations.
 */
template<DevFmtType T>
inline ALfloat LoadSample(typename DevFmtTypeTraits<T>::Type val) noexcept;

template<> inline ALfloat LoadSample<DevFmtByte>(DevFmtTypeTraits<DevFmtByte>::Type val) noexcept
{ return val * (1.0f/128.0f); }
template<> inline ALfloat LoadSample<DevFmtShort>(DevFmtTypeTraits<DevFmtShort>::Type val) noexcept
{ return val * (1.0f/32768.0f); }
template<> inline ALfloat LoadSample<DevFmtInt>(DevFmtTypeTraits<DevFmtInt>::Type val) noexcept
{ return val * (1.0f/2147483648.0f); }
template<> inline ALfloat LoadSample<DevFmtFloat>(DevFmtTypeTraits<DevFmtFloat>::Type val) noexcept
{ return val; }

template<> inline ALfloat LoadSample<DevFmtUByte>(DevFmtTypeTraits<DevFmtUByte>::Type val) noexcept
{ return LoadSample<DevFmtByte>(val - 128); }
template<> inline ALfloat LoadSample<DevFmtUShort>(DevFmtTypeTraits<DevFmtUShort>::Type val) noexcept
{ return LoadSample<DevFmtShort>(val - 32768); }
template<> inline ALfloat LoadSample<DevFmtUInt>(DevFmtTypeTraits<DevFmtUInt>::Type val) noexcept
{ return LoadSample<DevFmtInt>(val - 2147483648u); }


template<DevFmtType T>
inline void LoadSampleArray(ALfloat *RESTRICT dst, const void *src, const size_t srcstep,
    const ALsizei samples) noexcept
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(ALsizei i{0};i < samples;i++)
        dst[i] = LoadSample<T>(ssrc[i*srcstep]);
}

void LoadSamples(ALfloat *dst, const ALvoid *src, const size_t srcstep, const DevFmtType srctype,
    const ALsizei samples) noexcept
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
inline typename DevFmtTypeTraits<T>::Type StoreSample(ALfloat) noexcept;

template<> inline ALfloat StoreSample<DevFmtFloat>(ALfloat val) noexcept
{ return val; }
template<> inline ALint StoreSample<DevFmtInt>(ALfloat val) noexcept
{ return fastf2i(clampf(val*2147483648.0f, -2147483648.0f, 2147483520.0f)); }
template<> inline ALshort StoreSample<DevFmtShort>(ALfloat val) noexcept
{ return fastf2i(clampf(val*32768.0f, -32768.0f, 32767.0f)); }
template<> inline ALbyte StoreSample<DevFmtByte>(ALfloat val) noexcept
{ return fastf2i(clampf(val*128.0f, -128.0f, 127.0f)); }

/* Define unsigned output variations. */
template<> inline ALuint StoreSample<DevFmtUInt>(ALfloat val) noexcept
{ return StoreSample<DevFmtInt>(val) + 2147483648u; }
template<> inline ALushort StoreSample<DevFmtUShort>(ALfloat val) noexcept
{ return StoreSample<DevFmtShort>(val) + 32768; }
template<> inline ALubyte StoreSample<DevFmtUByte>(ALfloat val) noexcept
{ return StoreSample<DevFmtByte>(val) + 128; }

template<DevFmtType T>
inline void StoreSampleArray(void *dst, const ALfloat *RESTRICT src, const size_t dststep,
    const ALsizei samples) noexcept
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    SampleType *sdst = static_cast<SampleType*>(dst);
    for(ALsizei i{0};i < samples;i++)
        sdst[i*dststep] = StoreSample<T>(src[i]);
}


void StoreSamples(ALvoid *dst, const ALfloat *src, const size_t dststep, const DevFmtType dsttype,
    const ALsizei samples) noexcept
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
void Mono2Stereo(ALfloat *RESTRICT dst, const void *src, const ALsizei frames) noexcept
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(ALsizei i{0};i < frames;i++)
        dst[i*2 + 1] = dst[i*2 + 0] = LoadSample<T>(ssrc[i]) * 0.707106781187f;
}

template<DevFmtType T>
void Stereo2Mono(ALfloat *RESTRICT dst, const void *src, const ALsizei frames) noexcept
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(ALsizei i{0};i < frames;i++)
        dst[i] = (LoadSample<T>(ssrc[i*2 + 0])+LoadSample<T>(ssrc[i*2 + 1])) *
                 0.707106781187f;
}

} // namespace

SampleConverterPtr CreateSampleConverter(DevFmtType srcType, DevFmtType dstType, ALsizei numchans,
                                         ALsizei srcRate, ALsizei dstRate, Resampler resampler)
{
    if(numchans <= 0 || srcRate <= 0 || dstRate <= 0)
        return nullptr;

    void *ptr{al_calloc(16, SampleConverter::Sizeof(numchans))};
    SampleConverterPtr converter{new (ptr) SampleConverter{static_cast<size_t>(numchans)}};
    converter->mSrcType = srcType;
    converter->mDstType = dstType;
    converter->mSrcTypeSize = BytesFromDevFmt(srcType);
    converter->mDstTypeSize = BytesFromDevFmt(dstType);

    converter->mSrcPrepCount = 0;
    converter->mFracOffset = 0;

    /* Have to set the mixer FPU mode since that's what the resampler code expects. */
    FPUCtl mixer_mode{};
    auto step = static_cast<ALsizei>(
        mind(static_cast<ALdouble>(srcRate)/dstRate*FRACTIONONE + 0.5, MAX_PITCH*FRACTIONONE));
    converter->mIncrement = maxi(step, 1);
    if(converter->mIncrement == FRACTIONONE)
        converter->mResample = Resample_<CopyTag,CTag>;
    else
    {
        if(resampler == BSinc24Resampler)
            BsincPrepare(converter->mIncrement, &converter->mState.bsinc, &bsinc24);
        else if(resampler == BSinc12Resampler)
            BsincPrepare(converter->mIncrement, &converter->mState.bsinc, &bsinc12);
        converter->mResample = SelectResampler(resampler);
    }

    return converter;
}

ALsizei SampleConverter::availableOut(ALsizei srcframes) const
{
    ALint prepcount{mSrcPrepCount};
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

    auto DataSize64 = static_cast<uint64_t>(prepcount);
    DataSize64 += srcframes;
    DataSize64 -= MAX_RESAMPLE_PADDING*2;
    DataSize64 <<= FRACTIONBITS;
    DataSize64 -= mFracOffset;

    /* If we have a full prep, we can generate at least one sample. */
    return static_cast<ALsizei>(clampu64((DataSize64 + mIncrement-1)/mIncrement, 1, BUFFERSIZE));
}

ALsizei SampleConverter::convert(const ALvoid **src, ALsizei *srcframes, ALvoid *dst, ALsizei dstframes)
{
    const ALsizei SrcFrameSize{static_cast<ALsizei>(mChan.size()) * mSrcTypeSize};
    const ALsizei DstFrameSize{static_cast<ALsizei>(mChan.size()) * mDstTypeSize};
    const ALsizei increment{mIncrement};
    auto SamplesIn = static_cast<const al::byte*>(*src);
    ALsizei NumSrcSamples{*srcframes};

    FPUCtl mixer_mode{};
    ALsizei pos{0};
    while(pos < dstframes && NumSrcSamples > 0)
    {
        ALint prepcount{mSrcPrepCount};
        if(prepcount < 0)
        {
            /* Negative prepcount means we need to skip that many input samples. */
            if(-prepcount >= NumSrcSamples)
            {
                mSrcPrepCount = prepcount + NumSrcSamples;
                NumSrcSamples = 0;
                break;
            }
            SamplesIn += SrcFrameSize*-prepcount;
            NumSrcSamples += prepcount;
            mSrcPrepCount = 0;
            continue;
        }
        ALint toread{mini(NumSrcSamples, BUFFERSIZE - MAX_RESAMPLE_PADDING*2)};

        if(prepcount < MAX_RESAMPLE_PADDING*2 &&
           MAX_RESAMPLE_PADDING*2 - prepcount >= toread)
        {
            /* Not enough input samples to generate an output sample. Store
             * what we're given for later.
             */
            for(size_t chan{0u};chan < mChan.size();chan++)
                LoadSamples(&mChan[chan].PrevSamples[prepcount], SamplesIn + mSrcTypeSize*chan,
                    mChan.size(), mSrcType, toread);

            mSrcPrepCount = prepcount + toread;
            NumSrcSamples = 0;
            break;
        }

        ALfloat *RESTRICT SrcData{mSrcSamples};
        ALfloat *RESTRICT DstData{mDstSamples};
        ALsizei DataPosFrac{mFracOffset};
        auto DataSize64 = static_cast<uint64_t>(prepcount);
        DataSize64 += toread;
        DataSize64 -= MAX_RESAMPLE_PADDING*2;
        DataSize64 <<= FRACTIONBITS;
        DataSize64 -= DataPosFrac;

        /* If we have a full prep, we can generate at least one sample. */
        auto DstSize = static_cast<ALsizei>(
            clampu64((DataSize64 + increment-1)/increment, 1, BUFFERSIZE));
        DstSize = mini(DstSize, dstframes-pos);

        for(size_t chan{0u};chan < mChan.size();chan++)
        {
            const al::byte *SrcSamples{SamplesIn + mSrcTypeSize*chan};
            al::byte *DstSamples = static_cast<al::byte*>(dst) + mDstTypeSize*chan;

            /* Load the previous samples into the source data first, then the
             * new samples from the input buffer.
             */
            std::copy_n(mChan[chan].PrevSamples, prepcount, SrcData);
            LoadSamples(SrcData + prepcount, SrcSamples, mChan.size(), mSrcType, toread);

            /* Store as many prep samples for next time as possible, given the
             * number of output samples being generated.
             */
            ALsizei SrcDataEnd{(DstSize*increment + DataPosFrac)>>FRACTIONBITS};
            if(SrcDataEnd >= prepcount+toread)
                std::fill(std::begin(mChan[chan].PrevSamples),
                          std::end(mChan[chan].PrevSamples), 0.0f);
            else
            {
                size_t len = mini(MAX_RESAMPLE_PADDING*2, prepcount+toread-SrcDataEnd);
                std::copy_n(SrcData+SrcDataEnd, len, mChan[chan].PrevSamples);
                std::fill(std::begin(mChan[chan].PrevSamples)+len,
                          std::end(mChan[chan].PrevSamples), 0.0f);
            }

            /* Now resample, and store the result in the output buffer. */
            const ALfloat *ResampledData{mResample(&mState, SrcData+MAX_RESAMPLE_PADDING,
                DataPosFrac, increment, DstData, DstSize)};

            StoreSamples(DstSamples, ResampledData, mChan.size(), mDstType, DstSize);
        }

        /* Update the number of prep samples still available, as well as the
         * fractional offset.
         */
        DataPosFrac += increment*DstSize;
        mSrcPrepCount = mini(prepcount + toread - (DataPosFrac>>FRACTIONBITS),
            MAX_RESAMPLE_PADDING*2);
        mFracOffset = DataPosFrac & FRACTIONMASK;

        /* Update the src and dst pointers in case there's still more to do. */
        SamplesIn += SrcFrameSize*(DataPosFrac>>FRACTIONBITS);
        NumSrcSamples -= mini(NumSrcSamples, (DataPosFrac>>FRACTIONBITS));

        dst = static_cast<al::byte*>(dst) + DstFrameSize*DstSize;
        pos += DstSize;
    }

    *src = SamplesIn;
    *srcframes = NumSrcSamples;

    return pos;
}


ChannelConverterPtr CreateChannelConverter(DevFmtType srcType, DevFmtChannels srcChans, DevFmtChannels dstChans)
{
    if(srcChans != dstChans && !((srcChans == DevFmtMono && dstChans == DevFmtStereo) ||
                                 (srcChans == DevFmtStereo && dstChans == DevFmtMono)))
        return nullptr;
    return al::make_unique<ChannelConverter>(srcType, srcChans, dstChans);
}

void ChannelConverter::convert(const ALvoid *src, ALfloat *dst, ALsizei frames) const
{
    if(mSrcChans == DevFmtStereo && mDstChans == DevFmtMono)
    {
        switch(mSrcType)
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
    else if(mSrcChans == DevFmtMono && mDstChans == DevFmtStereo)
    {
        switch(mSrcType)
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
    else
        LoadSamples(dst, src, 1u, mSrcType, frames*ChannelsFromDevFmt(mSrcChans, 0));
}
