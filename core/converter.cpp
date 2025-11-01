
#include "config.h"

#include "converter.h"

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>

#include "alnumeric.h"
#include "fpu_ctrl.h"
#include "gsl/gsl"


namespace {

constexpr auto MaxPitch = 10u;

static_assert((BufferLineSize-1)/MaxPitch > 0, "MaxPitch is too large for BufferLineSize!");
static_assert((INT_MAX>>MixerFracBits)/MaxPitch > BufferLineSize,
    "MaxPitch and/or BufferLineSize are too large for MixerFracBits!");

template<DevFmtType T> constexpr
auto LoadSample(DevFmtType_t<T> val) noexcept -> f32 = delete;

template<> constexpr auto LoadSample<DevFmtByte>(i8 const val) noexcept -> f32
{ return gsl::narrow_cast<f32>(val) * (1.0f/128.0f); }
template<> constexpr auto LoadSample<DevFmtShort>(i16 const val) noexcept -> f32
{ return gsl::narrow_cast<f32>(val) * (1.0f/32768.0f); }
template<> constexpr auto LoadSample<DevFmtInt>(i32 const val) noexcept -> f32
{ return gsl::narrow_cast<f32>(val) * (1.0f/2147483648.0f); }
template<> constexpr auto LoadSample<DevFmtFloat>(f32 const val) noexcept -> f32
{ return val; }

template<> constexpr auto LoadSample<DevFmtUByte>(u8 const val) noexcept -> f32
{ return LoadSample<DevFmtByte>(gsl::narrow_cast<i8>(val - 128)); }
template<> constexpr auto LoadSample<DevFmtUShort>(u16 const val) noexcept -> f32
{ return LoadSample<DevFmtShort>(gsl::narrow_cast<i16>(val - 32768)); }
template<> constexpr auto LoadSample<DevFmtUInt>(u32 const val) noexcept -> f32
{ return LoadSample<DevFmtInt>(as_signed(val - 2147483648u)); }


template<DevFmtType T>
void LoadSampleArray(std::span<f32> const dst, void const *const src, usize const channel,
    usize const srcstep) noexcept
{
    Expects(channel < srcstep);
    const auto srcspan = std::span{static_cast<const DevFmtType_t<T>*>(src), dst.size()*srcstep};
    auto ssrc = srcspan.begin();
    std::advance(ssrc, channel);
    dst.front() = LoadSample<T>(*ssrc);
    std::ranges::generate(dst | std::views::drop(1), [&ssrc,srcstep]
    {
        std::advance(ssrc, srcstep);
        return LoadSample<T>(*ssrc);
    });
}

void LoadSamples(std::span<f32> const dst, void const *const src, usize const channel,
    usize const srcstep, DevFmtType const srctype) noexcept
{
#define HANDLE_FMT(T)                                                         \
    case T: LoadSampleArray<T>(dst, src, channel, srcstep); break
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
auto StoreSample(f32) noexcept -> DevFmtType_t<T> = delete;

template<> auto StoreSample<DevFmtFloat>(f32 const val) noexcept -> f32
{ return val; }
template<> auto StoreSample<DevFmtInt>(f32 const val) noexcept -> i32
{ return fastf2i(std::clamp(val*2147483648.0f, -2147483648.0f, 2147483520.0f)); }
template<> auto StoreSample<DevFmtShort>(f32 const val) noexcept -> i16
{ return gsl::narrow_cast<i16>(fastf2i(std::clamp(val*32768.0f, -32768.0f, 32767.0f))); }
template<> auto StoreSample<DevFmtByte>(f32 const val) noexcept -> i8
{ return gsl::narrow_cast<i8>(fastf2i(std::clamp(val*128.0f, -128.0f, 127.0f))); }

/* Define unsigned output variations. */
template<> auto StoreSample<DevFmtUInt>(f32 const val) noexcept -> u32
{ return as_unsigned(StoreSample<DevFmtInt>(val)) + 2147483648u; }
template<> auto StoreSample<DevFmtUShort>(f32 const val) noexcept -> u16
{ return gsl::narrow_cast<u16>(StoreSample<DevFmtShort>(val) + 32768); }
template<> auto StoreSample<DevFmtUByte>(f32 const val) noexcept -> u8
{ return gsl::narrow_cast<u8>(StoreSample<DevFmtByte>(val) + 128); }

template<DevFmtType T>
void StoreSampleArray(void *const dst, std::span<f32 const> const src, usize const channel,
    usize const dststep) noexcept
{
    Expects(channel < dststep);
    const auto dstspan = std::span{static_cast<DevFmtType_t<T>*>(dst), src.size()*dststep};
    auto sdst = dstspan.begin();
    std::advance(sdst, channel);
    *sdst = StoreSample<T>(src.front());
    std::ranges::for_each(src | std::views::drop(1), [&sdst,dststep](f32 const in)
    {
        std::advance(sdst, dststep);
        *sdst = StoreSample<T>(in);
    });
}


void StoreSamples(void *const dst, std::span<f32 const> const src, usize const channel,
    usize const dststep, DevFmtType const dsttype) noexcept
{
#define HANDLE_FMT(T)                                                         \
    case T: StoreSampleArray<T>(dst, src, channel, dststep); break
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
void Mono2Stereo(std::span<f32> const dst, void const *const src) noexcept
{
    const auto srcspan = std::span{static_cast<DevFmtType_t<T> const*>(src), dst.size()>>1};
    auto sdst = dst.begin();
    std::ranges::for_each(srcspan, [&sdst](f32 const in)
    { sdst = std::fill_n(sdst, 2, in*0.707106781187f); }, &LoadSample<T>);
}

template<DevFmtType T>
void Multi2Mono(u32 chanmask, usize const step, std::span<f32> const dst, void const *const src)
    noexcept
{
    const auto scale = std::sqrt(1.0f / gsl::narrow_cast<f32>(std::popcount(chanmask)));
    const auto srcspan = std::span{static_cast<DevFmtType_t<T> const*>(src), step*dst.size()};
    std::ranges::fill(dst, 0.0f);
    while(chanmask)
    {
        const auto c = std::countr_zero(chanmask);
        chanmask &= ~(1_u32 << c);

        auto ssrc = srcspan.begin();
        std::advance(ssrc, c);
        dst.front() += LoadSample<T>(*ssrc);
        std::ranges::for_each(dst, [&ssrc,step](f32 &sample)
        {
            std::advance(ssrc, step);
            sample += LoadSample<T>(*ssrc);
        });
    }
    std::ranges::transform(dst, dst.begin(), [scale](f32 const sample) noexcept -> f32
    { return sample * scale; });
}

} // namespace

auto SampleConverter::Create(DevFmtType const srcType, DevFmtType const dstType,
    usize const numchans, u32 const srcRate, u32 const dstRate, Resampler const resampler)
    -> SampleConverterPtr
{
    auto converter = SampleConverterPtr{};
    if(numchans < 1 || srcRate < 1 || dstRate < 1)
        return converter;

    converter = SampleConverterPtr{new(FamCount{numchans}) SampleConverter{numchans}};
    converter->mSrcType = srcType;
    converter->mDstType = dstType;
    converter->mSrcTypeSize = BytesFromDevFmt(srcType);
    converter->mDstTypeSize = BytesFromDevFmt(dstType);

    converter->mSrcPrepCount = MaxResamplerPadding;
    converter->mFracOffset = 0;
    std::ranges::fill(converter->mChan | std::views::transform(&ChanSamples::PrevSamples)
        | std::views::join, 0.0f);

    /* Have to set the mixer FPU mode since that's what the resampler code expects. */
    auto mixer_mode = FPUCtl{};
    const auto step = std::clamp(std::round(srcRate*f64{MixerFracOne}/dstRate), 1.0,
        MaxPitch*f64{MixerFracOne});
    converter->mIncrement = gsl::narrow_cast<u32>(step);
    if(converter->mIncrement == MixerFracOne)
    {
        converter->mResample = [](InterpState const*, std::span<f32 const> const src, u32,
            u32 const, std::span<f32> const dst)
        {
            std::ranges::copy(src | std::views::drop(MaxResamplerEdge)
                | std::views::take(dst.size()), dst.begin());
        };
    }
    else
        converter->mResample = PrepareResampler(resampler, converter->mIncrement,
            &converter->mState);

    return converter;
}

auto SampleConverter::availableOut(u32 const srcframes) const -> u32
{
    if(srcframes < 1)
    {
        /* No output samples if there's no input samples. */
        return 0;
    }

    auto const prepcount = mSrcPrepCount;
    if(prepcount < MaxResamplerPadding && MaxResamplerPadding - prepcount >= srcframes)
    {
        /* Not enough input samples to generate an output sample. */
        return 0;
    }

    auto DataSize64 = u64{prepcount};
    DataSize64 += srcframes;
    DataSize64 -= MaxResamplerPadding;
    DataSize64 <<= MixerFracBits;
    DataSize64 -= mFracOffset;

    /* If we have a full prep, we can generate at least one sample. */
    return gsl::narrow_cast<u32>(std::clamp((DataSize64 + mIncrement-1)/mIncrement, 1_u64,
        u64{std::numeric_limits<i32>::max()}));
}

auto SampleConverter::convert(const void **const src, u32 *const srcframes, void *const dst,
    u32 const dstframes) -> u32
{
    const auto SrcFrameSize = mChan.size() * mSrcTypeSize;
    const auto DstFrameSize = mChan.size() * mDstTypeSize;
    const auto increment = mIncrement;
    auto NumSrcSamples = *srcframes;
    auto SamplesIn = std::span{static_cast<const std::byte*>(*src), NumSrcSamples*SrcFrameSize};
    auto SamplesOut = std::span{static_cast<std::byte*>(dst), dstframes*DstFrameSize};

    const auto mixer_mode = FPUCtl{};
    auto pos = 0_u32;
    while(pos < dstframes && NumSrcSamples > 0)
    {
        const auto prepcount = mSrcPrepCount;
        const auto readable = std::min(NumSrcSamples, u32{BufferLineSize} - prepcount);

        if(prepcount < MaxResamplerPadding && MaxResamplerPadding-prepcount >= readable)
        {
            /* Not enough input samples to generate an output sample. Store
             * what we're given for later.
             */
            for(const auto chan : std::views::iota(0_uz, mChan.size()))
                LoadSamples(std::span{mChan[chan].PrevSamples}.subspan(prepcount, readable),
                    SamplesIn.data(), chan, mChan.size(), mSrcType);

            mSrcPrepCount = prepcount + readable;
            NumSrcSamples = 0;
            break;
        }

        const auto SrcData = std::span<f32>{mSrcSamples};
        const auto DstData = std::span<f32>{mDstSamples};
        const auto DataPosFrac = mFracOffset;
        auto DataSize64 = u64{prepcount};
        DataSize64 += readable;
        DataSize64 -= MaxResamplerPadding;
        DataSize64 <<= MixerFracBits;
        DataSize64 -= DataPosFrac;

        /* If we have a full prep, we can generate at least one sample. */
        auto DstSize = gsl::narrow_cast<u32>(std::clamp((DataSize64 + increment-1)/increment,
            1_u64, u64{BufferLineSize}));
        DstSize = std::min(DstSize, dstframes-pos);

        const auto DataPosEnd = DstSize*increment + DataPosFrac;
        const auto SrcDataEnd = DataPosEnd>>MixerFracBits;

        Expects(prepcount+readable >= SrcDataEnd);
        const auto nextprep = std::min(prepcount+readable-SrcDataEnd, MaxResamplerPadding);

        for(const auto chan : std::views::iota(0_uz, mChan.size()))
        {
            /* Load the previous samples into the source data first, then the
             * new samples from the input buffer.
             */
            std::copy_n(mChan[chan].PrevSamples.cbegin(), prepcount, SrcData.begin());
            LoadSamples(SrcData.subspan(prepcount, readable), SamplesIn.data(), chan, mChan.size(),
                mSrcType);

            /* Store as many prep samples for next time as possible, given the
             * number of output samples being generated.
             */
            auto const previter = std::ranges::copy(SrcData | std::views::drop(SrcDataEnd)
                | std::views::take(nextprep), mChan[chan].PrevSamples.begin()).out;
            std::fill(previter, mChan[chan].PrevSamples.end(), 0.0f);

            /* Now resample, and store the result in the output buffer. */
            mResample(&mState, SrcData, DataPosFrac, increment, DstData.first(DstSize));

            StoreSamples(SamplesOut.data(), DstData.first(DstSize), chan, mChan.size(), mDstType);
        }

        /* Update the number of prep samples still available, as well as the
         * fractional offset.
         */
        mSrcPrepCount = nextprep;
        mFracOffset = DataPosEnd & MixerFracMask;

        /* Update the src and dst pointers in case there's still more to do. */
        auto const srcread = std::min(NumSrcSamples, SrcDataEnd + mSrcPrepCount - prepcount);
        SamplesIn = SamplesIn.subspan(SrcFrameSize*srcread);
        NumSrcSamples -= srcread;

        SamplesOut = SamplesOut.subspan(DstFrameSize*DstSize);
        pos += DstSize;
    }

    *src = SamplesIn.data();
    *srcframes = NumSrcSamples;

    return pos;
}

auto SampleConverter::convertPlanar(void const **const src, u32 *const srcframes,
    void *const *const dst, u32 const dstframes) -> u32
{
    const auto srcs = std::span{src, mChan.size()};
    const auto dsts = std::span{dst, mChan.size()};
    const auto increment = mIncrement;
    auto NumSrcSamples = *srcframes;

    const auto mixer_mode = FPUCtl{};
    auto pos = 0_u32;
    while(pos < dstframes && NumSrcSamples > 0)
    {
        const auto prepcount = mSrcPrepCount;
        const auto readable = std::min(NumSrcSamples, u32{BufferLineSize} - prepcount);

        if(prepcount < MaxResamplerPadding && MaxResamplerPadding-prepcount >= readable)
        {
            /* Not enough input samples to generate an output sample. Store
             * what we're given for later.
             */
            for(const auto chan : std::views::iota(0_uz, mChan.size()))
            {
                auto samples = std::span{static_cast<const std::byte*>(srcs[chan]),
                    NumSrcSamples*usize{mSrcTypeSize}};
                LoadSamples(std::span{mChan[chan].PrevSamples}.subspan(prepcount, readable),
                    samples.data(), 0, 1, mSrcType);
                srcs[chan] = samples.subspan(usize{mSrcTypeSize}*readable).data();
            }

            mSrcPrepCount = prepcount + readable;
            NumSrcSamples = 0;
            break;
        }

        const auto SrcData = std::span{mSrcSamples};
        const auto DstData = std::span{mDstSamples};
        const auto DataPosFrac = mFracOffset;
        auto DataSize64 = u64{prepcount};
        DataSize64 += readable;
        DataSize64 -= MaxResamplerPadding;
        DataSize64 <<= MixerFracBits;
        DataSize64 -= DataPosFrac;

        /* If we have a full prep, we can generate at least one sample. */
        auto DstSize = gsl::narrow_cast<u32>(std::clamp((DataSize64 + increment-1)/increment,
            1_u64, u64{BufferLineSize}));
        DstSize = std::min(DstSize, dstframes-pos);

        const auto DataPosEnd = DstSize*increment + DataPosFrac;
        const auto SrcDataEnd = DataPosEnd>>MixerFracBits;

        Expects(prepcount+readable >= SrcDataEnd);
        const auto nextprep = std::min(prepcount+readable-SrcDataEnd, MaxResamplerPadding);

        for(const auto chan : std::views::iota(0_uz, mChan.size()))
        {
            /* Load the previous samples into the source data first, then the
             * new samples from the input buffer.
             */
            auto srciter = std::copy_n(mChan[chan].PrevSamples.cbegin(),prepcount,SrcData.begin());
            LoadSamples({srciter, readable}, srcs[chan], 0, 1, mSrcType);

            /* Store as many prep samples for next time as possible, given the
             * number of output samples being generated.
             */
            auto const previter = std::ranges::copy(SrcData | std::views::drop(SrcDataEnd)
                | std::views::take(nextprep), mChan[chan].PrevSamples.begin()).out;
            std::fill(previter, mChan[chan].PrevSamples.end(), 0.0f);

            /* Now resample, and store the result in the output buffer. */
            mResample(&mState, SrcData, DataPosFrac, increment, DstData.first(DstSize));

            auto DstSamples = std::span{static_cast<std::byte*>(dsts[chan]),
                usize{mDstTypeSize}*dstframes}.subspan(pos*usize{mDstTypeSize});
            StoreSamples(DstSamples.data(), DstData.first(DstSize), 0, 1, mDstType);
        }

        /* Update the number of prep samples still available, as well as the
         * fractional offset.
         */
        mSrcPrepCount = nextprep;
        mFracOffset = DataPosEnd & MixerFracMask;

        /* Update the src and dst pointers in case there's still more to do. */
        auto const srcread = std::min(NumSrcSamples, SrcDataEnd + mSrcPrepCount - prepcount);
        std::ranges::for_each(srcs, [this,NumSrcSamples,srcread](void const *&srcref)
        {
            auto const srcspan = std::span{static_cast<std::byte const*>(srcref),
                usize{mSrcTypeSize}*NumSrcSamples};
            srcref = srcspan.subspan(usize{mSrcTypeSize}*srcread).data();
        });
        NumSrcSamples -= srcread;

        pos += DstSize;
    }

    *srcframes = NumSrcSamples;

    return pos;
}


void ChannelConverter::convert(void const *const src, f32 *const dst, u32 const frames) const
{
    if(!frames)
        return;
    if(mDstChans == DevFmtMono)
    {
        switch(mSrcType)
        {
#define HANDLE_FMT(T) case T: Multi2Mono<T>(mChanMask, mSrcStep, {dst, frames}, src); break
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
    else if(mChanMask == 0x1 && mDstChans == DevFmtStereo)
    {
        switch(mSrcType)
        {
#define HANDLE_FMT(T) case T: Mono2Stereo<T>({dst, frames*2_uz}, src); break
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
