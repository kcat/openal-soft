
#include "config.h"

#include "converter.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

#include "AL/al.h"

#include "albyte.h"
#include "alu.h"
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
{ return static_cast<float>(val) * (1.0f/2147483648.0f); }
template<> inline ALfloat LoadSample<DevFmtFloat>(DevFmtTypeTraits<DevFmtFloat>::Type val) noexcept
{ return val; }

template<> inline ALfloat LoadSample<DevFmtUByte>(DevFmtTypeTraits<DevFmtUByte>::Type val) noexcept
{ return LoadSample<DevFmtByte>(static_cast<ALbyte>(val - 128)); }
template<> inline ALfloat LoadSample<DevFmtUShort>(DevFmtTypeTraits<DevFmtUShort>::Type val) noexcept
{ return LoadSample<DevFmtShort>(static_cast<ALshort>(val - 32768)); }
template<> inline ALfloat LoadSample<DevFmtUInt>(DevFmtTypeTraits<DevFmtUInt>::Type val) noexcept
{ return LoadSample<DevFmtInt>(static_cast<ALint>(val - 2147483648u)); }


template<DevFmtType T>
inline void LoadSampleArray(ALfloat *RESTRICT dst, const void *src, const size_t srcstep,
    const size_t samples) noexcept
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(size_t i{0u};i < samples;i++)
        dst[i] = LoadSample<T>(ssrc[i*srcstep]);
}

void LoadSamples(ALfloat *dst, const ALvoid *src, const size_t srcstep, const DevFmtType srctype,
    const size_t samples) noexcept
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
{ return static_cast<ALshort>(fastf2i(clampf(val*32768.0f, -32768.0f, 32767.0f))); }
template<> inline ALbyte StoreSample<DevFmtByte>(ALfloat val) noexcept
{ return static_cast<ALbyte>(fastf2i(clampf(val*128.0f, -128.0f, 127.0f))); }

/* Define unsigned output variations. */
template<> inline ALuint StoreSample<DevFmtUInt>(ALfloat val) noexcept
{ return static_cast<ALuint>(StoreSample<DevFmtInt>(val)) + 2147483648u; }
template<> inline ALushort StoreSample<DevFmtUShort>(ALfloat val) noexcept
{ return static_cast<ALushort>(StoreSample<DevFmtShort>(val) + 32768); }
template<> inline ALubyte StoreSample<DevFmtUByte>(ALfloat val) noexcept
{ return static_cast<ALubyte>(StoreSample<DevFmtByte>(val) + 128); }

template<DevFmtType T>
inline void StoreSampleArray(void *dst, const ALfloat *RESTRICT src, const size_t dststep,
    const size_t samples) noexcept
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    SampleType *sdst = static_cast<SampleType*>(dst);
    for(size_t i{0u};i < samples;i++)
        sdst[i*dststep] = StoreSample<T>(src[i]);
}


void StoreSamples(ALvoid *dst, const ALfloat *src, const size_t dststep, const DevFmtType dsttype,
    const size_t samples) noexcept
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
void Mono2Stereo(ALfloat *RESTRICT dst, const void *src, const size_t frames) noexcept
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(size_t i{0u};i < frames;i++)
        dst[i*2 + 1] = dst[i*2 + 0] = LoadSample<T>(ssrc[i]) * 0.707106781187f;
}

template<DevFmtType T>
void Stereo2Mono(ALfloat *RESTRICT dst, const void *src, const size_t frames) noexcept
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const SampleType *ssrc = static_cast<const SampleType*>(src);
    for(size_t i{0u};i < frames;i++)
        dst[i] = (LoadSample<T>(ssrc[i*2 + 0])+LoadSample<T>(ssrc[i*2 + 1])) *
                 0.707106781187f;
}

} // namespace

SampleConverterPtr CreateSampleConverter(DevFmtType srcType, DevFmtType dstType, size_t numchans,
    ALuint srcRate, ALuint dstRate, Resampler resampler)
{
    if(numchans < 1 || srcRate < 1 || dstRate < 1)
        return nullptr;

    SampleConverterPtr converter{new (FamCount{numchans}) SampleConverter{numchans}};
    converter->mSrcType = srcType;
    converter->mDstType = dstType;
    converter->mSrcTypeSize = BytesFromDevFmt(srcType);
    converter->mDstTypeSize = BytesFromDevFmt(dstType);

    converter->mSrcPrepCount = 0;
    converter->mFracOffset = 0;

    /* Have to set the mixer FPU mode since that's what the resampler code expects. */
    FPUCtl mixer_mode{};
    auto step = static_cast<ALuint>(
        mind(srcRate*double{FRACTIONONE}/dstRate + 0.5, MAX_PITCH*FRACTIONONE));
    converter->mIncrement = maxu(step, 1);
    if(converter->mIncrement == FRACTIONONE)
        converter->mResample = Resample_<CopyTag,CTag>;
    else
        converter->mResample = PrepareResampler(resampler, converter->mIncrement,
            &converter->mState);

    return converter;
}

