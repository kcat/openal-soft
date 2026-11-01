
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
#include "zudl.hpp"

#if HAVE_CXXMODULES
import core.device;
#else
#include "core/devfmttraits.hpp"
#endif


namespace {

constexpr auto MaxPitch = 10u;

static_assert((BufferLineSize-1)/MaxPitch > 0, "MaxPitch is too large for BufferLineSize!");
static_assert((INT_MAX>>MixerFracBits)/MaxPitch > BufferLineSize,
    "MaxPitch and/or BufferLineSize are too large for MixerFracBits!");

template<typename T> [[nodiscard]] constexpr
auto LoadSample(T const& val) noexcept -> float
{
    if constexpr(std::same_as<T, float>)
        return val;
    else if constexpr(std::same_as<T, std::int32_t>)
        return gsl::narrow_cast<float>(val) * (1.0f/2147483648.0f);
    else if constexpr(std::same_as<T, std::int16_t>)
        return gsl::narrow_cast<float>(val) * (1.0f/32768.0f);
    else if constexpr(std::same_as<T, std::int8_t>)
        return gsl::narrow_cast<float>(val) * (1.0f/128.0f);
    else if constexpr(std::same_as<T, std::uint32_t>)
        return LoadSample<std::int32_t>(static_cast<std::int32_t>(val - 2147483648u));
    else if constexpr(std::same_as<T, std::uint16_t>)
        return LoadSample<std::int16_t>(gsl::narrow_cast<std::int16_t>(val - 32768));
    else if constexpr(std::same_as<T, std::uint8_t>)
        return LoadSample<std::int8_t>(gsl::narrow_cast<std::int8_t>(val - 128));
    else
    {
        static_assert(al::dependent_false<T>, "Unexpected sample type");
        return {};
    }
}

template<typename T>
void LoadSampleArray(std::span<float> const dst, void const *const src, std::size_t const channel,
    std::size_t const srcstep) noexcept
{
    Expects(channel < srcstep);
    const auto srcspan = std::span{static_cast<T const*>(src), dst.size()*srcstep};
    auto ssrc = srcspan.begin();
    std::advance(ssrc, channel);
    dst.front() = LoadSample<T>(*ssrc);
    std::ranges::generate(dst | std::views::drop(1), [&ssrc,srcstep]
    {
        std::advance(ssrc, srcstep);
        return LoadSample<T>(*ssrc);
    });
}

void LoadSamples(std::span<float> const dst, void const *const src, std::size_t const channel,
    std::size_t const srcstep, DevFmtType const srctype) noexcept
{
#define HANDLE_FMT(T)                                                         \
    case T: LoadSampleArray<DevFmtType_t<T>>(dst, src, channel, srcstep); break
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


template<typename T> [[nodiscard]] constexpr
auto StoreSample(float const val) noexcept -> T
{
    if constexpr(std::same_as<T, float>)
        return val;
    else if constexpr(std::same_as<T, std::int32_t>)
        return std::int32_t{fastf2i(std::clamp(val*2147483648.0f, -2147483648.0f, 2147483520.0f))};
    else if constexpr(std::same_as<T, std::int16_t>)
    {
        auto const tmp = std::clamp(val*32768.0f, -32768.0f, 32767.0f);
        return gsl::narrow_cast<std::int16_t>(fastf2i(tmp));
    }
    else if constexpr(std::same_as<T, std::int8_t>)
        return gsl::narrow_cast<std::int8_t>(fastf2i(std::clamp(val*128.0f, -128.0f, 127.0f)));
    else if constexpr(std::same_as<T, std::uint32_t>)
        return static_cast<std::uint32_t>(StoreSample<std::int32_t>(val)) + 2147483648u;
    else if constexpr(std::same_as<T, std::uint16_t>)
        return gsl::narrow_cast<std::uint16_t>(StoreSample<std::int16_t>(val) + 32768);
    else if constexpr(std::same_as<T, std::uint8_t>)
        return gsl::narrow_cast<std::uint8_t>(StoreSample<std::int8_t>(val) + 128);
    else
    {
        static_assert(al::dependent_false<T>, "Unexpected sample type");
        return {};
    }
}

template<typename T>
void StoreSampleArray(void *const dst, std::span<float const> const src, std::size_t const channel,
    std::size_t const dststep) noexcept
{
    Expects(channel < dststep);
    const auto dstspan = std::span{static_cast<T*>(dst), src.size()*dststep};
    auto sdst = dstspan.begin();
    std::advance(sdst, channel);
    *sdst = StoreSample<T>(src.front());
    std::ranges::for_each(src | std::views::drop(1), [&sdst, dststep](float const in)
    {
        std::advance(sdst, dststep);
        *sdst = StoreSample<T>(in);
    });
}

void StoreSamples(void *const dst, std::span<float const> const src, std::size_t const channel,
    std::size_t const dststep, DevFmtType const dsttype) noexcept
{
#define HANDLE_FMT(T)                                                         \
    case T: StoreSampleArray<DevFmtType_t<T>>(dst, src, channel, dststep); break
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


template<typename T>
void Mono2Stereo(std::span<float> const dst, void const *const src) noexcept
{
    const auto srcspan = std::span{static_cast<T const*>(src), dst.size()>>1};
    auto sdst = dst.begin();
    std::ranges::for_each(srcspan, [&sdst](float const in)
    { sdst = std::fill_n(sdst, 2, in*0.707106781187f); }, &LoadSample<T>);
}

template<typename T>
void Multi2Mono(unsigned chanmask, std::size_t const step, std::span<float> const dst,
    void const *const src) noexcept
{
    const auto scale = std::sqrt(1.0f / gsl::narrow_cast<float>(std::popcount(chanmask)));
    const auto srcspan = std::span{static_cast<T const*>(src), step*dst.size()};
    std::ranges::fill(dst, 0.0f);
    while(chanmask)
    {
        const auto c = std::countr_zero(chanmask);
        chanmask &= ~(1u << c);

        auto ssrc = std::next(srcspan.begin(), c);
        dst.front() += LoadSample<T>(*ssrc);
        std::ranges::for_each(dst | std::views::drop(1), [&ssrc, step](float &sample)
        {
            std::advance(ssrc, step);
            sample += LoadSample<T>(*ssrc);
        });
    }
    std::ranges::transform(dst, dst.begin(), [scale](float const sample) noexcept -> float
    { return sample * scale; });
}

} // namespace

auto SampleConverter::Create(DevFmtType const srcType, DevFmtType const dstType,
    std::size_t const numchans, unsigned const srcRate, unsigned const dstRate,
    Resampler const resampler) -> SampleConverterPtr
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
    const auto step = std::clamp(std::round(srcRate*double{MixerFracOne}/dstRate), 1.0,
        MaxPitch*double{MixerFracOne});
    converter->mIncrement = gsl::narrow_cast<unsigned>(step);
    if(converter->mIncrement == MixerFracOne)
    {
        converter->mResample = [](InterpState const*, std::span<float const> const src, unsigned,
            unsigned const, std::span<float> const dst) noexcept NONBLOCKING
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

auto SampleConverter::availableOut(unsigned const srcframes) const -> unsigned
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

    auto DataSize64 = std::uint64_t{prepcount} + srcframes - MaxResamplerPadding;
    DataSize64 <<= MixerFracBits;
    DataSize64 -= mFracOffset;

    return al::saturating_cast<unsigned>((DataSize64 + mIncrement-1)/mIncrement);
}

auto SampleConverter::convert(const void **const src, unsigned *const srcframes, void *const dst,
    unsigned const dstframes) -> unsigned
{
    const auto SrcFrameSize = mChan.size() * mSrcTypeSize;
    const auto DstFrameSize = mChan.size() * mDstTypeSize;
    const auto increment = mIncrement;
    auto NumSrcSamples = *srcframes;
    auto SamplesIn = std::span{static_cast<const std::byte*>(*src), NumSrcSamples*SrcFrameSize};
    auto SamplesOut = std::span{static_cast<std::byte*>(dst), dstframes*DstFrameSize};

    const auto mixer_mode = FPUCtl{};
    auto pos = 0u;
    while(pos < dstframes && NumSrcSamples > 0)
    {
        const auto prepcount = mSrcPrepCount;
        const auto readable = std::min(NumSrcSamples, unsigned{BufferLineSize} - prepcount);

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

        const auto SrcData = std::span<float>{mSrcSamples};
        const auto DstData = std::span<float>{mDstSamples};
        const auto DataPosFrac = mFracOffset;

        /* If we have a full prep, we can generate at least one sample. */
        auto const DstSize = std::invoke([&] {
            auto DataSize64 = std::uint64_t{prepcount} + readable - MaxResamplerPadding;
            DataSize64 <<= MixerFracBits;
            DataSize64 -= DataPosFrac;
            auto const outcount = (DataSize64 + increment-1)/increment;
            return std::min(al::saturating_cast<unsigned>(outcount), dstframes-pos);
        });

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

auto SampleConverter::convertPlanar(void const **const src, unsigned *const srcframes,
    void *const *const dst, unsigned const dstframes) -> unsigned
{
    const auto srcs = std::span{src, mChan.size()};
    const auto dsts = std::span{dst, mChan.size()};
    const auto increment = mIncrement;
    auto NumSrcSamples = *srcframes;

    const auto mixer_mode = FPUCtl{};
    auto pos = 0u;
    while(pos < dstframes && NumSrcSamples > 0)
    {
        const auto prepcount = mSrcPrepCount;
        const auto readable = std::min(NumSrcSamples, unsigned{BufferLineSize} - prepcount);

        if(prepcount < MaxResamplerPadding && MaxResamplerPadding-prepcount >= readable)
        {
            /* Not enough input samples to generate an output sample. Store
             * what we're given for later.
             */
            for(const auto chan : std::views::iota(0_uz, mChan.size()))
            {
                auto samples = std::span{static_cast<const std::byte*>(srcs[chan]),
                    NumSrcSamples*std::size_t{mSrcTypeSize}};
                LoadSamples(std::span{mChan[chan].PrevSamples}.subspan(prepcount, readable),
                    samples.data(), 0, 1, mSrcType);
                srcs[chan] = samples.subspan(std::size_t{mSrcTypeSize}*readable).data();
            }

            mSrcPrepCount = prepcount + readable;
            NumSrcSamples = 0;
            break;
        }

        const auto SrcData = std::span{mSrcSamples};
        const auto DstData = std::span{mDstSamples};
        const auto DataPosFrac = mFracOffset;

        /* If we have a full prep, we can generate at least one sample. */
        auto const DstSize = std::invoke([&] {
            auto DataSize64 = std::uint64_t{prepcount} + readable - MaxResamplerPadding;
            DataSize64 <<= MixerFracBits;
            DataSize64 -= DataPosFrac;
            auto const outcount = (DataSize64 + increment-1)/increment;
            return std::min(al::saturating_cast<unsigned>(outcount), dstframes-pos);
        });

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
                std::size_t{mDstTypeSize}*dstframes}.subspan(pos*std::size_t{mDstTypeSize});
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
                std::size_t{mSrcTypeSize}*NumSrcSamples};
            srcref = srcspan.subspan(std::size_t{mSrcTypeSize}*srcread).data();
        });
        NumSrcSamples -= srcread;

        pos += DstSize;
    }

    *srcframes = NumSrcSamples;

    return pos;
}


void ChannelConverter::convert(void const *const src, float *const dst, unsigned const frames)
    const
{
    if(!frames)
        return;
    if(mDstChans == DevFmtMono)
    {
        switch(mSrcType)
        {
#define HANDLE_FMT(T) case T:                                                 \
    Multi2Mono<DevFmtType_t<T>>(mChanMask, mSrcStep, {dst, frames}, src); break
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
#define HANDLE_FMT(T) case T: Mono2Stereo<DevFmtType_t<T>>({dst, frames*2_uz}, src); break
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
