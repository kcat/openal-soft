
#include "config.h"
#include "config_simd.h"

#include "voice.h"

#include <algorithm>
#include <array>
#include <atomic>
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
#include "gsl/gsl"
#include "logging.h"
#include "mixer.h"
#include "mixer/defs.h"
#include "mixer/hrtfdefs.h"
#include "opthelpers.h"
#include "resampler_limits.h"
#include "ringbuffer.h"
#include "vector.h"
#include "voice_change.h"


namespace {

static_assert((DeviceBase::MixerLineSize&3) == 0, "MixerLineSize must be a multiple of 4");
static_assert((MaxResamplerEdge&3) == 0, "MaxResamplerEdge is not a multiple of 4");

constexpr auto PitchLimit = (std::numeric_limits<i32>::max()-MixerFracMask) / MixerFracOne
    / BufferLineSize;
static_assert(MaxPitch <= PitchLimit, "MaxPitch, BufferLineSize, or MixerFracBits is too large");
static_assert(BufferLineSize > MaxPitch, "MaxPitch must be less then BufferLineSize");


using namespace std::chrono;
using namespace std::string_view_literals;

using HrtfMixerFunc = void(*)(std::span<const f32> InSamples, std::span<f32x2> AccumSamples,
    u32 IrSize, MixHrtfFilter const *hrtfparams, usize SamplesToDo);
using HrtfMixerBlendFunc = void(*)(std::span<const f32> InSamples, std::span<f32x2> AccumSamples,
    u32 IrSize, HrtfFilter const *oldparams, MixHrtfFilter const *newparams, usize SamplesToDo);

constinit auto MixHrtfSamples = HrtfMixerFunc{MixHrtf_C};
constinit auto MixHrtfBlendSamples = HrtfMixerBlendFunc{MixHrtfBlend_C};

[[nodiscard]]
auto SelectMixer() -> MixerOutFunc
{
#if HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return Mix_NEON;
#endif
#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return Mix_SSE;
#endif
    return Mix_C;
}

[[nodiscard]]
auto SelectMixerOne() -> MixerOneFunc
{
#if HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return Mix_NEON;
#endif
#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return Mix_SSE;
#endif
    return Mix_C;
}

auto SelectHrtfMixer() -> HrtfMixerFunc
{
#if HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtf_NEON;
#endif
#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtf_SSE;
#endif
    return MixHrtf_C;
}

auto SelectHrtfBlendMixer() -> HrtfMixerBlendFunc
{
#if HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtfBlend_NEON;
#endif
#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtfBlend_SSE;
#endif
    return MixHrtfBlend_C;
}

} // namespace

