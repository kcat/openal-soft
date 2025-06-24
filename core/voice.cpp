
#include "config.h"
#include "config_simd.h"

#include "voice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "alnumeric.h"
#include "alstring.h"
#include "ambidefs.h"
#include "async_event.h"
#include "buffer_storage.h"
#include "context.h"
#include "cpu_caps.h"
#include "devformat.h"
#include "device.h"
#include "filters/biquad.h"
#include "filters/nfc.h"
#include "filters/splitter.h"
#include "fmt_traits.h"
#include "logging.h"
#include "mixer.h"
#include "mixer/defs.h"
#include "mixer/hrtfdefs.h"
#include "opthelpers.h"
#include "resampler_limits.h"
#include "ringbuffer.h"
#include "vector.h"
#include "voice_change.h"

struct CTag;
#if HAVE_SSE
struct SSETag;
#endif
#if HAVE_NEON
struct NEONTag;
#endif


namespace {

static_assert(!(DeviceBase::MixerLineSize&3), "MixerLineSize must be a multiple of 4");
static_assert(!(MaxResamplerEdge&3), "MaxResamplerEdge is not a multiple of 4");

constexpr auto PitchLimit = (std::numeric_limits<int>::max()-MixerFracMask) / MixerFracOne
    / BufferLineSize;
static_assert(MaxPitch <= PitchLimit, "MaxPitch, BufferLineSize, or MixerFracBits is too large");
static_assert(BufferLineSize > MaxPitch, "MaxPitch must be less then BufferLineSize");


using uint = unsigned int;
using namespace std::chrono;
using namespace std::string_view_literals;

using HrtfMixerFunc = void(*)(const std::span<const float> InSamples,
    const std::span<float2> AccumSamples, const uint IrSize, const MixHrtfFilter *hrtfparams,
    const size_t SamplesToDo);
using HrtfMixerBlendFunc = void(*)(const std::span<const float> InSamples,
    const std::span<float2> AccumSamples, const uint IrSize, const HrtfFilter *oldparams,
    const MixHrtfFilter *newparams, const size_t SamplesToDo);

auto MixHrtfSamples = HrtfMixerFunc{MixHrtf_<CTag>};
auto MixHrtfBlendSamples = HrtfMixerBlendFunc{MixHrtfBlend_<CTag>};

inline auto SelectMixer() -> MixerOutFunc
{
#if HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return Mix_<NEONTag>;
#endif
#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return Mix_<SSETag>;
#endif
    return Mix_<CTag>;
}

inline auto SelectMixerOne() -> MixerOneFunc
{
#if HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return Mix_<NEONTag>;
#endif
#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return Mix_<SSETag>;
#endif
    return Mix_<CTag>;
}

inline auto SelectHrtfMixer() -> HrtfMixerFunc
{
#if HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtf_<NEONTag>;
#endif
#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtf_<SSETag>;
#endif
    return MixHrtf_<CTag>;
}

inline auto SelectHrtfBlendMixer() -> HrtfMixerBlendFunc
{
#if HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtfBlend_<NEONTag>;
#endif
#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtfBlend_<SSETag>;
#endif
    return MixHrtfBlend_<CTag>;
}

} // namespace

void Voice::InitMixer(std::optional<std::string> resopt)
{
    if(resopt)
    {
        struct ResamplerEntry {
            const std::string_view name;
            const Resampler resampler;
        };
        constexpr std::array ResamplerList{
            ResamplerEntry{"none"sv, Resampler::Point},
            ResamplerEntry{"point"sv, Resampler::Point},
            ResamplerEntry{"linear"sv, Resampler::Linear},
            ResamplerEntry{"spline"sv, Resampler::Spline},
            ResamplerEntry{"gaussian"sv, Resampler::Gaussian},
            ResamplerEntry{"bsinc12"sv, Resampler::BSinc12},
            ResamplerEntry{"fast_bsinc12"sv, Resampler::FastBSinc12},
            ResamplerEntry{"bsinc24"sv, Resampler::BSinc24},
            ResamplerEntry{"fast_bsinc24"sv, Resampler::FastBSinc24},
            ResamplerEntry{"bsinc48"sv, Resampler::BSinc48},
            ResamplerEntry{"fast_bsinc48"sv, Resampler::FastBSinc48},
        };

        auto resampler = std::string_view{*resopt};
		
        if (al::case_compare(resampler, "cubic"sv) == 0)
        {
            WARN("Resampler option \"{}\" is deprecated, using spline", *resopt);
            resampler = "spline"sv;
        }
        else if(al::case_compare(resampler, "sinc4"sv) == 0
            || al::case_compare(resampler, "sinc8"sv) == 0)
        {
            WARN("Resampler option \"{}\" is deprecated, using gaussian", *resopt);
            resampler = "gaussian"sv;
        }
        else if(al::case_compare(resampler, "bsinc"sv) == 0)
        {
            WARN("Resampler option \"{}\" is deprecated, using bsinc12", *resopt);
            resampler = "bsinc12"sv;
        }

        auto iter = std::find_if(ResamplerList.begin(), ResamplerList.end(),
            [resampler](const ResamplerEntry &entry) -> bool
            { return al::case_compare(resampler, entry.name) == 0; });
        if(iter == ResamplerList.end())
            ERR("Invalid resampler: {}", *resopt);
        else
            ResamplerDefault = iter->resampler;
    }

    MixSamplesOut = SelectMixer();
    MixSamplesOne = SelectMixerOne();
    MixHrtfBlendSamples = SelectHrtfBlendMixer();
    MixHrtfSamples = SelectHrtfMixer();
}