ALuint SampleConverter::availableOut(ALuint srcframes) const
{
    ALint prepcount{mSrcPrepCount};
    if(prepcount < 0)
    {
        /* Negative prepcount means we need to skip that many input samples. */
        if(static_cast<ALuint>(-prepcount) >= srcframes)
            return 0;
        srcframes -= static_cast<ALuint>(-prepcount);
        prepcount = 0;
    }

    if(srcframes < 1)
    {
        /* No output samples if there's no input samples. */
        return 0;
    }

    if(prepcount < MAX_RESAMPLER_PADDING
        && static_cast<ALuint>(MAX_RESAMPLER_PADDING - prepcount) >= srcframes)
    {
        /* Not enough input samples to generate an output sample. */
        return 0;
    }

    auto DataSize64 = static_cast<uint64_t>(prepcount);
    DataSize64 += srcframes;
    DataSize64 -= MAX_RESAMPLER_PADDING;
    DataSize64 <<= FRACTIONBITS;
    DataSize64 -= mFracOffset;

    /* If we have a full prep, we can generate at least one sample. */
    return static_cast<ALuint>(clampu64((DataSize64 + mIncrement-1)/mIncrement, 1, BUFFERSIZE));
}

ALuint SampleConverter::convert(const ALvoid **src, ALuint *srcframes, ALvoid *dst, ALuint dstframes)
{
    const ALuint SrcFrameSize{static_cast<ALuint>(mChan.size()) * mSrcTypeSize};
    const ALuint DstFrameSize{static_cast<ALuint>(mChan.size()) * mDstTypeSize};
    const ALuint increment{mIncrement};
    auto SamplesIn = static_cast<const al::byte*>(*src);
    ALuint NumSrcSamples{*srcframes};

    FPUCtl mixer_mode{};
    ALuint pos{0};
    while(pos < dstframes && NumSrcSamples > 0)
    {
        ALint prepcount{mSrcPrepCount};
        if(prepcount < 0)
        {
            /* Negative prepcount means we need to skip that many input samples. */
            if(static_cast<ALuint>(-prepcount) >= NumSrcSamples)
            {
                mSrcPrepCount = static_cast<ALint>(NumSrcSamples) + prepcount;
                NumSrcSamples = 0;
                break;
            }
            SamplesIn += SrcFrameSize*static_cast<ALuint>(-prepcount);
            NumSrcSamples -= static_cast<ALuint>(-prepcount);
            mSrcPrepCount = 0;
            continue;
        }
        ALuint toread{minu(NumSrcSamples, BUFFERSIZE - MAX_RESAMPLER_PADDING)};

        if(prepcount < MAX_RESAMPLER_PADDING
            && static_cast<ALuint>(MAX_RESAMPLER_PADDING - prepcount) >= toread)
        {
            /* Not enough input samples to generate an output sample. Store
             * what we're given for later.
             */
            for(size_t chan{0u};chan < mChan.size();chan++)
                LoadSamples(&mChan[chan].PrevSamples[prepcount], SamplesIn + mSrcTypeSize*chan,
                    mChan.size(), mSrcType, toread);

            mSrcPrepCount = prepcount + static_cast<ALint>(toread);
            NumSrcSamples = 0;
            break;
        }

        ALfloat *RESTRICT SrcData{mSrcSamples};
        ALfloat *RESTRICT DstData{mDstSamples};
        ALuint DataPosFrac{mFracOffset};
        auto DataSize64 = static_cast<uint64_t>(prepcount);
        DataSize64 += toread;
        DataSize64 -= MAX_RESAMPLER_PADDING;
        DataSize64 <<= FRACTIONBITS;
        DataSize64 -= DataPosFrac;

        /* If we have a full prep, we can generate at least one sample. */
        auto DstSize = static_cast<ALuint>(
            clampu64((DataSize64 + increment-1)/increment, 1, BUFFERSIZE));
        DstSize = minu(DstSize, dstframes-pos);

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
            ALuint SrcDataEnd{(DstSize*increment + DataPosFrac)>>FRACTIONBITS};
            if(SrcDataEnd >= static_cast<ALuint>(prepcount)+toread)
                std::fill(std::begin(mChan[chan].PrevSamples),
                    std::end(mChan[chan].PrevSamples), 0.0f);
            else
            {
                const size_t len{minz(al::size(mChan[chan].PrevSamples),
                    static_cast<ALuint>(prepcount)+toread-SrcDataEnd)};
                std::copy_n(SrcData+SrcDataEnd, len, mChan[chan].PrevSamples);
                std::fill(std::begin(mChan[chan].PrevSamples)+len,
                    std::end(mChan[chan].PrevSamples), 0.0f);
            }

            /* Now resample, and store the result in the output buffer. */
            const ALfloat *ResampledData{mResample(&mState, SrcData+(MAX_RESAMPLER_PADDING>>1),
                DataPosFrac, increment, {DstData, DstSize})};

            StoreSamples(DstSamples, ResampledData, mChan.size(), mDstType, DstSize);
        }

        /* Update the number of prep samples still available, as well as the
         * fractional offset.
         */
        DataPosFrac += increment*DstSize;
        mSrcPrepCount = mini(prepcount + static_cast<ALint>(toread - (DataPosFrac>>FRACTIONBITS)),
            MAX_RESAMPLER_PADDING);
        mFracOffset = DataPosFrac & FRACTIONMASK;

        /* Update the src and dst pointers in case there's still more to do. */
        SamplesIn += SrcFrameSize*(DataPosFrac>>FRACTIONBITS);
        NumSrcSamples -= minu(NumSrcSamples, (DataPosFrac>>FRACTIONBITS));

        dst = static_cast<al::byte*>(dst) + DstFrameSize*DstSize;
        pos += DstSize;
    }

    *src = SamplesIn;
    *srcframes = NumSrcSamples;

    return pos;
}


void ChannelConverter::convert(const ALvoid *src, ALfloat *dst, ALuint frames) const
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
        LoadSamples(dst, src, 1u, mSrcType, frames * ChannelsFromDevFmt(mSrcChans, 0));
}