void Voice::InitMixer(std::optional<std::string> const &resopt)
{
    if(resopt)
    {
        struct ResamplerEntry {
            std::string_view const name;
            Resampler const resampler;
        };
        constexpr auto ResamplerList = std::array{
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

        auto const iter = std::ranges::find_if(ResamplerList,
            [resampler](ResamplerEntry const &entry)
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


void SendSourceStoppedEvent(ContextBase const *const context, u32 const id)
{
    auto *const ring = context->mAsyncEvents.get();
    auto const evt_vec = ring->getWriteVector();
    if(evt_vec[0].empty()) return;

    auto &evt = InitAsyncEvent<AsyncSourceStateEvent>(evt_vec[0].front());
    evt.mId = id;
    evt.mState = AsyncSrcState::Stop;

    ring->writeAdvance(1);
}


auto DoFilters(BiquadInterpFilter &lpfilter, BiquadInterpFilter &hpfilter,
    std::span<f32, BufferLineSize> const dst LIFETIMEBOUND,
    std::span<f32 const> const src LIFETIMEBOUND, bool const active) -> std::span<f32 const>
{
    if(active)
    {
        DualBiquadInterp{lpfilter, hpfilter}.process(src, dst);
        return dst.first(src.size());
    }
    lpfilter.clear();
    hpfilter.clear();
    return src;
}


template<typename T>
void LoadSamples(std::span<f32> const dstSamples, std::span<T const> const srcData,
    usize const srcChan, usize const srcOffset, usize const srcStep,
    usize const samplesPerBlock [[maybe_unused]]) noexcept
{
    using TypeTraits = SampleInfo<T>;
    Expects(srcChan < srcStep);

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
void LoadSamples<IMA4Data>(std::span<f32> dstSamples, std::span<IMA4Data const> src,
    usize const srcChan, usize const srcOffset, usize const srcStep,
    usize const samplesPerBlock) noexcept
{
    static constexpr auto MaxStepIndex = gsl::narrow<isize>(IMAStep_size.size()) - 1;

    Expects(srcStep > 0 && srcStep <= 2);
    Expects(srcChan < srcStep);
    Expects(samplesPerBlock > 1);
    auto const blockBytes = ((samplesPerBlock-1_uz)/2_uz + 4_uz)*srcStep;

    /* Skip to the ADPCM block containing the srcOffset sample. */
    src = src.subspan(srcOffset / samplesPerBlock * blockBytes);
    /* Calculate how many samples need to be skipped in the block. */
    auto skip = srcOffset % samplesPerBlock;

    /* NOTE: This could probably be optimized better. */
    while(!dstSamples.empty())
    {
        /* Each IMA4 block starts with a signed 16-bit sample, and a signed(?)
         * 16-bit table index. The table index needs to be clamped.
         */
        auto sample = i32{bit_pack<i16>(src[srcChan*4 + 1].value, src[srcChan*4 + 0].value)};
        auto ima_idx = isize{bit_pack<i16>(src[srcChan*4 + 3].value, src[srcChan*4 + 2].value)};
        ima_idx = std::clamp(ima_idx, 0_z, MaxStepIndex);

        auto const nibbleData = src.subspan((srcStep+srcChan)*4);
        src = src.subspan(blockBytes);

        if(skip == 0)
        {
            dstSamples[0] = gsl::narrow_cast<f32>(sample) / 32768.0f;
            dstSamples = dstSamples.subspan(1);
            if(dstSamples.empty()) return;
        }
        else
            --skip;

        /* The rest of the block is arranged as a series of nibbles, contained
         * in 4 *bytes* per channel interleaved. So every 8 nibbles we need to
         * skip 4 bytes per channel to get the next nibbles for this channel.
         */
        auto decode_nibble = [&sample,&ima_idx,srcStep,nibbleData](usize const nibbleOffset)
            noexcept -> i32
        {
            static constexpr auto NibbleMask = std::byte{0xf};
            auto const byteShift = (nibbleOffset&1) * 4;
            auto const wordOffset = (nibbleOffset>>1) & ~3_uz;
            auto const byteOffset = wordOffset*srcStep + ((nibbleOffset>>1)&3);

            auto const nibble = (nibbleData[byteOffset].value >> byteShift) & NibbleMask;
            auto const codeidx = to_integer<usize>(nibble);

            sample += IMA4Codeword[codeidx] * IMAStep_size[gsl::narrow_cast<u32>(ima_idx)] / 8;
            sample = std::clamp(sample, -32768, 32767);

            ima_idx = std::clamp(ima_idx + IMA4Index_adjust[codeidx], 0_z, MaxStepIndex);

            return sample;
        };

        /* First, decode the samples that we need to skip in the block (will
         * always be less than the block size). They need to be decoded despite
         * being ignored for proper state on the remaining samples.
         */
        auto const startOffset = skip + 1_uz;
        auto nibbleOffset = 0_uz;
        for(;skip;--skip)
        {
            std::ignore = decode_nibble(nibbleOffset);
            ++nibbleOffset;
        }

        /* Second, decode the rest of the block and write to the output, until
         * the end of the block or the end of output.
         */
        auto const written = std::min(samplesPerBlock-startOffset, dstSamples.size());
        std::ranges::generate(dstSamples.first(written), [&]
        {
            auto const decspl = decode_nibble(nibbleOffset);
            ++nibbleOffset;

            return gsl::narrow_cast<f32>(decspl) / 32768.0f;
        });
        dstSamples = dstSamples.subspan(written);
    }
}

template<>
void LoadSamples<MSADPCMData>(std::span<f32> dstSamples, std::span<MSADPCMData const> src,
    usize const srcChan, usize const srcOffset, usize const srcStep,
    usize const samplesPerBlock) noexcept
{
    Expects(srcStep > 0 && srcStep <= 2);
    Expects(srcChan < srcStep);
    Expects(samplesPerBlock > 2);
    auto const blockBytes = ((samplesPerBlock-2_uz)/2_uz + 7_uz)*srcStep;

    src = src.subspan(srcOffset / samplesPerBlock * blockBytes);
    auto skip = srcOffset % samplesPerBlock;

    while(!dstSamples.empty())
    {
        /* Each MS ADPCM block starts with an 8-bit block predictor, used to
         * dictate how the two sample history values are mixed with the decoded
         * sample, and an initial signed 16-bit scaling value which scales the
         * nibble sample value. This is followed by the two initial 16-bit
         * sample history values.
         */
        auto const blockpred = u8{std::min(to_integer<u8::value_t>(src[srcChan].value),
            u8::value_t{MSADPCMAdaptionCoeff.size()-1})};
        auto scale = i32{bit_pack<i16>(src[srcStep + 2*srcChan + 1].value,
            src[srcStep + 2*srcChan + 0].value)};

        auto sampleHistory = std::array{
            i32{bit_pack<i16>(src[3*srcStep + 2*srcChan + 1].value,
                src[3*srcStep + 2*srcChan + 0].value)},
            i32{bit_pack<i16>(src[5*srcStep + 2*srcChan + 1].value,
                src[5*srcStep + 2*srcChan + 0].value)}};

        auto const nibbleData = src.subspan(7*srcStep);
        src = src.subspan(blockBytes);

        auto const coeffs = std::span{MSADPCMAdaptionCoeff[blockpred.c_val]};

        /* The second history sample is "older", so it's the first to be
         * written out.
         */
        if(skip == 0)
        {
            dstSamples[0] = gsl::narrow_cast<f32>(sampleHistory[1]) / 32768.0f;
            if(dstSamples.size() < 2) return;
            dstSamples[1] = gsl::narrow_cast<f32>(sampleHistory[0]) / 32768.0f;
            dstSamples = dstSamples.subspan(2);
            if(dstSamples.empty()) return;
        }
        else if(skip == 1)
        {
            --skip;
            dstSamples[0] = gsl::narrow_cast<f32>(sampleHistory[0]) / 32768.0f;
            dstSamples = dstSamples.subspan(1);
            if(dstSamples.empty()) return;
        }
        else
            skip -= 2;

        /* The rest of the block is a series of nibbles, interleaved per
         * channel.
         */
        auto decode_nibble = [&sampleHistory,&scale,coeffs,nibbleData](usize const nibbleOffset)
            noexcept -> i32
        {
            static constexpr auto NibbleMask = std::byte{0xf};
            auto const byteOffset = nibbleOffset>>1;
            auto const byteShift = ((nibbleOffset&1)^1) * 4;

            auto const nibble = (nibbleData[byteOffset].value >> byteShift) & NibbleMask;
            auto const nval = to_integer<u8::value_t>(nibble);

            auto const pred = ((i32{nval}^0x08) - 0x08) * scale;
            auto const diff = (sampleHistory[0]*coeffs[0] + sampleHistory[1]*coeffs[1]) / 256;
            auto const sample = std::clamp(pred + diff, -32768, 32767);

            sampleHistory[1] = sampleHistory[0];
            sampleHistory[0] = sample;

            scale = std::max(MSADPCMAdaption[nval] * scale / 256, 16);

            return sample;
        };

        /* First, skip samples. */
        auto const startOffset = skip + 2_uz;
        auto nibbleOffset = srcChan;
        for(;skip;--skip)
        {
            std::ignore = decode_nibble(nibbleOffset);
            nibbleOffset += srcStep;
        }

        /* Now decode the rest of the block, until the end of the block or the
         * dst buffer is filled.
         */
        auto const written = std::min(samplesPerBlock-startOffset, dstSamples.size());
        std::ranges::generate(dstSamples.first(written), [&]
        {
            auto const sample = decode_nibble(nibbleOffset);
            nibbleOffset += srcStep;

            return gsl::narrow_cast<f32>(sample) / 32768.0f;
        });
        dstSamples = dstSamples.subspan(written);
    }
}

void LoadSamples(std::span<f32> const dstSamples, SampleVariant const &src,
    usize const srcChan, usize const srcOffset, usize const srcStep,
    usize const samplesPerBlock) noexcept
{
    std::visit([&]<typename T>(T&& splvec)
    {
        using sample_t = std::remove_cvref_t<T>::value_type;
        LoadSamples<sample_t>(dstSamples, splvec, srcChan, srcOffset, srcStep, samplesPerBlock);
    }, src);
}

void LoadBufferStatic(VoiceBufferItem const *const buffer,
    VoiceBufferItem const *const bufferLoopItem, usize const dataPosInt, usize const srcChannel,
    usize const srcStep, std::span<f32> voiceSamples)
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
        auto const loopStart = usize{buffer->mLoopStart};
        auto const loopEnd = usize{buffer->mLoopEnd};
        ASSUME(loopEnd > loopStart);

        auto const intPos = (dataPosInt < loopEnd) ? dataPosInt
            : (((dataPosInt-loopStart)%(loopEnd-loopStart)) + loopStart);

        /* Load what's left of this loop iteration */
        auto const remaining = std::min(voiceSamples.size(), loopEnd-intPos);
        LoadSamples(voiceSamples.first(remaining), buffer->mSamples, srcChannel, intPos, srcStep,
            buffer->mBlockAlign);
        voiceSamples = voiceSamples.subspan(remaining);

        /* Load repeats of the loop to fill the buffer. */
        auto const loopSize = loopEnd - loopStart;
        while(auto const toFill = std::min(voiceSamples.size(), loopSize))
        {
            LoadSamples(voiceSamples.first(toFill), buffer->mSamples, srcChannel, loopStart,
                srcStep, buffer->mBlockAlign);
            voiceSamples = voiceSamples.subspan(toFill);
        }
    }
}

void LoadBufferCallback(VoiceBufferItem const *const buffer, usize const dataPosInt,
    usize const numCallbackSamples, usize const srcChannel, usize const srcStep,
    std::span<f32> voiceSamples)
{
    auto lastSample = 0.0f;
    if(numCallbackSamples > dataPosInt) [[likely]]
    {
        auto const remaining = std::min(voiceSamples.size(), numCallbackSamples-dataPosInt);
        LoadSamples(voiceSamples.first(remaining), buffer->mSamples, srcChannel, dataPosInt,
            srcStep, buffer->mBlockAlign);
        lastSample = voiceSamples[remaining-1];
        voiceSamples = voiceSamples.subspan(remaining);
    }

    std::ranges::fill(voiceSamples, lastSample);
}

void LoadBufferQueue(VoiceBufferItem const *buffer, VoiceBufferItem const *const bufferLoopItem,
    usize dataPosInt, usize const srcChannel, usize const srcStep,
    std::span<f32> voiceSamples)
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

        auto const remaining = std::min(voiceSamples.size(), buffer->mSampleLen-dataPosInt);
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


void DoHrtfMix(std::span<f32 const> const samples, DirectParams &parms, f32 const targetGain,
    usize const counter, usize outPos, bool const isPlaying, DeviceBase *const device)
{
    auto const IrSize = device->mIrSize;
    auto const HrtfSamples = std::span{device->ExtraSampleData};
    auto const AccumSamples = std::span{device->HrtfAccumData};

    /* Copy the HRTF history and new input samples into a temp buffer. */
    auto const src_iter = std::ranges::copy(parms.Hrtf.History, HrtfSamples.begin()).out;
    std::ranges::copy(samples, src_iter);
    /* Copy the last used samples back into the history buffer for later. */
    if(isPlaying) [[likely]]
    {
        auto const endsamples = HrtfSamples.subspan(samples.size(), parms.Hrtf.History.size());
        std::ranges::copy(endsamples, parms.Hrtf.History.begin());
    }

    /* If fading and this is the first mixing pass, fade between the IRs. */
    auto fademix = 0_uz;
    if(counter && outPos == 0)
    {
        fademix = std::min(samples.size(), counter);

        auto gain = targetGain;

        /* The new coefficients need to fade in completely since they're
         * replacing the old ones. To keep the gain fading consistent,
         * interpolate between the old and new target gains given how much of
         * the fade time this mix handles.
         */
        if(counter > fademix)
        {
            auto const a = gsl::narrow_cast<f32>(fademix) / gsl::narrow_cast<f32>(counter);
            gain = lerpf(parms.Hrtf.Old.Gain, targetGain, a);
        }

        auto const hrtfparams = MixHrtfFilter{
            parms.Hrtf.Target.Coeffs,
            parms.Hrtf.Target.Delay,
            0.0f, gain / gsl::narrow_cast<f32>(fademix)};
        MixHrtfBlendSamples(HrtfSamples, AccumSamples.subspan(outPos), IrSize, &parms.Hrtf.Old,
            &hrtfparams, fademix);

        /* Update the old parameters with the result. */
        parms.Hrtf.Old = parms.Hrtf.Target;
        parms.Hrtf.Old.Gain = gain;
        outPos += fademix;
    }

    if(fademix < samples.size())
    {
        auto const todo = samples.size() - fademix;
        auto gain = targetGain;

        /* Interpolate the target gain if the gain fading lasts longer than
         * this mix.
         */
        if(counter > samples.size())
        {
            auto const a = gsl::narrow_cast<f32>(todo) / gsl::narrow_cast<f32>(counter-fademix);
            gain = lerpf(parms.Hrtf.Old.Gain, targetGain, a);
        }

        auto const hrtfparams = MixHrtfFilter{
            parms.Hrtf.Target.Coeffs,
            parms.Hrtf.Target.Delay,
            parms.Hrtf.Old.Gain,
            (gain - parms.Hrtf.Old.Gain) / gsl::narrow_cast<f32>(todo)};
        MixHrtfSamples(HrtfSamples.subspan(fademix), AccumSamples.subspan(outPos), IrSize,
            &hrtfparams, todo);

        /* Store the now-current gain for next time. */
        parms.Hrtf.Old.Gain = gain;
    }
}

void DoNfcMix(std::span<f32 const> const samples, std::span<FloatBufferLine> outBuffer,
    DirectParams &parms, std::span<f32 const, MaxOutputChannels> const outGains,
    u32 const counter, u32 const outPos, DeviceBase *const device)
{
    using FilterProc = void(NfcFilter::*)(std::span<f32 const> src, std::span<f32> dst);
    static constexpr auto NfcProcess = std::array{FilterProc{nullptr}, &NfcFilter::process1,
        &NfcFilter::process2, &NfcFilter::process3, &NfcFilter::process4};
    static_assert(NfcProcess.size() == MaxAmbiOrder+1);

    MixSamples(samples, std::span{outBuffer[0]}.subspan(outPos), parms.Gains.Current[0],
        outGains[0], counter);
    outBuffer = outBuffer.subspan(1);
    auto CurrentGains = std::span{parms.Gains.Current}.subspan(1);
    auto TargetGains = outGains.subspan(1);

    auto const nfcsamples = std::span{device->ExtraSampleData}.first(samples.size());
    auto order = 1_uz;
    while(auto const chancount = usize{device->NumChannelsPerOrder[order]})
    {
        (parms.NFCtrlFilter.*NfcProcess[order])(samples, nfcsamples);
        MixSamples(nfcsamples, outBuffer.first(chancount), CurrentGains, TargetGains, counter,
            outPos);
        if(++order == MaxAmbiOrder+1)
            break;
        outBuffer = outBuffer.subspan(chancount);
        CurrentGains = CurrentGains.subspan(chancount);
        TargetGains = TargetGains.subspan(chancount);
    }
}

} // namespace

void Voice::mix(State const vstate, ContextBase *const context, nanoseconds const deviceTime,
    u32 const samplesToDo)
{
    static constexpr auto SilentTarget = std::array<f32, MaxOutputChannels>{};

    ASSUME(samplesToDo > 0);

    auto const device = al::get_not_null(context->mDevice);
    auto const numSends = device->NumAuxSends;

    /* Get voice info */
    auto bufPosInt = mPosition.load(std::memory_order_relaxed);
    auto bufPosFrac = mPositionFrac.load(std::memory_order_relaxed);
    auto *bufferListItem = mCurrentBuffer.load(std::memory_order_relaxed);
    auto *bufferLoopItem = mLoopBuffer.load(std::memory_order_relaxed);
    auto const increment = mStep;
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
    if(mFlags.test(VoiceIsStatic) && bufferLoopItem)
    {
        if(std::cmp_greater_equal(bufPosInt, bufferListItem->mLoopEnd))
            bufferLoopItem = nullptr;
    }

    auto outPos = 0_u32;

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
        auto const diff = mStartTime - deviceTime;
        if(diff >= seconds{1})
            return;

        /* Get the number of samples ahead of the current time that output
         * should start at. Skip this update if it's beyond the output sample
         * count.
         */
        outPos = gsl::narrow_cast<u32>(round<seconds>(diff * device->mSampleRate).count());
        if(outPos >= samplesToDo) return;
    }

    /* Calculate the number of samples to mix, and the number of (resampled)
     * samples that need to be loaded (mixing samples and decoder padding).
     */
    auto const samplesToMix = samplesToDo - outPos;
    auto const samplesToLoad = samplesToMix + mDecoderPadding;

    /* Get a span of pointers to hold the floating point, deinterlaced,
     * resampled buffer data to be mixed.
     */
    auto samplePointers = std::array<std::span<f32>, DeviceBase::MixerChannelsMax>{};
    auto const mixingSamples = std::span{samplePointers}
        .first((mFmtChannels == FmtMono && !mDuplicateMono) ? 1_uz : mChans.size());
    {
        auto const channelStep = (samplesToLoad+3_u32)&~3_u32;
        auto base = device->mSampleData.end() - mixingSamples.size()*channelStep;
        std::ranges::generate(mixingSamples, [&base,samplesToLoad,channelStep]
        {
            const auto ret = base;
            std::advance(base, channelStep);
            return std::span{ret, samplesToLoad};
        });
    }

    /* UHJ2 and SuperStereo only have 2 buffer channels, but 3 mixing channels
     * (3rd channel is generated from decoding).
     */
    auto const realChannels = (mFmtChannels == FmtMono) ? 1_uz
        : (mFmtChannels == FmtUHJ2 || mFmtChannels == FmtSuperStereo) ? 2_uz
        : mixingSamples.size();
    for(auto const chan : std::views::iota(0_uz, realChannels))
    {
        static constexpr auto ResBufSize = std::tuple_size_v<decltype(DeviceBase::mResampleData)>;
        static constexpr auto SrcSizeMax = u32{ResBufSize - MaxResamplerEdge};

        auto const prevSamples = std::span{mPrevSamples[chan]};
        std::ranges::copy(prevSamples, device->mResampleData.begin());
        auto const resampleBuffer = std::span{device->mResampleData}.subspan<MaxResamplerEdge>();
        auto cbOffset = mCallbackBlockOffset;
        auto intPos = bufPosInt;
        auto fracPos = bufPosFrac;

        /* Load samples for this channel from the available buffer(s), with
         * resampling.
         */
        for(auto samplesLoaded = 0_u32;samplesLoaded < samplesToLoad;)
        {
            /* Calculate the number of dst samples that can be loaded this
             * iteration, given the available resampler buffer size, and the
             * number of src samples that are needed to load it.
             */
            const auto [dstBufferSize, srcBufferSize] = std::invoke(
                [fracPos,increment,dstRemaining = samplesToLoad-samplesLoaded]() noexcept
                -> std::array<u32, 2>
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
                auto dataSize64 = u64{dstRemaining - ext};
                dataSize64 = (dataSize64*increment + fracPos) >> MixerFracBits;
                /* Also include resampler padding. */
                dataSize64 += ext + MaxResamplerEdge;

                if(dataSize64 <= SrcSizeMax)
                    return std::array{dstRemaining, gsl::narrow_cast<u32>(dataSize64)};

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
                    return std::array{gsl::narrow_cast<u32>(dataSize64)&~3_u32, SrcSizeMax};
                }
                return std::array{dstRemaining, SrcSizeMax};
            });

            auto srcSampleDelay = 0_uz;
            if(intPos < 0) [[unlikely]]
            {
                /* If the current position is negative, there's that many
                 * silent samples to load before using the buffer.
                 */
                srcSampleDelay = gsl::narrow_cast<u32>(-intPos);
                if(srcSampleDelay >= srcBufferSize)
                {
                    /* If the number of silent source samples exceeds the
                     * number to load, the output will be silent.
                     */
                    std::ranges::fill(mixingSamples[chan].subspan(samplesLoaded, dstBufferSize),
                        0.0f);
                    std::ranges::fill(resampleBuffer.first(srcBufferSize), 0.0f);
                    goto skip_resample;
                }

                std::ranges::fill(resampleBuffer | std::views::take(srcSampleDelay), 0.0f);
            }

            /* Load the necessary samples from the given buffer(s). */
            if(!bufferListItem) [[unlikely]]
            {
                auto const avail = std::min(srcBufferSize, MaxResamplerEdge);
                auto const tofill = std::max(srcBufferSize, MaxResamplerEdge);
                auto const srcbuf = resampleBuffer.first(tofill);

                /* When loading from a voice that ended prematurely, only take
                 * the samples that get closest to 0 amplitude. This helps
                 * certain sounds fade out better.
                 */
                auto const srciter = std::ranges::min_element(srcbuf.begin(),
                    std::next(srcbuf.begin(), gsl::narrow_cast<ptrdiff_t>(avail)), {},
                    [](f32 const s) { return std::abs(s); });

                std::ranges::fill(std::next(srciter), srcbuf.end(), *srciter);
            }
            else if(mFlags.test(VoiceIsStatic))
            {
                auto const uintPos = gsl::narrow_cast<u32>(std::max(intPos, 0_i32));
                auto const bufferSamples = resampleBuffer.first(srcBufferSize)
                    .subspan(srcSampleDelay);
                LoadBufferStatic(bufferListItem, bufferLoopItem, uintPos, chan, mFrameStep,
                    bufferSamples);
            }
            else if(mFlags.test(VoiceIsCallback))
            {
                auto const bufferOffset = usize{cbOffset};
                auto const needSamples = bufferOffset + srcBufferSize - srcSampleDelay;
                auto const needBlocks = (needSamples + mSamplesPerBlock-1) / mSamplesPerBlock;
                if(!mFlags.test(VoiceCallbackStopped) && needBlocks > mNumCallbackBlocks)
                {
                    auto const byteOffset = mNumCallbackBlocks * usize{mBytesPerBlock};
                    auto const needBytes = (needBlocks-mNumCallbackBlocks) * usize{mBytesPerBlock};

                    auto const samples = std::visit([](auto &splspan)
                    { return std::as_writable_bytes(splspan); }, bufferListItem->mSamples);

                    auto const gotBytes = bufferListItem->mCallback(bufferListItem->mUserData,
                        &samples[byteOffset], gsl::narrow_cast<int>(needBytes));
                    if(gotBytes < 0)
                        mFlags.set(VoiceCallbackStopped);
                    else if(gsl::narrow_cast<u32>(gotBytes) < needBytes)
                    {
                        mFlags.set(VoiceCallbackStopped);
                        mNumCallbackBlocks += gsl::narrow_cast<u32>(gotBytes) / mBytesPerBlock;
                    }
                    else
                        mNumCallbackBlocks = gsl::narrow_cast<u32>(needBlocks);
                }
                auto const numSamples = usize{mNumCallbackBlocks} * mSamplesPerBlock;
                auto const bufferSamples = resampleBuffer.first(srcBufferSize)
                    .subspan(srcSampleDelay);
                LoadBufferCallback(bufferListItem, bufferOffset, numSamples, chan, mFrameStep,
                    bufferSamples);
            }
            else
            {
                auto const uintPos = gsl::narrow_cast<u32>(std::max(intPos, 0_i32));
                auto const bufferSamples = resampleBuffer.first(srcBufferSize)
                    .subspan(srcSampleDelay);
                LoadBufferQueue(bufferListItem, bufferLoopItem, uintPos, chan, mFrameStep,
                    bufferSamples);
            }

            /* If there's a matching sample step and no phase offset, use a
             * simple copy for resampling.
             */
            if(increment == MixerFracOne && fracPos == 0)
                std::ranges::copy(resampleBuffer.first(dstBufferSize),
                    mixingSamples[chan].subspan(samplesLoaded).begin());
            else
                mResampler(&mResampleState, device->mResampleData, fracPos, increment,
                    mixingSamples[chan].subspan(samplesLoaded, dstBufferSize));

            /* Store the last source samples used for next time. */
            if(vstate == Playing) [[likely]]
            {
                /* Only store samples for the end of the mix, excluding what
                 * gets loaded for decoder padding.
                 */
                auto const loadEnd = samplesLoaded + dstBufferSize;
                if(samplesToMix > samplesLoaded && samplesToMix <= loadEnd) [[likely]]
                {
                    auto const dstOffset = usize{samplesToMix - samplesLoaded};
                    auto const srcOffset = usize{(dstOffset*increment + fracPos) >> MixerFracBits};
                    std::ranges::copy(device->mResampleData | std::views::drop(srcOffset)
                        | std::views::take(prevSamples.size()), prevSamples.begin());
                }
            }

        skip_resample:
            samplesLoaded += dstBufferSize;
            if(samplesLoaded < samplesToLoad)
            {
                fracPos += dstBufferSize*increment;
                auto const srcOffset = fracPos >> MixerFracBits;
                fracPos &= MixerFracMask;
                intPos = al::add_sat(intPos, gsl::narrow_cast<i32>(srcOffset));
                cbOffset += srcOffset;

                /* If more samples need to be loaded, copy the back of the
                 * resampleBuffer to the front to reuse it. prevSamples isn't
                 * reliable since it's only updated for the end of the mix.
                 */
                std::ranges::copy(device->mResampleData | std::views::drop(srcOffset)
                    | std::views::take(MaxResamplerPadding), device->mResampleData.begin());
            }
        }
    }
    if(mDuplicateMono)
    {
        /* NOTE: a mono source shouldn't have a decoder or the VoiceIsAmbisonic
         * flag, so aliasing instead of copying to the second channel shouldn't
         * be a problem.
         */
        mixingSamples[1] = mixingSamples[0];
    }
    else for(auto &samples : mixingSamples.subspan(realChannels))
        std::ranges::fill(samples, 0.0f);

    if(mDecoder)
    {
        mDecoder->decode(mixingSamples, (vstate==Playing));
        std::ranges::transform(mixingSamples, mixingSamples.begin(),
            [samplesToMix](std::span<f32> const samples)
        { return samples.first(samplesToMix); });
    }

    if(mFlags.test(VoiceIsAmbisonic))
    {
        auto chandata = mChans.begin();
        for(auto const samplespan : mixingSamples)
        {
            chandata->mAmbiSplitter.processScale(samplespan, chandata->mAmbiHFScale,
                chandata->mAmbiLFScale);
            ++chandata;
        }
    }

    auto const counter = mFlags.test(VoiceIsFading) ? std::min(samplesToMix, 64_u32) : 0_u32;
    if(!counter)
    {
        /* No fading, just overwrite the old/current params. */
        for(auto &chandata : mChans)
        {
            if(auto &parms = chandata.mDryParams; !mFlags.test(VoiceHasHrtf))
                parms.Gains.Current = parms.Gains.Target;
            else
                parms.Hrtf.Old = parms.Hrtf.Target;

            std::ignore = std::ranges::mismatch(mSend | std::views::take(numSends),
                chandata.mWetParams, [](TargetData const &send, SendParams &parms)
            {
                if(!send.Buffer.empty())
                    parms.Gains.Current = parms.Gains.Target;
                return true;
            });
        }
    }

    auto chandata = mChans.begin();
    for(auto const samplespan : mixingSamples)
    {
        /* Now filter and mix to the appropriate outputs. */
        auto const FilterBuf = std::span{device->FilteredData};
        {
            auto &parms = chandata->mDryParams;
            auto const samples = DoFilters(parms.LowPass, parms.HighPass, FilterBuf, samplespan,
                mDirect.FilterActive);

            if(mFlags.test(VoiceHasHrtf))
            {
                auto const targetGain = parms.Hrtf.Target.Gain
                    * gsl::narrow_cast<f32>(vstate == Playing);
                DoHrtfMix(samples, parms, targetGain, counter, outPos, (vstate == Playing),
                    device);
            }
            else
            {
                auto const targetGains = (vstate == Playing) ? std::span{parms.Gains.Target}
                    : std::span{SilentTarget};
                if(mFlags.test(VoiceHasNfc))
                    DoNfcMix(samples, mDirect.Buffer, parms, targetGains, counter, outPos, device);
                else
                    MixSamples(samples, mDirect.Buffer, parms.Gains.Current, targetGains, counter,
                        outPos);
            }
        }

        for(auto const send : std::views::iota(0_u32, numSends))
        {
            if(mSend[send].Buffer.empty())
                continue;

            auto &parms = chandata->mWetParams[send];
            auto const samples = DoFilters(parms.LowPass, parms.HighPass, FilterBuf, samplespan,
                mSend[send].FilterActive);

            auto const targetGains = (vstate == Playing) ? std::span{parms.Gains.Target}
                : std::span{SilentTarget}.first<MaxAmbiChannels>();
            MixSamples(samples, mSend[send].Buffer, parms.Gains.Current, targetGains, counter,
                outPos);
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
    bufPosFrac += increment*samplesToMix;
    auto const samplesDone = bufPosFrac >> MixerFracBits;
    bufPosInt = al::add_sat(bufPosInt, gsl::narrow_cast<i32>(samplesDone));
    bufPosFrac &= MixerFracMask;

    auto buffers_done = 0_u32;
    if(bufferListItem && bufPosInt > 0) [[likely]]
    {
        if(mFlags.test(VoiceIsStatic))
        {
            if(bufferLoopItem)
            {
                /* Handle looping static source */
                auto const LoopStart = bufferListItem->mLoopStart;
                auto const LoopEnd = bufferListItem->mLoopEnd;
                if(auto DataPosUInt = gsl::narrow_cast<u32>(bufPosInt); DataPosUInt >= LoopEnd)
                {
                    Expects(LoopEnd > LoopStart);
                    DataPosUInt = ((DataPosUInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                    bufPosInt = gsl::narrow_cast<i32>(DataPosUInt);
                }
            }
            else
            {
                /* Handle non-looping static source */
                if(gsl::narrow_cast<u32>(bufPosInt) >= bufferListItem->mSampleLen)
                    bufferListItem = nullptr;
            }
        }
        else if(mFlags.test(VoiceIsCallback))
        {
            /* Handle callback buffer source */
            auto const endOffset = mCallbackBlockOffset
                + std::min(samplesDone, gsl::narrow_cast<u32>(bufPosInt));
            auto const blocksDone = endOffset / mSamplesPerBlock;
            if(blocksDone == 0)
                mCallbackBlockOffset = endOffset;
            else if(blocksDone < mNumCallbackBlocks)
            {
                auto const byteOffset = blocksDone * usize{mBytesPerBlock};
                auto const byteEnd = mNumCallbackBlocks * usize{mBytesPerBlock};
                auto const data = std::visit([](auto &splspan)
                { return std::as_writable_bytes(splspan); }, bufferListItem->mSamples);
                std::ranges::copy(data | std::views::take(byteEnd) | std::views::drop(byteOffset),
                    data.begin());
                mNumCallbackBlocks -= blocksDone;
                mCallbackBlockOffset = endOffset - blocksDone*mSamplesPerBlock;
            }
            else
            {
                bufferListItem = nullptr;
                mNumCallbackBlocks = 0;
                mCallbackBlockOffset = 0;
            }
        }
        else
        {
            /* Handle streaming source */
            do {
                if(bufferListItem->mSampleLen > gsl::narrow_cast<u32>(bufPosInt))
                    break;

                bufPosInt -= gsl::narrow_cast<i32>(bufferListItem->mSampleLen);

                ++buffers_done;
                bufferListItem = bufferListItem->mNext.load(std::memory_order_relaxed);
                if(!bufferListItem) bufferListItem = bufferLoopItem;
            } while(bufferListItem);
        }
    }

    /* Capture the source ID in case it gets reset for stopping. */
    auto const sourceID = mSourceID.load(std::memory_order_relaxed);

    /* Update voice info */
    mPosition.store(bufPosInt, std::memory_order_relaxed);
    mPositionFrac.store(bufPosFrac, std::memory_order_relaxed);
    mCurrentBuffer.store(bufferListItem, std::memory_order_release);
    if(!bufferListItem)
    {
        mLoopBuffer.store(nullptr, std::memory_order_relaxed);
        mSourceID.store(0_u32, std::memory_order_release);
    }

    /* Send any events now, after the position/buffer info was updated. */
    auto const enabledevt = context->mEnabledEvts.load(std::memory_order_acquire);
    if(buffers_done > 0 && enabledevt.test(al::to_underlying(AsyncEnableBits::BufferCompleted)))
    {
        auto *ring = context->mAsyncEvents.get();
        if(auto const evt_vec = ring->getWriteVector(); !evt_vec[0].empty())
        {
            auto &evt = InitAsyncEvent<AsyncBufferCompleteEvent>(evt_vec[0].front());
            evt.mId = sourceID;
            evt.mCount = buffers_done;
            ring->writeAdvance(1);
        }
    }

    if(!bufferListItem)
    {
        /* If the voice just ended, set it to Stopping so the next render
         * ensures any residual noise fades to 0 amplitude.
         */
        mPlayState.store(Stopping, std::memory_order_release);
        if(enabledevt.test(al::to_underlying(AsyncEnableBits::SourceState)))
            SendSourceStoppedEvent(context, sourceID);
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
    auto num_channels = (mFmtChannels == FmtMono) ? 2_u32
        : (mFmtChannels == FmtUHJ2 || mFmtChannels == FmtSuperStereo) ? 3_u32
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
    static constexpr auto init_decoder = []<typename T>(T arg [[maybe_unused]])
        -> std::pair<std::unique_ptr<DecoderBase>, u32>
    {
        using decoder_t = T::decoder_t;
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
            / gsl::narrow_cast<f32>(device->mSampleRate)};
        std::ranges::for_each(mChans, [splitter,device](ChannelData &chandata)
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
        const auto ordersSpan = Is2DAmbisonic(mFmtChannels)
            ? std::span<u8 const>{AmbiIndex::OrderFrom2DChannel}
            : std::span<u8 const>{AmbiIndex::OrderFromChannel};
        const auto scales = AmbiScale::GetHFOrderScales(mAmbiOrder, device->mAmbiOrder,
            device->m2DMixing);

        const auto splitter = BandSplitter{device->mXOverFreq
            / gsl::narrow_cast<f32>(device->mSampleRate)};
        std::ignore = std::ranges::mismatch(mChans, ordersSpan,
            [&scales,splitter,device](ChannelData &chandata, u8 const scaleidx)
        {
            chandata.mAmbiHFScale = scales[scaleidx.c_val];
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
        std::ranges::for_each(mChans, [device](ChannelData &chandata)
        {
            chandata.mDryParams = DirectParams{};
            chandata.mDryParams.NFCtrlFilter = device->mNFCtrlFilter;
            std::ranges::fill(chandata.mWetParams | std::views::take(device->NumAuxSends),
                SendParams{});
        });
        mFlags.reset(VoiceIsAmbisonic);
    }
}