namespace {

/* IMA ADPCM Stepsize table */
constexpr auto IMAStep_size = std::array{
       7,    8,    9,   10,   11,   12,   13,   14,   16,   17,   19,
      21,   23,   25,   28,   31,   34,   37,   41,   45,   50,   55,
      60,   66,   73,   80,   88,   97,  107,  118,  130,  143,  157,
     173,  190,  209,  230,  253,  279,  307,  337,  371,  408,  449,
     494,  544,  598,  658,  724,  796,  876,  963, 1060, 1166, 1282,
    1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,10442,
   11487,12635,13899,15289,16818,18500,20350,22358,24633,27086,29794,
   32767
};

/* IMA4 ADPCM Codeword decode table */
constexpr auto IMA4Codeword = std::array{
    1, 3, 5, 7, 9, 11, 13, 15,
   -1,-3,-5,-7,-9,-11,-13,-15,
};

/* IMA4 ADPCM Step index adjust decode table */
constexpr auto IMA4Index_adjust = std::array{
   -1,-1,-1,-1, 2, 4, 6, 8,
   -1,-1,-1,-1, 2, 4, 6, 8
};

/* MSADPCM Adaption table */
constexpr auto MSADPCMAdaption = std::array{
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};

/* MSADPCM Adaption Coefficient tables */
constexpr auto MSADPCMAdaptionCoeff = std::array{
    std::array{256,    0},
    std::array{512, -256},
    std::array{  0,    0},
    std::array{192,   64},
    std::array{240,    0},
    std::array{460, -208},
    std::array{392, -232}
};


void SendSourceStoppedEvent(ContextBase *context, uint id)
{
    auto *ring = context->mAsyncEvents.get();
    auto evt_vec = ring->getWriteVector();
    if(evt_vec[0].empty()) return;

    auto &evt = InitAsyncEvent<AsyncSourceStateEvent>(evt_vec[0].front());
    evt.mId = id;
    evt.mState = AsyncSrcState::Stop;

    ring->writeAdvance(1);
}


auto DoFilters(BiquadFilter &lpfilter, BiquadFilter &hpfilter,
    const std::span<float,BufferLineSize> dst, const std::span<const float> src, int type)
    -> std::span<const float>
{
    switch(type)
    {
    case AF_None:
        lpfilter.clear();
        hpfilter.clear();
        break;

    case AF_LowPass:
        lpfilter.process(src, dst);
        hpfilter.clear();
        return dst.first(src.size());
    case AF_HighPass:
        lpfilter.clear();
        hpfilter.process(src, dst);
        return dst.first(src.size());

    case AF_BandPass:
        DualBiquad{lpfilter, hpfilter}.process(src, dst);
        return dst.first(src.size());
    }
    return src;
}


template<typename T>
inline void LoadSamples(const std::span<float> dstSamples,
    const std::span<const T> srcData, const size_t srcChan, const size_t srcOffset,
    const size_t srcStep, const size_t samplesPerBlock [[maybe_unused]]) noexcept
{
    using TypeTraits = SampleInfo<T>;
    assert(srcChan < srcStep);

    auto ssrc = srcData.begin();
    std::advance(ssrc, srcOffset*srcStep + srcChan);
    dstSamples.front() = TypeTraits::to_float(*ssrc);
    std::ranges::generate(dstSamples | std::views::drop(1), [&ssrc,srcStep]
    {
        std::advance(ssrc, srcStep);
        return TypeTraits::to_float(*ssrc);
    });
}

template<>
inline void LoadSamples<IMA4Data>(std::span<float> dstSamples, std::span<const IMA4Data> src,
    const size_t srcChan, const size_t srcOffset, const size_t srcStep,
    const size_t samplesPerBlock) noexcept
{
    static constexpr auto MaxStepIndex = static_cast<int>(IMAStep_size.size()) - 1;

    assert(srcStep > 0 || srcStep <= 2);
    assert(srcChan < srcStep);
    assert(samplesPerBlock > 1);
    const size_t blockBytes{((samplesPerBlock-1)/2 + 4)*srcStep};

    /* Skip to the ADPCM block containing the srcOffset sample. */
    src = src.subspan(srcOffset/samplesPerBlock*blockBytes);
    /* Calculate how many samples need to be skipped in the block. */
    auto skip = srcOffset % samplesPerBlock;

    /* NOTE: This could probably be optimized better. */
    while(!dstSamples.empty())
    {
        /* Each IMA4 block starts with a signed 16-bit sample, and a signed(?)
         * 16-bit table index. The table index needs to be clamped.
         */
        auto prevSample = std::to_integer<uint8_t>(src[srcChan*4 + 0].value)
            | (std::to_integer<uint8_t>(src[srcChan*4 + 1].value) << 8);
        auto prevIndex = std::to_integer<uint8_t>(src[srcChan*4 + 2].value)
            | (std::to_integer<uint8_t>(src[srcChan*4 + 3].value) << 8);
        const auto nibbleData = src.subspan((srcStep+srcChan)*4);
        src = src.subspan(blockBytes);

        /* Sign-extend the 16-bit sample and index values. */
        prevSample = (prevSample^0x8000) - 32768;
        prevIndex = std::clamp((prevIndex^0x8000) - 32768, 0, MaxStepIndex);

        if(skip == 0)
        {
            dstSamples[0] = static_cast<float>(prevSample) / 32768.0f;
            dstSamples = dstSamples.subspan(1);
            if(dstSamples.empty()) return;
        }
        else
            --skip;

        /* The rest of the block is arranged as a series of nibbles, contained
         * in 4 *bytes* per channel interleaved. So every 8 nibbles we need to
         * skip 4 bytes per channel to get the next nibbles for this channel.
         */
        auto decode_nibble = [&prevSample,&prevIndex,srcStep,nibbleData](const size_t nibbleOffset)
            noexcept -> int
        {
            static constexpr auto NibbleMask = std::byte{0xf};
            const auto byteShift = (nibbleOffset&1) * 4;
            const auto wordOffset = (nibbleOffset>>1) & ~3_uz;
            const auto byteOffset = wordOffset*srcStep + ((nibbleOffset>>1)&3);

            const auto byteval = nibbleData[byteOffset].value;
            const auto nibble = std::to_integer<uint8_t>((byteval>>byteShift)&NibbleMask);

            prevSample += IMA4Codeword[nibble] * IMAStep_size[static_cast<uint>(prevIndex)] / 8;
            prevSample = std::clamp(prevSample, -32768, 32767);

            prevIndex += IMA4Index_adjust[nibble];
            prevIndex = std::clamp(prevIndex, 0, MaxStepIndex);

            return prevSample;
        };

        /* First, decode the samples that we need to skip in the block (will
         * always be less than the block size). They need to be decoded despite
         * being ignored for proper state on the remaining samples.
         */
        auto nibbleOffset = 0_uz;
        const auto startOffset = skip + 1_uz;
        for(;skip;--skip)
        {
            std::ignore = decode_nibble(nibbleOffset);
            ++nibbleOffset;
        }

        /* Second, decode the rest of the block and write to the output, until
         * the end of the block or the end of output.
         */
        const auto dst = std::ranges::generate(dstSamples
            | std::views::take(samplesPerBlock - startOffset), [&]
        {
            const auto sample = decode_nibble(nibbleOffset);
            ++nibbleOffset;

            return static_cast<float>(sample) / 32768.0f;
        });
        dstSamples = std::span{dst, dstSamples.end()};
    }
}

template<>
inline void LoadSamples<MSADPCMData>(std::span<float> dstSamples, std::span<const MSADPCMData> src,
    const size_t srcChan, const size_t srcOffset, const size_t srcStep,
    const size_t samplesPerBlock) noexcept
{
    assert(srcStep > 0 || srcStep <= 2);
    assert(srcChan < srcStep);
    assert(samplesPerBlock > 2);
    const auto blockBytes = ((samplesPerBlock-2_uz)/2_uz + 7_uz)*srcStep;

    src = src.subspan(srcOffset/samplesPerBlock*blockBytes);
    auto skip = srcOffset % samplesPerBlock;

    while(!dstSamples.empty())
    {
        /* Each MS ADPCM block starts with an 8-bit block predictor, used to
         * dictate how the two sample history values are mixed with the decoded
         * sample, and an initial signed 16-bit scaling value which scales the
         * nibble sample value. This is followed by the two initial 16-bit
         * sample history values.
         */
        const auto blockpred = std::min(std::to_integer<uint8_t>(src[srcChan].value),
            uint8_t{MSADPCMAdaptionCoeff.size()-1});
        auto scale = std::to_integer<uint8_t>(src[srcStep + 2*srcChan + 0].value)
            | (std::to_integer<uint8_t>(src[srcStep + 2*srcChan + 1].value) << 8);

        auto sampleHistory = std::array{
            std::to_integer<int>(src[3*srcStep + 2*srcChan + 0].value)
                | (std::to_integer<int>(src[3*srcStep + 2*srcChan + 1].value)<<8),
            std::to_integer<int>(src[5*srcStep + 2*srcChan + 0].value)
                | (std::to_integer<int>(src[5*srcStep + 2*srcChan + 1].value)<<8)};
        const auto nibbleData = src.subspan(7*srcStep);
        src = src.subspan(blockBytes);

        const auto coeffs = std::span{MSADPCMAdaptionCoeff[blockpred]};
        scale = (scale^0x8000) - 32768;
        sampleHistory[0] = (sampleHistory[0]^0x8000) - 32768;
        sampleHistory[1] = (sampleHistory[1]^0x8000) - 32768;

        /* The second history sample is "older", so it's the first to be
         * written out.
         */
        if(skip == 0)
        {
            dstSamples[0] = static_cast<float>(sampleHistory[1]) / 32768.0f;
            if(dstSamples.size() < 2) return;
            dstSamples[1] = static_cast<float>(sampleHistory[0]) / 32768.0f;
            dstSamples = dstSamples.subspan(2);
            if(dstSamples.empty()) return;
        }
        else if(skip == 1)
        {
            --skip;
            dstSamples[0] = static_cast<float>(sampleHistory[0]) / 32768.0f;
            dstSamples = dstSamples.subspan(1);
            if(dstSamples.empty()) return;
        }
        else
            skip -= 2;

        /* The rest of the block is a series of nibbles, interleaved per-
         * channel.
         */
        auto decode_nibble = [&sampleHistory,&scale,coeffs,nibbleData](const size_t nibbleOffset)
            noexcept -> int
        {
            static constexpr auto NibbleMask = std::byte{0xf};
            const auto byteOffset = nibbleOffset>>1;
            const auto byteShift = ((nibbleOffset&1)^1) * 4;

            const auto byteval = nibbleData[byteOffset].value;
            const auto nibble = std::to_integer<uint8_t>((byteval>>byteShift)&NibbleMask);

            const auto pred = ((nibble^0x08) - 0x08) * scale;
            const auto diff = (sampleHistory[0]*coeffs[0] + sampleHistory[1]*coeffs[1]) / 256;
            const auto sample = std::clamp(pred + diff, -32768, 32767);

            sampleHistory[1] = sampleHistory[0];
            sampleHistory[0] = sample;

            scale = MSADPCMAdaption[nibble] * scale / 256;
            scale = std::max(16, scale);

            return sample;
        };

        /* First, skip samples. */
        const auto startOffset = skip + 2_uz;
        auto nibbleOffset = srcChan;
        for(;skip;--skip)
        {
            std::ignore = decode_nibble(nibbleOffset);
            nibbleOffset += srcStep;
        }

        /* Now decode the rest of the block, until the end of the block or the
         * dst buffer is filled.
         */
        const auto dst = std::ranges::generate(dstSamples
            | std::views::take(samplesPerBlock - startOffset), [&]
        {
            const auto sample = decode_nibble(nibbleOffset);
            nibbleOffset += srcStep;

            return static_cast<float>(sample) / 32768.0f;
        });
        dstSamples = std::span{dst, dstSamples.end()};
    }
}

void LoadSamples(const std::span<float> dstSamples, const SampleVariant &src,
    const size_t srcChan, const size_t srcOffset, const size_t srcStep,
    const size_t samplesPerBlock) noexcept
{
    std::visit([&](auto&& splvec)
    {
        using sample_t = std::remove_cvref_t<decltype(splvec)>::value_type;
        LoadSamples<sample_t>(dstSamples, splvec, srcChan, srcOffset, srcStep, samplesPerBlock);
    }, src);
}

void LoadBufferStatic(VoiceBufferItem *buffer, VoiceBufferItem *bufferLoopItem,
    const size_t dataPosInt, const size_t srcChannel, const size_t srcStep,
    std::span<float> voiceSamples)
{
    if(!bufferLoopItem)
    {
        auto lastSample = 0.0f;
        /* Load what's left to play from the buffer */
        if(buffer->mSampleLen > dataPosInt) [[likely]]
        {
            const auto buffer_remaining = buffer->mSampleLen - dataPosInt;
            const auto remaining = std::min(voiceSamples.size(), buffer_remaining);
            LoadSamples(voiceSamples.first(remaining), buffer->mSamples, srcChannel, dataPosInt,
                srcStep, buffer->mBlockAlign);
            lastSample = voiceSamples[remaining-1];
            voiceSamples = voiceSamples.subspan(remaining);
        }

        std::ranges::fill(voiceSamples, lastSample);
    }
    else
    {
        const auto loopStart = size_t{buffer->mLoopStart};
        const auto loopEnd = size_t{buffer->mLoopEnd};
        ASSUME(loopEnd > loopStart);

        const auto intPos = (dataPosInt < loopEnd) ? dataPosInt
            : (((dataPosInt-loopStart)%(loopEnd-loopStart)) + loopStart);

        /* Load what's left of this loop iteration */
        const auto remaining = std::min(voiceSamples.size(), loopEnd-dataPosInt);
        LoadSamples(voiceSamples.first(remaining), buffer->mSamples, srcChannel, intPos, srcStep,
            buffer->mBlockAlign);
        voiceSamples = voiceSamples.subspan(remaining);

        /* Load repeats of the loop to fill the buffer. */
        const auto loopSize = loopEnd - loopStart;
        while(const auto toFill = std::min(voiceSamples.size(), loopSize))
        {
            LoadSamples(voiceSamples.first(toFill), buffer->mSamples, srcChannel, loopStart,
                srcStep, buffer->mBlockAlign);
            voiceSamples = voiceSamples.subspan(toFill);
        }
    }
}

void LoadBufferCallback(VoiceBufferItem *buffer, const size_t dataPosInt,
    const size_t numCallbackSamples, const size_t srcChannel, const size_t srcStep,
    std::span<float> voiceSamples)
{
    auto lastSample = 0.0f;
    if(numCallbackSamples > dataPosInt) [[likely]]
    {
        const auto remaining = std::min(voiceSamples.size(), numCallbackSamples-dataPosInt);
        LoadSamples(voiceSamples.first(remaining), buffer->mSamples, srcChannel, dataPosInt,
            srcStep, buffer->mBlockAlign);
        lastSample = voiceSamples[remaining-1];
        voiceSamples = voiceSamples.subspan(remaining);
    }

    std::ranges::fill(voiceSamples, lastSample);
}

void LoadBufferQueue(VoiceBufferItem *buffer, VoiceBufferItem *bufferLoopItem,
    size_t dataPosInt, const size_t srcChannel, const size_t srcStep,
    std::span<float> voiceSamples)
{
    auto lastSample = 0.0f;
    /* Crawl the buffer queue to fill in the temp buffer */
    while(buffer && !voiceSamples.empty())
    {
        if(dataPosInt >= buffer->mSampleLen)
        {
            dataPosInt -= buffer->mSampleLen;
            buffer = buffer->mNext.load(std::memory_order_acquire);
            if(!buffer) buffer = bufferLoopItem;
            continue;
        }

        const auto remaining = std::min(voiceSamples.size(), buffer->mSampleLen-dataPosInt);
        LoadSamples(voiceSamples.first(remaining), buffer->mSamples, srcChannel, dataPosInt,
            srcStep, buffer->mBlockAlign);

        lastSample = voiceSamples[remaining-1];
        voiceSamples = voiceSamples.subspan(remaining);
        if(voiceSamples.empty())
            break;

        dataPosInt = 0;
        buffer = buffer->mNext.load(std::memory_order_acquire);
        if(!buffer) buffer = bufferLoopItem;
    }

    std::ranges::fill(voiceSamples, lastSample);
}


void DoHrtfMix(const std::span<const float> samples, DirectParams &parms, const float TargetGain,
    const size_t Counter, size_t OutPos, const bool IsPlaying, DeviceBase *Device)
{
    const auto IrSize = Device->mIrSize;
    const auto HrtfSamples = std::span{Device->ExtraSampleData};
    const auto AccumSamples = std::span{Device->HrtfAccumData};

    /* Copy the HRTF history and new input samples into a temp buffer. */
    auto src_iter = std::ranges::copy(parms.Hrtf.History, HrtfSamples.begin()).out;
    std::ranges::copy(samples, src_iter);
    /* Copy the last used samples back into the history buffer for later. */
    if(IsPlaying) [[likely]]
    {
        const auto endsamples = HrtfSamples.subspan(samples.size(), parms.Hrtf.History.size());
        std::ranges::copy(endsamples, parms.Hrtf.History.begin());
    }

    /* If fading and this is the first mixing pass, fade between the IRs. */
    auto fademix = 0_uz;
    if(Counter && OutPos == 0)
    {
        fademix = std::min(samples.size(), Counter);

        auto gain = TargetGain;

        /* The new coefficients need to fade in completely since they're
         * replacing the old ones. To keep the gain fading consistent,
         * interpolate between the old and new target gains given how much of
         * the fade time this mix handles.
         */
        if(Counter > fademix)
        {
            const auto a = static_cast<float>(fademix) / static_cast<float>(Counter);
            gain = lerpf(parms.Hrtf.Old.Gain, TargetGain, a);
        }

        const auto hrtfparams = MixHrtfFilter{
            parms.Hrtf.Target.Coeffs,
            parms.Hrtf.Target.Delay,
            0.0f, gain / static_cast<float>(fademix)};
        MixHrtfBlendSamples(HrtfSamples, AccumSamples.subspan(OutPos), IrSize, &parms.Hrtf.Old,
            &hrtfparams, fademix);

        /* Update the old parameters with the result. */
        parms.Hrtf.Old = parms.Hrtf.Target;
        parms.Hrtf.Old.Gain = gain;
        OutPos += fademix;
    }

    if(fademix < samples.size())
    {
        const auto todo = samples.size() - fademix;
        auto gain{TargetGain};

        /* Interpolate the target gain if the gain fading lasts longer than
         * this mix.
         */
        if(Counter > samples.size())
        {
            const auto a = static_cast<float>(todo) / static_cast<float>(Counter-fademix);
            gain = lerpf(parms.Hrtf.Old.Gain, TargetGain, a);
        }

        const auto hrtfparams = MixHrtfFilter{
            parms.Hrtf.Target.Coeffs,
            parms.Hrtf.Target.Delay,
            parms.Hrtf.Old.Gain,
            (gain - parms.Hrtf.Old.Gain) / static_cast<float>(todo)};
        MixHrtfSamples(HrtfSamples.subspan(fademix), AccumSamples.subspan(OutPos), IrSize,
            &hrtfparams, todo);

        /* Store the now-current gain for next time. */
        parms.Hrtf.Old.Gain = gain;
    }
}

void DoNfcMix(const std::span<const float> samples, std::span<FloatBufferLine> OutBuffer,
    DirectParams &parms, const std::span<const float,MaxOutputChannels> OutGains,
    const uint Counter, const uint OutPos, DeviceBase *Device)
{
    using FilterProc = void(NfcFilter::*)(const std::span<const float>, const std::span<float>);
    static constexpr auto NfcProcess = std::array{FilterProc{nullptr}, &NfcFilter::process1,
        &NfcFilter::process2, &NfcFilter::process3, &NfcFilter::process4};
    static_assert(NfcProcess.size() == MaxAmbiOrder+1);

    MixSamples(samples, std::span{OutBuffer[0]}.subspan(OutPos), parms.Gains.Current[0],
        OutGains[0], Counter);
    OutBuffer = OutBuffer.subspan(1);
    auto CurrentGains = std::span{parms.Gains.Current}.subspan(1);
    auto TargetGains = OutGains.subspan(1);

    const auto nfcsamples = std::span{Device->ExtraSampleData}.first(samples.size());
    auto order = 1_uz;
    while(const auto chancount = size_t{Device->NumChannelsPerOrder[order]})
    {
        (parms.NFCtrlFilter.*NfcProcess[order])(samples, nfcsamples);
        MixSamples(nfcsamples, OutBuffer.first(chancount), CurrentGains, TargetGains, Counter,
            OutPos);
        if(++order == MaxAmbiOrder+1)
            break;
        OutBuffer = OutBuffer.subspan(chancount);
        CurrentGains = CurrentGains.subspan(chancount);
        TargetGains = TargetGains.subspan(chancount);
    }
}

} // namespace

void Voice::mix(const State vstate, ContextBase *Context, const nanoseconds deviceTime,
    const uint SamplesToDo)
{
    static constexpr std::array<float,MaxOutputChannels> SilentTarget{};

    ASSUME(SamplesToDo > 0);

    auto *Device = Context->mDevice;
    const auto NumSends = Device->NumAuxSends;

    /* Get voice info */
    auto DataPosInt = mPosition.load(std::memory_order_relaxed);
    auto DataPosFrac = mPositionFrac.load(std::memory_order_relaxed);
    auto *BufferListItem = mCurrentBuffer.load(std::memory_order_relaxed);
    auto *BufferLoopItem = mLoopBuffer.load(std::memory_order_relaxed);
    const auto increment = mStep;
    if(increment < 1) [[unlikely]]
    {
        /* If the voice is supposed to be stopping but can't be mixed, just
         * stop it before bailing.
         */
        if(vstate == Stopping)
            mPlayState.store(Stopped, std::memory_order_release);
        return;
    }

    /* If the static voice's current position is beyond the buffer loop end
     * position, disable looping.
     */
    if(mFlags.test(VoiceIsStatic) && BufferLoopItem)
    {
        if(std::cmp_greater_equal(DataPosInt, BufferListItem->mLoopEnd))
            BufferLoopItem = nullptr;
    }

    auto OutPos = 0u;

    /* Check if we're doing a delayed start, and we start in this update. */
    if(mStartTime > deviceTime) [[unlikely]]
    {
        /* If the voice is supposed to be stopping but hasn't actually started
         * yet, make sure its stopped.
         */
        if(vstate == Stopping)
        {
            mPlayState.store(Stopped, std::memory_order_release);
            return;
        }

        /* If the start time is too far ahead, don't bother. */
        auto diff = mStartTime - deviceTime;
        if(diff >= seconds{1})
            return;

        /* Get the number of samples ahead of the current time that output
         * should start at. Skip this update if it's beyond the output sample
         * count.
         */
        OutPos = static_cast<uint>(round<seconds>(diff * Device->mSampleRate).count());
        if(OutPos >= SamplesToDo) return;
    }

    /* Calculate the number of samples to mix, and the number of (resampled)
     * samples that need to be loaded (mixing samples and decoder padding).
     */
    const auto samplesToMix = SamplesToDo - OutPos;
    const auto samplesToLoad = samplesToMix + mDecoderPadding;

    /* Get a span of pointers to hold the floating point, deinterlaced,
     * resampled buffer data to be mixed.
     */
    auto SamplePointers = std::array<std::span<float>,DeviceBase::MixerChannelsMax>{};
    const auto MixingSamples = std::span{SamplePointers}
        .first((mFmtChannels == FmtMono && !mDuplicateMono) ? 1_uz : mChans.size());
    {
        const auto channelStep = (samplesToLoad+3u)&~3u;
        auto base = Device->mSampleData.end() - MixingSamples.size()*channelStep;
        std::ranges::generate(MixingSamples, [&base,samplesToLoad,channelStep]
        {
            const auto ret = base;
            std::advance(base, channelStep);
            return std::span{ret, samplesToLoad};
        });
    }

    /* UHJ2 and SuperStereo only have 2 buffer channels, but 3 mixing channels
     * (3rd channel is generated from decoding).
     */
    const auto realChannels = (mFmtChannels == FmtMono) ? 1_uz
        : (mFmtChannels == FmtUHJ2 || mFmtChannels == FmtSuperStereo) ? 2_uz
        : MixingSamples.size();
    for(const auto chan : std::views::iota(0_uz, realChannels))
    {
        static constexpr auto ResBufSize = std::tuple_size_v<decltype(DeviceBase::mResampleData)>;
        static constexpr auto SrcSizeMax = uint{ResBufSize - MaxResamplerEdge};

        const auto prevSamples = std::span{mPrevSamples[chan]};
        std::ranges::copy(prevSamples, Device->mResampleData.begin());
        const auto resampleBuffer = std::span{Device->mResampleData}.subspan<MaxResamplerEdge>();
        auto cbOffset = mCallbackBlockOffset;
        auto intPos = DataPosInt;
        auto fracPos = DataPosFrac;

        /* Load samples for this channel from the available buffer(s), with
         * resampling.
         */
        for(auto samplesLoaded = 0u;samplesLoaded < samplesToLoad;)
        {
            /* Calculate the number of dst samples that can be loaded this
             * iteration, given the available resampler buffer size, and the
             * number of src samples that are needed to load it.
             */
            const auto [dstBufferSize, srcBufferSize] = std::invoke(
                [fracPos,increment,dstRemaining = samplesToLoad-samplesLoaded]() noexcept
                -> std::array<uint,2>
            {
                /* If ext=true, calculate the last written dst pos from the dst
                 * count, convert to the last read src pos, then add one to get
                 * the src count.
                 *
                 * If ext=false, convert the dst count to src count directly.
                 *
                 * Without this, the src count could be short by one when
                 * increment < 1.0, or not have a full src at the end when
                 * increment > 1.0.
                 */
                const auto ext = increment <= MixerFracOne;
                auto dataSize64 = uint64_t{dstRemaining - ext};
                dataSize64 = (dataSize64*increment + fracPos) >> MixerFracBits;
                /* Also include resampler padding. */
                dataSize64 += ext + MaxResamplerEdge;

                if(dataSize64 <= SrcSizeMax)
                    return std::array{dstRemaining, static_cast<uint>(dataSize64)};

                /* If the source size got saturated, we can't fill the desired
                 * dst size. Figure out how many dst samples we can fill.
                 */
                dataSize64 = SrcSizeMax - MaxResamplerEdge;
                dataSize64 = ((dataSize64<<MixerFracBits) - fracPos) / increment;
                if(dataSize64 < dstRemaining)
                {
                    /* Some resamplers require the destination being 16-byte
                     * aligned, so limit to a multiple of 4 samples to maintain
                     * alignment if we need to do another iteration after this.
                     */
                    return std::array{static_cast<uint>(dataSize64)&~3u, SrcSizeMax};
                }
                return std::array{dstRemaining, SrcSizeMax};
            });

            auto srcSampleDelay = 0_uz;
            if(intPos < 0) [[unlikely]]
            {
                /* If the current position is negative, there's that many
                 * silent samples to load before using the buffer.
                 */
                srcSampleDelay = static_cast<uint>(-intPos);
                if(srcSampleDelay >= srcBufferSize)
                {
                    /* If the number of silent source samples exceeds the
                     * number to load, the output will be silent.
                     */
                    std::ranges::fill(MixingSamples[chan].subspan(samplesLoaded, dstBufferSize),
                        0.0f);
                    std::ranges::fill(resampleBuffer.first(srcBufferSize), 0.0f);
                    goto skip_resample;
                }

                std::ranges::fill(resampleBuffer | std::views::take(srcSampleDelay), 0.0f);
            }

            /* Load the necessary samples from the given buffer(s). */
            if(!BufferListItem) [[unlikely]]
            {
                const auto avail = std::min(srcBufferSize, MaxResamplerEdge);
                const auto tofill = std::max(srcBufferSize, MaxResamplerEdge);
                const auto srcbuf = resampleBuffer.first(tofill);

                /* When loading from a voice that ended prematurely, only take
                 * the samples that get closest to 0 amplitude. This helps
                 * certain sounds fade out better.
                 */
                const auto srciter = std::ranges::min_element(srcbuf.begin(),
                    std::next(srcbuf.begin(), ptrdiff_t(avail)), {},
                    [](const float s) { return std::abs(s); });

                std::ranges::fill(std::next(srciter), srcbuf.end(), *srciter);
            }
            else if(mFlags.test(VoiceIsStatic))
            {
                const auto uintPos = static_cast<uint>(std::max(intPos, 0));
                const auto bufferSamples = resampleBuffer.first(srcBufferSize)
                    .subspan(srcSampleDelay);
                LoadBufferStatic(BufferListItem, BufferLoopItem, uintPos, chan, mFrameStep,
                    bufferSamples);
            }
            else if(mFlags.test(VoiceIsCallback))
            {
                const auto bufferOffset = size_t{cbOffset};
                const auto needSamples = bufferOffset + srcBufferSize - srcSampleDelay;
                const auto needBlocks = (needSamples + mSamplesPerBlock-1) / mSamplesPerBlock;
                if(!mFlags.test(VoiceCallbackStopped) && needBlocks > mNumCallbackBlocks)
                {
                    const auto byteOffset = mNumCallbackBlocks*size_t{mBytesPerBlock};
                    const auto needBytes = (needBlocks-mNumCallbackBlocks)*size_t{mBytesPerBlock};

                    const auto samples = std::visit([](auto &splspan)
                    { return std::as_writable_bytes(splspan); }, BufferListItem->mSamples);

                    const auto gotBytes = BufferListItem->mCallback(BufferListItem->mUserData,
                        &samples[byteOffset], static_cast<int>(needBytes));
                    if(gotBytes < 0)
                        mFlags.set(VoiceCallbackStopped);
                    else if(static_cast<uint>(gotBytes) < needBytes)
                    {
                        mFlags.set(VoiceCallbackStopped);
                        mNumCallbackBlocks += static_cast<uint>(gotBytes) / mBytesPerBlock;
                    }
                    else
                        mNumCallbackBlocks = static_cast<uint>(needBlocks);
                }
                const auto numSamples = size_t{mNumCallbackBlocks} * mSamplesPerBlock;
                const auto bufferSamples = resampleBuffer.first(srcBufferSize)
                    .subspan(srcSampleDelay);
                LoadBufferCallback(BufferListItem, bufferOffset, numSamples, chan, mFrameStep,
                    bufferSamples);
            }
            else
            {
                const auto uintPos = static_cast<uint>(std::max(intPos, 0));
                const auto bufferSamples = resampleBuffer.first(srcBufferSize)
                    .subspan(srcSampleDelay);
                LoadBufferQueue(BufferListItem, BufferLoopItem, uintPos, chan, mFrameStep,
                    bufferSamples);
            }

            /* If there's a matching sample step and no phase offset, use a
             * simple copy for resampling.
             */
            if(increment == MixerFracOne && fracPos == 0)
                std::ranges::copy(resampleBuffer.first(dstBufferSize),
                    MixingSamples[chan].subspan(samplesLoaded).begin());
            else
                mResampler(&mResampleState, Device->mResampleData, fracPos, increment,
                    MixingSamples[chan].subspan(samplesLoaded, dstBufferSize));

            /* Store the last source samples used for next time. */
            if(vstate == Playing) [[likely]]
            {
                /* Only store samples for the end of the mix, excluding what
                 * gets loaded for decoder padding.
                 */
                const uint loadEnd{samplesLoaded + dstBufferSize};
                if(samplesToMix > samplesLoaded && samplesToMix <= loadEnd) [[likely]]
                {
                    const size_t dstOffset{samplesToMix - samplesLoaded};
                    const size_t srcOffset{(dstOffset*increment + fracPos) >> MixerFracBits};
                    std::ranges::copy(Device->mResampleData | std::views::drop(srcOffset)
                        | std::views::take(prevSamples.size()), prevSamples.begin());
                }
            }

        skip_resample:
            samplesLoaded += dstBufferSize;
            if(samplesLoaded < samplesToLoad)
            {
                fracPos += dstBufferSize*increment;
                const auto srcOffset = fracPos >> MixerFracBits;
                fracPos &= MixerFracMask;
                intPos = al::add_sat(intPos, static_cast<int>(srcOffset));
                cbOffset += srcOffset;

                /* If more samples need to be loaded, copy the back of the
                 * resampleBuffer to the front to reuse it. prevSamples isn't
                 * reliable since it's only updated for the end of the mix.
                 */
                std::ranges::copy(Device->mResampleData | std::views::drop(srcOffset)
                    | std::views::take(MaxResamplerPadding), Device->mResampleData.begin());
            }
        }
    }
    if(mDuplicateMono)
    {
        /* NOTE: a mono source shouldn't have a decoder or the VoiceIsAmbisonic
         * flag, so aliasing instead of copying to the second channel shouldn't
         * be a problem.
         */
        MixingSamples[1] = MixingSamples[0];
    }
    else for(auto &samples : MixingSamples.subspan(realChannels))
        std::ranges::fill(samples, 0.0f);

    if(mDecoder)
    {
        mDecoder->decode(MixingSamples, (vstate==Playing));
        std::ranges::transform(MixingSamples, MixingSamples.begin(),
            [samplesToMix](const std::span<float> samples)
        { return samples.first(samplesToMix); });
    }

    if(mFlags.test(VoiceIsAmbisonic))
    {
        auto chandata = mChans.begin();
        for(const auto samplespan : MixingSamples)
        {
            chandata->mAmbiSplitter.processScale(samplespan, chandata->mAmbiHFScale,
                chandata->mAmbiLFScale);
            ++chandata;
        }
    }

    const auto Counter = mFlags.test(VoiceIsFading) ? std::min(samplesToMix, 64u) : 0u;
    if(!Counter)
    {
        /* No fading, just overwrite the old/current params. */
        for(auto &chandata : mChans)
        {
            if(auto &parms = chandata.mDryParams; !mFlags.test(VoiceHasHrtf))
                parms.Gains.Current = parms.Gains.Target;
            else
                parms.Hrtf.Old = parms.Hrtf.Target;

            std::ignore = std::ranges::mismatch(mSend | std::views::take(NumSends),
                chandata.mWetParams, [](TargetData &send, SendParams &parms)
            {
                if(!send.Buffer.empty())
                    parms.Gains.Current = parms.Gains.Target;
                return true;
            });
        }
    }

    auto chandata = mChans.begin();
    for(const auto samplespan : MixingSamples)
    {
        /* Now filter and mix to the appropriate outputs. */
        const auto FilterBuf = std::span{Device->FilteredData};
        {
            auto &parms = chandata->mDryParams;
            const auto samples = DoFilters(parms.LowPass, parms.HighPass, FilterBuf, samplespan,
                mDirect.FilterType);

            if(mFlags.test(VoiceHasHrtf))
            {
                const auto TargetGain = parms.Hrtf.Target.Gain * float(vstate == Playing);
                DoHrtfMix(samples, parms, TargetGain, Counter, OutPos, (vstate == Playing),
                    Device);
            }
            else
            {
                const auto TargetGains = (vstate == Playing) ? std::span{parms.Gains.Target}
                    : std::span{SilentTarget};
                if(mFlags.test(VoiceHasNfc))
                    DoNfcMix(samples, mDirect.Buffer, parms, TargetGains, Counter, OutPos, Device);
                else
                    MixSamples(samples, mDirect.Buffer, parms.Gains.Current, TargetGains, Counter,
                        OutPos);
            }
        }

        for(const auto send : std::views::iota(0u, NumSends))
        {
            if(mSend[send].Buffer.empty())
                continue;

            SendParams &parms = chandata->mWetParams[send];
            const auto samples = DoFilters(parms.LowPass, parms.HighPass, FilterBuf, samplespan,
                mSend[send].FilterType);

            const auto TargetGains = (vstate == Playing) ? std::span{parms.Gains.Target}
                : std::span{SilentTarget}.first<MaxAmbiChannels>();
            MixSamples(samples, mSend[send].Buffer, parms.Gains.Current, TargetGains, Counter,
                OutPos);
        }

        ++chandata;
    }

    mFlags.set(VoiceIsFading);

    /* Don't update positions and buffers if we were stopping. */
    if(vstate == Stopping) [[unlikely]]
    {
        mPlayState.store(Stopped, std::memory_order_release);
        return;
    }

    /* Update voice positions and buffers as needed. */
    DataPosFrac += increment*samplesToMix;
    auto const samplesDone = DataPosFrac >> MixerFracBits;
    DataPosInt = al::add_sat(DataPosInt, static_cast<int>(samplesDone));
    DataPosFrac &= MixerFracMask;

    auto buffers_done = 0u;
    if(BufferListItem && DataPosInt > 0) [[likely]]
    {
        if(mFlags.test(VoiceIsStatic))
        {
            if(BufferLoopItem)
            {
                /* Handle looping static source */
                const auto LoopStart = BufferListItem->mLoopStart;
                const auto LoopEnd = BufferListItem->mLoopEnd;
                auto DataPosUInt = static_cast<uint>(DataPosInt);
                if(DataPosUInt >= LoopEnd)
                {
                    assert(LoopEnd > LoopStart);
                    DataPosUInt = ((DataPosUInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                    DataPosInt = static_cast<int>(DataPosUInt);
                }
            }
            else
            {
                /* Handle non-looping static source */
                if(static_cast<uint>(DataPosInt) >= BufferListItem->mSampleLen)
                    BufferListItem = nullptr;
            }
        }
        else if(mFlags.test(VoiceIsCallback))
        {
            /* Handle callback buffer source */
            const auto endOffset = mCallbackBlockOffset + samplesDone;
            const auto blocksDone = endOffset / mSamplesPerBlock;
            if(blocksDone == 0)
                mCallbackBlockOffset = endOffset;
            else if(blocksDone < mNumCallbackBlocks)
            {
                const auto byteOffset = blocksDone*size_t{mBytesPerBlock};
                const auto byteEnd = mNumCallbackBlocks*size_t{mBytesPerBlock};
                const auto data = std::visit([](auto &splspan)
                { return std::as_writable_bytes(splspan); }, BufferListItem->mSamples);
                std::ranges::copy(data | std::views::take(byteEnd) | std::views::drop(byteOffset),
                    data.begin());
                mNumCallbackBlocks -= blocksDone;
                mCallbackBlockOffset = endOffset - blocksDone*mSamplesPerBlock;
            }
            else
            {
                BufferListItem = nullptr;
                mNumCallbackBlocks = 0;
                mCallbackBlockOffset = 0;
            }
        }
        else
        {
            /* Handle streaming source */
            do {
                if(BufferListItem->mSampleLen > static_cast<uint>(DataPosInt))
                    break;

                DataPosInt -= static_cast<int>(BufferListItem->mSampleLen);

                ++buffers_done;
                BufferListItem = BufferListItem->mNext.load(std::memory_order_relaxed);
                if(!BufferListItem) BufferListItem = BufferLoopItem;
            } while(BufferListItem);
        }
    }

    /* Capture the source ID in case it gets reset for stopping. */
    const auto SourceID = mSourceID.load(std::memory_order_relaxed);

    /* Update voice info */
    mPosition.store(DataPosInt, std::memory_order_relaxed);
    mPositionFrac.store(DataPosFrac, std::memory_order_relaxed);
    mCurrentBuffer.store(BufferListItem, std::memory_order_release);
    if(!BufferListItem)
    {
        mLoopBuffer.store(nullptr, std::memory_order_relaxed);
        mSourceID.store(0u, std::memory_order_release);
    }

    /* Send any events now, after the position/buffer info was updated. */
    const auto enabledevt = Context->mEnabledEvts.load(std::memory_order_acquire);
    if(buffers_done > 0 && enabledevt.test(al::to_underlying(AsyncEnableBits::BufferCompleted)))
    {
        auto *ring = Context->mAsyncEvents.get();
        auto evt_vec = ring->getWriteVector();
        if(!evt_vec[0].empty())
        {
            auto &evt = InitAsyncEvent<AsyncBufferCompleteEvent>(evt_vec[0].front());
            evt.mId = SourceID;
            evt.mCount = buffers_done;
            ring->writeAdvance(1);
        }
    }

    if(!BufferListItem)
    {
        /* If the voice just ended, set it to Stopping so the next render
         * ensures any residual noise fades to 0 amplitude.
         */
        mPlayState.store(Stopping, std::memory_order_release);
        if(enabledevt.test(al::to_underlying(AsyncEnableBits::SourceState)))
            SendSourceStoppedEvent(Context, SourceID);
    }
}

void Voice::prepare(DeviceBase *device)
{
    /* Mono can need 2 mixing channels when panning is enabled, which can be
     * done dynamically.
     *
     * UHJ2 and SuperStereo need 3 mixing channels, despite having only 2
     * buffer channels.
     *
     * Even if storing really high order ambisonics, we only mix channels for
     * orders up to the device order. The rest are simply dropped.
     */
    auto num_channels = (mFmtChannels == FmtMono) ? 2u
        : (mFmtChannels == FmtUHJ2 || mFmtChannels == FmtSuperStereo) ? 3u
        : ChannelsFromFmt(mFmtChannels, std::min(mAmbiOrder, device->mAmbiOrder));
    if(num_channels > device->MixerChannelsMax) [[unlikely]]
    {
        ERR("Unexpected channel count: {} (limit: {}, {} : {})", num_channels,
            device->MixerChannelsMax, NameFromFormat(mFmtChannels), mAmbiOrder);
        num_channels = device->MixerChannelsMax;
    }
    if(mChans.capacity() > 2 && num_channels < mChans.capacity())
    {
        decltype(mChans){}.swap(mChans);
        decltype(mPrevSamples){}.swap(mPrevSamples);
    }
    mChans.resize(num_channels);
    mPrevSamples.resize(num_channels);

    mDecoder = nullptr;
    mDecoderPadding = 0;
    static constexpr auto init_decoder = [](auto arg)
        -> std::pair<std::unique_ptr<DecoderBase>, uint>
    {
        using decoder_t = typename decltype(arg)::decoder_t;
        return {std::make_unique<decoder_t>(), decoder_t::sInputPadding};
    };
    if(mFmtChannels == FmtSuperStereo)
    {
        switch(UhjDecodeQuality)
        {
        case UhjQualityType::IIR:
            std::tie(mDecoder, mDecoderPadding) = init_decoder(UhjStereoDecoderIIR::Tag{});
            break;
        case UhjQualityType::FIR256:
            std::tie(mDecoder,mDecoderPadding)=init_decoder(UhjStereoDecoder<UhjLength256>::Tag{});
            break;
        case UhjQualityType::FIR512:
            std::tie(mDecoder,mDecoderPadding)=init_decoder(UhjStereoDecoder<UhjLength512>::Tag{});
            break;
        }
    }
    else if(IsUHJ(mFmtChannels))
    {
        switch(UhjDecodeQuality)
        {
        case UhjQualityType::IIR:
            std::tie(mDecoder, mDecoderPadding) = init_decoder(UhjDecoderIIR::Tag{});
            break;
        case UhjQualityType::FIR256:
            std::tie(mDecoder, mDecoderPadding) = init_decoder(UhjDecoder<UhjLength256>::Tag{});
            break;
        case UhjQualityType::FIR512:
            std::tie(mDecoder, mDecoderPadding) = init_decoder(UhjDecoder<UhjLength512>::Tag{});
            break;
        }
    }

    /* Clear the stepping value explicitly so the mixer knows not to mix this
     * until the update gets applied.
     */
    mStep = 0;

    /* Make sure the sample history is cleared. */
    std::ranges::fill(mPrevSamples | std::views::join, 0.0f);

    if(mFmtChannels == FmtUHJ2 && !std::holds_alternative<UhjPostProcess>(device->mPostProcess))
    {
        /* 2-channel UHJ needs different shelf filters. However, we can't just
         * use different shelf filters after mixing it, given any old speaker
         * setup the user has. To make this work, we apply the expected shelf
         * filters for decoding UHJ2 to quad (only needs LF scaling), and act
         * as if those 4 quad channels are encoded right back into B-Format.
         *
         * This isn't perfect, but without an entirely separate and limited
         * UHJ2 path, it's better than nothing.
         *
         * Note this isn't needed with UHJ output (UHJ2->B-Format->UHJ2 is
         * identity, so don't mess with it).
         */
        const auto splitter = BandSplitter{device->mXOverFreq
            / static_cast<float>(device->mSampleRate)};
        std::ranges::for_each(mChans, [splitter,device](Voice::ChannelData &chandata)
        {
            chandata.mAmbiHFScale = 1.0f;
            chandata.mAmbiLFScale = 1.0f;
            chandata.mAmbiSplitter = splitter;
            chandata.mDryParams = DirectParams{};
            chandata.mDryParams.NFCtrlFilter = device->mNFCtrlFilter;
            std::ranges::fill(chandata.mWetParams | std::views::take(device->NumAuxSends),
                SendParams{});
        });
        mChans[0].mAmbiLFScale = DecoderBase::sWLFScale;
        mChans[1].mAmbiLFScale = DecoderBase::sXYLFScale;
        mChans[2].mAmbiLFScale = DecoderBase::sXYLFScale;
        mFlags.set(VoiceIsAmbisonic);
    }
    /* Don't need to set the VoiceIsAmbisonic flag if the device is not higher
     * order than the voice. No HF scaling is necessary to mix it.
     */
    else if(mAmbiOrder && device->mAmbiOrder > mAmbiOrder)
    {
        const auto OrdersSpan = Is2DAmbisonic(mFmtChannels)
            ? std::span<const uint8_t>{AmbiIndex::OrderFrom2DChannel}
            : std::span<const uint8_t>{AmbiIndex::OrderFromChannel};
        const auto scales = AmbiScale::GetHFOrderScales(mAmbiOrder, device->mAmbiOrder,
            device->m2DMixing);

        const auto splitter = BandSplitter{device->mXOverFreq
            / static_cast<float>(device->mSampleRate)};
        std::ignore = std::ranges::mismatch(mChans, OrdersSpan,
            [&scales,splitter,device](Voice::ChannelData &chandata, const size_t scaleidx)
        {
            chandata.mAmbiHFScale = scales[scaleidx];
            chandata.mAmbiLFScale = 1.0f;
            chandata.mAmbiSplitter = splitter;
            chandata.mDryParams = DirectParams{};
            chandata.mDryParams.NFCtrlFilter = device->mNFCtrlFilter;
            std::ranges::fill(chandata.mWetParams | std::views::take(device->NumAuxSends),
                SendParams{});
            return true;
        });
        mFlags.set(VoiceIsAmbisonic);
    }
    else
    {
        std::ranges::for_each(mChans, [device](Voice::ChannelData &chandata)
        {
            chandata.mDryParams = DirectParams{};
            chandata.mDryParams.NFCtrlFilter = device->mNFCtrlFilter;
            std::ranges::fill(chandata.mWetParams | std::views::take(device->NumAuxSends),
                SendParams{});
        });
        mFlags.reset(VoiceIsAmbisonic);
    }
}
