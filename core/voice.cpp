
#include "config.h"

#include "voice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <climits>
#include <cstdint>
#include <iterator>
#include <memory>
#include <new>
#include <stdlib.h>
#include <utility>
#include <vector>

#include "albyte.h"
#include "alnumeric.h"
#include "aloptional.h"
#include "alspan.h"
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
#ifdef HAVE_SSE
struct SSETag;
#endif
#ifdef HAVE_NEON
struct NEONTag;
#endif


static_assert(!(sizeof(DeviceBase::MixerBufferLine)&15),
    "DeviceBase::MixerBufferLine must be a multiple of 16 bytes");
static_assert(!(MaxResamplerEdge&3), "MaxResamplerEdge is not a multiple of 4");

static_assert((BufferLineSize-1)/MaxPitch > 0, "MaxPitch is too large for BufferLineSize!");
static_assert((INT_MAX>>MixerFracBits)/MaxPitch > BufferLineSize,
    "MaxPitch and/or BufferLineSize are too large for MixerFracBits!");

Resampler ResamplerDefault{Resampler::Cubic};

namespace {

using uint = unsigned int;
using namespace std::chrono;

using HrtfMixerFunc = void(*)(const float *InSamples, float2 *AccumSamples, const uint IrSize,
    const MixHrtfFilter *hrtfparams, const size_t BufferSize);
using HrtfMixerBlendFunc = void(*)(const float *InSamples, float2 *AccumSamples,
    const uint IrSize, const HrtfFilter *oldparams, const MixHrtfFilter *newparams,
    const size_t BufferSize);

HrtfMixerFunc MixHrtfSamples{MixHrtf_<CTag>};
HrtfMixerBlendFunc MixHrtfBlendSamples{MixHrtfBlend_<CTag>};

inline MixerOutFunc SelectMixer()
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return Mix_<NEONTag>;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return Mix_<SSETag>;
#endif
    return Mix_<CTag>;
}

inline MixerOneFunc SelectMixerOne()
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return Mix_<NEONTag>;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return Mix_<SSETag>;
#endif
    return Mix_<CTag>;
}

inline HrtfMixerFunc SelectHrtfMixer()
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtf_<NEONTag>;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtf_<SSETag>;
#endif
    return MixHrtf_<CTag>;
}

inline HrtfMixerBlendFunc SelectHrtfBlendMixer()
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixHrtfBlend_<NEONTag>;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixHrtfBlend_<SSETag>;
#endif
    return MixHrtfBlend_<CTag>;
}

} // namespace

void Voice::InitMixer(al::optional<std::string> resampler)
{
    if(resampler)
    {
        struct ResamplerEntry {
            const char name[16];
            const Resampler resampler;
        };
        constexpr ResamplerEntry ResamplerList[]{
            { "none", Resampler::Point },
            { "point", Resampler::Point },
            { "linear", Resampler::Linear },
            { "cubic", Resampler::Cubic },
            { "bsinc12", Resampler::BSinc12 },
            { "fast_bsinc12", Resampler::FastBSinc12 },
            { "bsinc24", Resampler::BSinc24 },
            { "fast_bsinc24", Resampler::FastBSinc24 },
        };

        const char *str{resampler->c_str()};
        if(al::strcasecmp(str, "bsinc") == 0)
        {
            WARN("Resampler option \"%s\" is deprecated, using bsinc12\n", str);
            str = "bsinc12";
        }
        else if(al::strcasecmp(str, "sinc4") == 0 || al::strcasecmp(str, "sinc8") == 0)
        {
            WARN("Resampler option \"%s\" is deprecated, using cubic\n", str);
            str = "cubic";
        }

        auto iter = std::find_if(std::begin(ResamplerList), std::end(ResamplerList),
            [str](const ResamplerEntry &entry) -> bool
            { return al::strcasecmp(str, entry.name) == 0; });
        if(iter == std::end(ResamplerList))
            ERR("Invalid resampler: %s\n", str);
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
constexpr int IMAStep_size[89] = {
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
constexpr int IMA4Codeword[16] = {
    1, 3, 5, 7, 9, 11, 13, 15,
   -1,-3,-5,-7,-9,-11,-13,-15,
};

/* IMA4 ADPCM Step index adjust decode table */
constexpr int IMA4Index_adjust[16] = {
   -1,-1,-1,-1, 2, 4, 6, 8,
   -1,-1,-1,-1, 2, 4, 6, 8
};

/* MSADPCM Adaption table */
constexpr int MSADPCMAdaption[16] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};

/* MSADPCM Adaption Coefficient tables */
constexpr int MSADPCMAdaptionCoeff[7][2] = {
    { 256,    0 },
    { 512, -256 },
    {   0,    0 },
    { 192,   64 },
    { 240,    0 },
    { 460, -208 },
    { 392, -232 }
};


void SendSourceStoppedEvent(ContextBase *context, uint id)
{
    RingBuffer *ring{context->mAsyncEvents.get()};
    auto evt_vec = ring->getWriteVector();
    if(evt_vec.first.len < 1) return;

    AsyncEvent *evt{al::construct_at(reinterpret_cast<AsyncEvent*>(evt_vec.first.buf),
        AsyncEvent::SourceStateChange)};
    evt->u.srcstate.id = id;
    evt->u.srcstate.state = AsyncEvent::SrcState::Stop;

    ring->writeAdvance(1);
}


const float *DoFilters(BiquadFilter &lpfilter, BiquadFilter &hpfilter, float *dst,
    const al::span<const float> src, int type)
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
        return dst;
    case AF_HighPass:
        lpfilter.clear();
        hpfilter.process(src, dst);
        return dst;

    case AF_BandPass:
        DualBiquad{lpfilter, hpfilter}.process(src, dst);
        return dst;
    }
    return src.data();
}


template<FmtType Type>
inline void LoadSamples(float *RESTRICT dstSamples, const al::byte *src, const size_t srcChan,
    const size_t srcOffset, const size_t srcStep, const size_t /*samplesPerBlock*/,
    const size_t samplesToLoad) noexcept
{
    constexpr size_t sampleSize{sizeof(typename al::FmtTypeTraits<Type>::Type)};
    auto s = src + (srcOffset*srcStep + srcChan)*sampleSize;

    al::LoadSampleArray<Type>(dstSamples, s, srcStep, samplesToLoad);
}

template<>
inline void LoadSamples<FmtIMA4>(float *RESTRICT dstSamples, const al::byte *src,
    const size_t srcChan, const size_t srcOffset, const size_t srcStep,
    const size_t samplesPerBlock, const size_t samplesToLoad) noexcept
{
    const size_t blockBytes{((samplesPerBlock-1)/2 + 4)*srcStep};

    /* Skip to the ADPCM block containing the srcOffset sample. */
    src += srcOffset/samplesPerBlock*blockBytes;
    /* Calculate how many samples need to be skipped in the block. */
    size_t skip{srcOffset % samplesPerBlock};

    /* NOTE: This could probably be optimized better. */
    size_t wrote{0};
    do {
        /* Each IMA4 block starts with a signed 16-bit sample, and a signed
         * 16-bit table index. The table index needs to be clamped.
         */
        int sample{src[srcChan*4] | (src[srcChan*4 + 1] << 8)};
        int index{src[srcChan*4 + 2] | (src[srcChan*4 + 3] << 8)};

        sample = (sample^0x8000) - 32768;
        index = clampi((index^0x8000) - 32768, 0, al::size(IMAStep_size)-1);

        if(skip == 0)
        {
            dstSamples[wrote++] = static_cast<float>(sample) / 32768.0f;
            if(wrote == samplesToLoad) return;
        }
        else
            --skip;

        auto decode_sample = [&sample,&index](const uint nibble)
        {
            sample += IMA4Codeword[nibble] * IMAStep_size[index] / 8;
            sample = clampi(sample, -32768, 32767);

            index += IMA4Index_adjust[nibble];
            index = clampi(index, 0, al::size(IMAStep_size)-1);

            return sample;
        };

        /* The rest of the block is arranged as a series of nibbles, contained
         * in 4 *bytes* per channel interleaved. So every 8 nibbles we need to
         * skip 4 bytes per channel to get the next nibbles for this channel.
         *
         * First, decode the samples that we need to skip in the block (will
         * always be less than the block size). They need to be decoded despite
         * being ignored for proper state on the remaining samples.
         */
        const al::byte *nibbleData{src + (srcStep+srcChan)*4};
        size_t nibbleOffset{0};
        const size_t startOffset{skip + 1};
        for(;skip;--skip)
        {
            const size_t byteShift{(nibbleOffset&1) * 4};
            const size_t wordOffset{(nibbleOffset>>1) & ~size_t{3}};
            const size_t byteOffset{wordOffset*srcStep + ((nibbleOffset>>1)&3u)};
            ++nibbleOffset;

            std::ignore = decode_sample((nibbleData[byteOffset]>>byteShift) & 15u);
        }

        /* Second, decode the rest of the block and write to the output, until
         * the end of the block or the end of output.
         */
        const size_t todo{minz(samplesPerBlock-startOffset, samplesToLoad-wrote)};
        for(size_t i{0};i < todo;++i)
        {
            const size_t byteShift{(nibbleOffset&1) * 4};
            const size_t wordOffset{(nibbleOffset>>1) & ~size_t{3}};
            const size_t byteOffset{wordOffset*srcStep + ((nibbleOffset>>1)&3u)};
            ++nibbleOffset;

            const int result{decode_sample((nibbleData[byteOffset]>>byteShift) & 15u)};
            dstSamples[wrote++] = static_cast<float>(result) / 32768.0f;
        }
        if(wrote == samplesToLoad)
            return;

        src += blockBytes;
    } while(true);
}

template<>
inline void LoadSamples<FmtMSADPCM>(float *RESTRICT dstSamples, const al::byte *src,
    const size_t srcChan, const size_t srcOffset, const size_t srcStep,
    const size_t samplesPerBlock, const size_t samplesToLoad) noexcept
{
    const size_t blockBytes{((samplesPerBlock-2)/2 + 7)*srcStep};

    src += srcOffset/samplesPerBlock*blockBytes;
    size_t skip{srcOffset % samplesPerBlock};

    size_t wrote{0};
    do {
        /* Each MS ADPCM block starts with an 8-bit block predictor, used to
         * dictate how the two sample history values are mixed with the decoded
         * sample, and an initial signed 16-bit delta value which scales the
         * nibble sample value. This is followed by the two initial 16-bit
         * sample history values.
         */
        const al::byte *input{src};
        const uint8_t blockpred{std::min(input[srcChan], uint8_t{6})};
        input += srcStep;
        int delta{input[2*srcChan + 0] | (input[2*srcChan + 1] << 8)};
        input += srcStep*2;

        int sampleHistory[2]{};
        sampleHistory[0] = input[2*srcChan + 0] | (input[2*srcChan + 1]<<8);
        input += srcStep*2;
        sampleHistory[1] = input[2*srcChan + 0] | (input[2*srcChan + 1]<<8);
        input += srcStep*2;

        const auto coeffs = al::as_span(MSADPCMAdaptionCoeff[blockpred]);
        delta = (delta^0x8000) - 32768;
        sampleHistory[0] = (sampleHistory[0]^0x8000) - 32768;
        sampleHistory[1] = (sampleHistory[1]^0x8000) - 32768;

        /* The second history sample is "older", so it's the first to be
         * written out.
         */
        if(skip == 0)
        {
            dstSamples[wrote++] = static_cast<float>(sampleHistory[1]) / 32768.0f;
            if(wrote == samplesToLoad) return;
            dstSamples[wrote++] = static_cast<float>(sampleHistory[0]) / 32768.0f;
            if(wrote == samplesToLoad) return;
        }
        else if(skip == 1)
        {
            --skip;
            dstSamples[wrote++] = static_cast<float>(sampleHistory[0]) / 32768.0f;
            if(wrote == samplesToLoad) return;
        }
        else
            skip -= 2;

        auto decode_sample = [&sampleHistory,&delta,coeffs](const int nibble)
        {
            int pred{(sampleHistory[0]*coeffs[0] + sampleHistory[1]*coeffs[1]) / 256};
            pred += ((nibble^0x08) - 0x08) * delta;
            pred  = clampi(pred, -32768, 32767);

            sampleHistory[1] = sampleHistory[0];
            sampleHistory[0] = pred;

            delta = (MSADPCMAdaption[nibble] * delta) / 256;
            delta = maxi(16, delta);

            return pred;
        };

        /* The rest of the block is a series of nibbles, interleaved per-
         * channel. First, skip samples.
         */
        const size_t startOffset{skip + 2};
        size_t nibbleOffset{srcChan};
        for(;skip;--skip)
        {
            const size_t byteOffset{nibbleOffset>>1};
            const size_t byteShift{((nibbleOffset&1)^1) * 4};
            nibbleOffset += srcStep;

            std::ignore = decode_sample((input[byteOffset]>>byteShift) & 15);
        }

        /* Now decode the rest of the block, until the end of the block or the
         * dst buffer is filled.
         */
        const size_t todo{minz(samplesPerBlock-startOffset, samplesToLoad-wrote)};
        for(size_t j{0};j < todo;++j)
        {
            const size_t byteOffset{nibbleOffset>>1};
            const size_t byteShift{((nibbleOffset&1)^1) * 4};
            nibbleOffset += srcStep;

            const int sample{decode_sample((input[byteOffset]>>byteShift) & 15)};
            dstSamples[wrote++] = static_cast<float>(sample) / 32768.0f;
        }
        if(wrote == samplesToLoad)
            return;

        src += blockBytes;
    } while(true);
}

void LoadSamples(float *dstSamples, const al::byte *src, const size_t srcChan,
    const size_t srcOffset, const FmtType srcType, const size_t srcStep,
    const size_t samplesPerBlock, const size_t samplesToLoad) noexcept
{
#define HANDLE_FMT(T) case T:                                                 \
    LoadSamples<T>(dstSamples, src, srcChan, srcOffset, srcStep,              \
        samplesPerBlock, samplesToLoad);                                      \
    break

    switch(srcType)
    {
    HANDLE_FMT(FmtUByte);
    HANDLE_FMT(FmtShort);
    HANDLE_FMT(FmtFloat);
    HANDLE_FMT(FmtDouble);
    HANDLE_FMT(FmtMulaw);
    HANDLE_FMT(FmtAlaw);
    HANDLE_FMT(FmtIMA4);
    HANDLE_FMT(FmtMSADPCM);
    }
#undef HANDLE_FMT
}

void LoadBufferStatic(VoiceBufferItem *buffer, VoiceBufferItem *bufferLoopItem,
    const size_t dataPosInt, const FmtType sampleType, const size_t srcChannel,
    const size_t srcStep, size_t samplesLoaded, const size_t samplesToLoad,
    float *voiceSamples)
{
    if(!bufferLoopItem)
    {
        /* Load what's left to play from the buffer */
        if(buffer->mSampleLen > dataPosInt) LIKELY
        {
            const size_t buffer_remaining{buffer->mSampleLen - dataPosInt};
            const size_t remaining{minz(samplesToLoad-samplesLoaded, buffer_remaining)};
            LoadSamples(voiceSamples+samplesLoaded, buffer->mSamples, srcChannel, dataPosInt,
                sampleType, srcStep, buffer->mBlockAlign, remaining);
            samplesLoaded += remaining;
        }

        if(const size_t toFill{samplesToLoad - samplesLoaded})
        {
            auto srcsamples = voiceSamples + samplesLoaded;
            std::fill_n(srcsamples, toFill, *(srcsamples-1));
        }
    }
    else
    {
        const size_t loopStart{buffer->mLoopStart};
        const size_t loopEnd{buffer->mLoopEnd};
        ASSUME(loopEnd > loopStart);

        const size_t intPos{(dataPosInt < loopEnd) ? dataPosInt
            : (((dataPosInt-loopStart)%(loopEnd-loopStart)) + loopStart)};

        /* Load what's left of this loop iteration */
        const size_t remaining{minz(samplesToLoad-samplesLoaded, loopEnd-dataPosInt)};
        LoadSamples(voiceSamples+samplesLoaded, buffer->mSamples, srcChannel, intPos, sampleType,
            srcStep, buffer->mBlockAlign, remaining);
        samplesLoaded += remaining;

        /* Load repeats of the loop to fill the buffer. */
        const size_t loopSize{loopEnd - loopStart};
        while(const size_t toFill{minz(samplesToLoad - samplesLoaded, loopSize)})
        {
            LoadSamples(voiceSamples+samplesLoaded, buffer->mSamples, srcChannel, loopStart,
                sampleType, srcStep, buffer->mBlockAlign, toFill);
            samplesLoaded += toFill;
        }
    }
}

void LoadBufferCallback(VoiceBufferItem *buffer, const size_t dataPosInt,
    const size_t numCallbackSamples, const FmtType sampleType, const size_t srcChannel,
    const size_t srcStep, size_t samplesLoaded, const size_t samplesToLoad, float *voiceSamples)
{
    /* Load what's left to play from the buffer */
    if(numCallbackSamples > dataPosInt) LIKELY
    {
        const size_t remaining{minz(samplesToLoad-samplesLoaded, numCallbackSamples-dataPosInt)};
        LoadSamples(voiceSamples+samplesLoaded, buffer->mSamples, srcChannel, dataPosInt,
            sampleType, srcStep, buffer->mBlockAlign, remaining);
        samplesLoaded += remaining;
    }

    if(const size_t toFill{samplesToLoad - samplesLoaded})
    {
        auto srcsamples = voiceSamples + samplesLoaded;
        std::fill_n(srcsamples, toFill, *(srcsamples-1));
    }
}

void LoadBufferQueue(VoiceBufferItem *buffer, VoiceBufferItem *bufferLoopItem,
    size_t dataPosInt, const FmtType sampleType, const size_t srcChannel,
    const size_t srcStep, size_t samplesLoaded, const size_t samplesToLoad,
    float *voiceSamples)
{
    /* Crawl the buffer queue to fill in the temp buffer */
    while(buffer && samplesLoaded != samplesToLoad)
    {
        if(dataPosInt >= buffer->mSampleLen)
        {
            dataPosInt -= buffer->mSampleLen;
            buffer = buffer->mNext.load(std::memory_order_acquire);
            if(!buffer) buffer = bufferLoopItem;
            continue;
        }

        const size_t remaining{minz(samplesToLoad-samplesLoaded, buffer->mSampleLen-dataPosInt)};
        LoadSamples(voiceSamples+samplesLoaded, buffer->mSamples, srcChannel, dataPosInt,
            sampleType, srcStep, buffer->mBlockAlign, remaining);

        samplesLoaded += remaining;
        if(samplesLoaded == samplesToLoad)
            break;

        dataPosInt = 0;
        buffer = buffer->mNext.load(std::memory_order_acquire);
        if(!buffer) buffer = bufferLoopItem;
    }
    if(const size_t toFill{samplesToLoad - samplesLoaded})
    {
        auto srcsamples = voiceSamples + samplesLoaded;
        std::fill_n(srcsamples, toFill, *(srcsamples-1));
    }
}


void DoHrtfMix(const float *samples, const uint DstBufferSize, DirectParams &parms,
    const float TargetGain, const uint Counter, uint OutPos, const bool IsPlaying,
    DeviceBase *Device)
{
    const uint IrSize{Device->mIrSize};
    auto &HrtfSamples = Device->HrtfSourceData;
    auto &AccumSamples = Device->HrtfAccumData;

    /* Copy the HRTF history and new input samples into a temp buffer. */
    auto src_iter = std::copy(parms.Hrtf.History.begin(), parms.Hrtf.History.end(),
        std::begin(HrtfSamples));
    std::copy_n(samples, DstBufferSize, src_iter);
    /* Copy the last used samples back into the history buffer for later. */
    if(IsPlaying) LIKELY
        std::copy_n(std::begin(HrtfSamples) + DstBufferSize, parms.Hrtf.History.size(),
            parms.Hrtf.History.begin());

    /* If fading and this is the first mixing pass, fade between the IRs. */
    uint fademix{0u};
    if(Counter && OutPos == 0)
    {
        fademix = minu(DstBufferSize, Counter);

        float gain{TargetGain};

        /* The new coefficients need to fade in completely since they're
         * replacing the old ones. To keep the gain fading consistent,
         * interpolate between the old and new target gains given how much of
         * the fade time this mix handles.
         */
        if(Counter > fademix)
        {
            const float a{static_cast<float>(fademix) / static_cast<float>(Counter)};
            gain = lerpf(parms.Hrtf.Old.Gain, TargetGain, a);
        }

        MixHrtfFilter hrtfparams{
            parms.Hrtf.Target.Coeffs,
            parms.Hrtf.Target.Delay,
            0.0f, gain / static_cast<float>(fademix)};
        MixHrtfBlendSamples(HrtfSamples, AccumSamples+OutPos, IrSize, &parms.Hrtf.Old, &hrtfparams,
            fademix);

        /* Update the old parameters with the result. */
        parms.Hrtf.Old = parms.Hrtf.Target;
        parms.Hrtf.Old.Gain = gain;
        OutPos += fademix;
    }

    if(fademix < DstBufferSize)
    {
        const uint todo{DstBufferSize - fademix};
        float gain{TargetGain};

        /* Interpolate the target gain if the gain fading lasts longer than
         * this mix.
         */
        if(Counter > DstBufferSize)
        {
            const float a{static_cast<float>(todo) / static_cast<float>(Counter-fademix)};
            gain = lerpf(parms.Hrtf.Old.Gain, TargetGain, a);
        }

        MixHrtfFilter hrtfparams{
            parms.Hrtf.Target.Coeffs,
            parms.Hrtf.Target.Delay,
            parms.Hrtf.Old.Gain,
            (gain - parms.Hrtf.Old.Gain) / static_cast<float>(todo)};
        MixHrtfSamples(HrtfSamples+fademix, AccumSamples+OutPos, IrSize, &hrtfparams, todo);

        /* Store the now-current gain for next time. */
        parms.Hrtf.Old.Gain = gain;
    }
}

void DoNfcMix(const al::span<const float> samples, FloatBufferLine *OutBuffer, DirectParams &parms,
    const float *TargetGains, const uint Counter, const uint OutPos, DeviceBase *Device)
{
    using FilterProc = void (NfcFilter::*)(const al::span<const float>, float*);
    static constexpr FilterProc NfcProcess[MaxAmbiOrder+1]{
        nullptr, &NfcFilter::process1, &NfcFilter::process2, &NfcFilter::process3};

    float *CurrentGains{parms.Gains.Current.data()};
    MixSamples(samples, {OutBuffer, 1u}, CurrentGains, TargetGains, Counter, OutPos);
    ++OutBuffer;
    ++CurrentGains;
    ++TargetGains;

    const al::span<float> nfcsamples{Device->NfcSampleData, samples.size()};
    size_t order{1};
    while(const size_t chancount{Device->NumChannelsPerOrder[order]})
    {
        (parms.NFCtrlFilter.*NfcProcess[order])(samples, nfcsamples.data());
        MixSamples(nfcsamples, {OutBuffer, chancount}, CurrentGains, TargetGains, Counter, OutPos);
        OutBuffer += chancount;
        CurrentGains += chancount;
        TargetGains += chancount;
        if(++order == MaxAmbiOrder+1)
            break;
    }
}

} // namespace

void Voice::mix(const State vstate, ContextBase *Context, const nanoseconds deviceTime,
    const uint SamplesToDo)
{
    static constexpr std::array<float,MAX_OUTPUT_CHANNELS> SilentTarget{};

    ASSUME(SamplesToDo > 0);

    DeviceBase *Device{Context->mDevice};
    const uint NumSends{Device->NumAuxSends};

    /* Get voice info */
    int DataPosInt{mPosition.load(std::memory_order_relaxed)};
    uint DataPosFrac{mPositionFrac.load(std::memory_order_relaxed)};
    VoiceBufferItem *BufferListItem{mCurrentBuffer.load(std::memory_order_relaxed)};
    VoiceBufferItem *BufferLoopItem{mLoopBuffer.load(std::memory_order_relaxed)};
    const uint increment{mStep};
    if(increment < 1) UNLIKELY
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
        if(DataPosInt >= 0 && static_cast<uint>(DataPosInt) >= BufferListItem->mLoopEnd)
            BufferLoopItem = nullptr;
    }

    uint OutPos{0u};

    /* Check if we're doing a delayed start, and we start in this update. */
    if(mStartTime > deviceTime) UNLIKELY
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
         *
         * Round the start position to a multiple of 4, which some mixers want.
         * This makes the start time accurate to 4 samples. This could be made
         * sample-accurate by forcing non-SIMD functions on the first run.
         */
        seconds::rep sampleOffset{duration_cast<seconds>(diff * Device->Frequency).count()};
        sampleOffset = (sampleOffset+2) & ~seconds::rep{3};
        if(sampleOffset >= SamplesToDo)
            return;

        OutPos = static_cast<uint>(sampleOffset);
    }

    /* Calculate the number of samples to mix, and the number of (resampled)
     * samples that need to be loaded (mixing samples and decoder padding).
     */
    const uint samplesToMix{SamplesToDo - OutPos};
    const uint samplesToLoad{samplesToMix + mDecoderPadding};

    /* Get a span of pointers to hold the floating point, deinterlaced,
     * resampled buffer data to be mixed.
     */
    std::array<float*,DeviceBase::MixerChannelsMax> SamplePointers;
    const al::span<float*> MixingSamples{SamplePointers.data(), mChans.size()};
    auto get_bufferline = [](DeviceBase::MixerBufferLine &bufline) noexcept -> float*
    { return bufline.data(); };
    std::transform(Device->mSampleData.end() - mChans.size(), Device->mSampleData.end(),
        MixingSamples.begin(), get_bufferline);

    /* If there's a matching sample step and no phase offset, use a simple copy
     * for resampling.
     */
    const ResamplerFunc Resample{(increment == MixerFracOne && DataPosFrac == 0)
        ? ResamplerFunc{[](const InterpState*, const float *RESTRICT src, uint, const uint,
            const al::span<float> dst) { std::copy_n(src, dst.size(), dst.begin()); }}
        : mResampler};

    /* UHJ2 and SuperStereo only have 2 buffer channels, but 3 mixing channels
     * (3rd channel is generated from decoding).
     */
    const size_t realChannels{(mFmtChannels == FmtUHJ2 || mFmtChannels == FmtSuperStereo) ? 2u
        : MixingSamples.size()};
    for(size_t chan{0};chan < realChannels;++chan)
    {
        using ResBufType = decltype(DeviceBase::mResampleData);
        static constexpr uint srcSizeMax{static_cast<uint>(ResBufType{}.size()-MaxResamplerEdge)};

        const auto prevSamples = al::as_span(mPrevSamples[chan]);
        const auto resampleBuffer = std::copy(prevSamples.cbegin(), prevSamples.cend(),
            Device->mResampleData.begin()) - MaxResamplerEdge;
        int intPos{DataPosInt};
        uint fracPos{DataPosFrac};

        /* Load samples for this channel from the available buffer(s), with
         * resampling.
         */
        for(uint samplesLoaded{0};samplesLoaded < samplesToLoad;)
        {
            /* Calculate the number of dst samples that can be loaded this
             * iteration, given the available resampler buffer size, and the
             * number of src samples that are needed to load it.
             */
            auto calc_buffer_sizes = [fracPos,increment](uint dstBufferSize)
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
                const bool ext{increment <= MixerFracOne};
                uint64_t dataSize64{dstBufferSize - ext};
                dataSize64 = (dataSize64*increment + fracPos) >> MixerFracBits;
                /* Also include resampler padding. */
                dataSize64 += ext + MaxResamplerEdge;

                if(dataSize64 <= srcSizeMax)
                    return std::make_pair(dstBufferSize, static_cast<uint>(dataSize64));

                /* If the source size got saturated, we can't fill the desired
                 * dst size. Figure out how many dst samples we can fill.
                 */
                dataSize64 = srcSizeMax - MaxResamplerEdge;
                dataSize64 = ((dataSize64<<MixerFracBits) - fracPos) / increment;
                if(dataSize64 < dstBufferSize)
                {
                    /* Some resamplers require the destination being 16-byte
                     * aligned, so limit to a multiple of 4 samples to maintain
                     * alignment if we need to do another iteration after this.
                     */
                    dstBufferSize = static_cast<uint>(dataSize64) & ~3u;
                }
                return std::make_pair(dstBufferSize, srcSizeMax);
            };
            const auto bufferSizes = calc_buffer_sizes(samplesToLoad - samplesLoaded);
            const auto dstBufferSize = bufferSizes.first;
            const auto srcBufferSize = bufferSizes.second;

            /* Load the necessary samples from the given buffer(s). */
            if(!BufferListItem)
            {
                const uint avail{minu(srcBufferSize, MaxResamplerEdge)};
                const uint tofill{maxu(srcBufferSize, MaxResamplerEdge)};

                /* When loading from a voice that ended prematurely, only take
                 * the samples that get closest to 0 amplitude. This helps
                 * certain sounds fade out better.
                 */
                auto abs_lt = [](const float lhs, const float rhs) noexcept -> bool
                { return std::abs(lhs) < std::abs(rhs); };
                auto srciter = std::min_element(resampleBuffer, resampleBuffer+avail, abs_lt);

                std::fill(srciter+1, resampleBuffer+tofill, *srciter);
            }
            else
            {
                size_t srcSampleDelay{0};
                if(intPos < 0) UNLIKELY
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
                        std::fill_n(MixingSamples[chan]+samplesLoaded, dstBufferSize, 0.0f);
                        std::fill_n(resampleBuffer, srcBufferSize, 0.0f);
                        goto skip_resample;
                    }

                    std::fill_n(resampleBuffer, srcSampleDelay, 0.0f);
                }
                const uint uintPos{static_cast<uint>(maxi(intPos, 0))};

                if(mFlags.test(VoiceIsStatic))
                    LoadBufferStatic(BufferListItem, BufferLoopItem, uintPos, mFmtType, chan,
                        mFrameStep, srcSampleDelay, srcBufferSize, al::to_address(resampleBuffer));
                else if(mFlags.test(VoiceIsCallback))
                {
                    const uint callbackBase{mCallbackBlockBase * mSamplesPerBlock};
                    const size_t bufferOffset{uintPos - callbackBase};
                    const size_t needSamples{bufferOffset + srcBufferSize - srcSampleDelay};
                    const size_t needBlocks{(needSamples + mSamplesPerBlock-1) / mSamplesPerBlock};
                    if(!mFlags.test(VoiceCallbackStopped) && needBlocks > mNumCallbackBlocks)
                    {
                        const size_t byteOffset{mNumCallbackBlocks*mBytesPerBlock};
                        const size_t needBytes{(needBlocks-mNumCallbackBlocks)*mBytesPerBlock};

                        const int gotBytes{BufferListItem->mCallback(BufferListItem->mUserData,
                            &BufferListItem->mSamples[byteOffset], static_cast<int>(needBytes))};
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
                    const size_t numSamples{uint{mNumCallbackBlocks} * mSamplesPerBlock};
                    LoadBufferCallback(BufferListItem, bufferOffset, numSamples, mFmtType, chan,
                        mFrameStep, srcSampleDelay, srcBufferSize, al::to_address(resampleBuffer));
                }
                else
                    LoadBufferQueue(BufferListItem, BufferLoopItem, uintPos, mFmtType, chan,
                        mFrameStep, srcSampleDelay, srcBufferSize, al::to_address(resampleBuffer));
            }

            Resample(&mResampleState, al::to_address(resampleBuffer), fracPos, increment,
                {MixingSamples[chan]+samplesLoaded, dstBufferSize});

            /* Store the last source samples used for next time. */
            if(vstate == Playing) LIKELY
            {
                /* Only store samples for the end of the mix, excluding what
                 * gets loaded for decoder padding.
                 */
                const uint loadEnd{samplesLoaded + dstBufferSize};
                if(samplesToMix > samplesLoaded && samplesToMix <= loadEnd) LIKELY
                {
                    const size_t dstOffset{samplesToMix - samplesLoaded};
                    const size_t srcOffset{(dstOffset*increment + fracPos) >> MixerFracBits};
                    std::copy_n(resampleBuffer-MaxResamplerEdge+srcOffset, prevSamples.size(),
                        prevSamples.begin());
                }
            }

        skip_resample:
            samplesLoaded += dstBufferSize;
            if(samplesLoaded < samplesToLoad)
            {
                fracPos += dstBufferSize*increment;
                const uint srcOffset{fracPos >> MixerFracBits};
                fracPos &= MixerFracMask;
                intPos += srcOffset;

                /* If more samples need to be loaded, copy the back of the
                 * resampleBuffer to the front to reuse it. prevSamples isn't
                 * reliable since it's only updated for the end of the mix.
                 */
                std::copy(resampleBuffer-MaxResamplerEdge+srcOffset,
                    resampleBuffer+MaxResamplerEdge+srcOffset, resampleBuffer-MaxResamplerEdge);
            }
        }
    }
    for(auto &samples : MixingSamples.subspan(realChannels))
        std::fill_n(samples, samplesToLoad, 0.0f);

    if(mDecoder)
        mDecoder->decode(MixingSamples, samplesToMix, (vstate==Playing));

    if(mFlags.test(VoiceIsAmbisonic))
    {
        auto voiceSamples = MixingSamples.begin();
        for(auto &chandata : mChans)
        {
            chandata.mAmbiSplitter.processScale({*voiceSamples, samplesToMix},
                chandata.mAmbiHFScale, chandata.mAmbiLFScale);
            ++voiceSamples;
        }
    }

    const uint Counter{mFlags.test(VoiceIsFading) ? minu(samplesToMix, 64u) : 0u};
    if(!Counter)
    {
        /* No fading, just overwrite the old/current params. */
        for(auto &chandata : mChans)
        {
            {
                DirectParams &parms = chandata.mDryParams;
                if(!mFlags.test(VoiceHasHrtf))
                    parms.Gains.Current = parms.Gains.Target;
                else
                    parms.Hrtf.Old = parms.Hrtf.Target;
            }
            for(uint send{0};send < NumSends;++send)
            {
                if(mSend[send].Buffer.empty())
                    continue;

                SendParams &parms = chandata.mWetParams[send];
                parms.Gains.Current = parms.Gains.Target;
            }
        }
    }

    auto voiceSamples = MixingSamples.begin();
    for(auto &chandata : mChans)
    {
        /* Now filter and mix to the appropriate outputs. */
        const al::span<float,BufferLineSize> FilterBuf{Device->FilteredData};
        {
            DirectParams &parms = chandata.mDryParams;
            const float *samples{DoFilters(parms.LowPass, parms.HighPass, FilterBuf.data(),
                {*voiceSamples, samplesToMix}, mDirect.FilterType)};

            if(mFlags.test(VoiceHasHrtf))
            {
                const float TargetGain{parms.Hrtf.Target.Gain * (vstate == Playing)};
                DoHrtfMix(samples, samplesToMix, parms, TargetGain, Counter, OutPos,
                    (vstate == Playing), Device);
            }
            else
            {
                const float *TargetGains{(vstate == Playing) ? parms.Gains.Target.data()
                    : SilentTarget.data()};
                if(mFlags.test(VoiceHasNfc))
                    DoNfcMix({samples, samplesToMix}, mDirect.Buffer.data(), parms,
                        TargetGains, Counter, OutPos, Device);
                else
                    MixSamples({samples, samplesToMix}, mDirect.Buffer,
                        parms.Gains.Current.data(), TargetGains, Counter, OutPos);
            }
        }

        for(uint send{0};send < NumSends;++send)
        {
            if(mSend[send].Buffer.empty())
                continue;

            SendParams &parms = chandata.mWetParams[send];
            const float *samples{DoFilters(parms.LowPass, parms.HighPass, FilterBuf.data(),
                {*voiceSamples, samplesToMix}, mSend[send].FilterType)};

            const float *TargetGains{(vstate == Playing) ? parms.Gains.Target.data()
                : SilentTarget.data()};
            MixSamples({samples, samplesToMix}, mSend[send].Buffer,
                parms.Gains.Current.data(), TargetGains, Counter, OutPos);
        }

        ++voiceSamples;
    }

    mFlags.set(VoiceIsFading);

    /* Don't update positions and buffers if we were stopping. */
    if(vstate == Stopping) UNLIKELY
    {
        mPlayState.store(Stopped, std::memory_order_release);
        return;
    }

    /* Update voice positions and buffers as needed. */
    DataPosFrac += increment*samplesToMix;
    const uint SrcSamplesDone{DataPosFrac>>MixerFracBits};
    DataPosInt  += SrcSamplesDone;
    DataPosFrac &= MixerFracMask;

    uint buffers_done{0u};
    if(BufferListItem && DataPosInt >= 0) LIKELY
    {
        if(mFlags.test(VoiceIsStatic))
        {
            if(BufferLoopItem)
            {
                /* Handle looping static source */
                const uint LoopStart{BufferListItem->mLoopStart};
                const uint LoopEnd{BufferListItem->mLoopEnd};
                uint DataPosUInt{static_cast<uint>(DataPosInt)};
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
            const uint currentBlock{static_cast<uint>(DataPosInt) / mSamplesPerBlock};
            const uint blocksDone{currentBlock - mCallbackBlockBase};
            if(blocksDone < mNumCallbackBlocks)
            {
                const size_t byteOffset{blocksDone*mBytesPerBlock};
                const size_t byteEnd{mNumCallbackBlocks*mBytesPerBlock};
                al::byte *data{BufferListItem->mSamples};
                std::copy(data+byteOffset, data+byteEnd, data);
                mNumCallbackBlocks -= blocksDone;
                mCallbackBlockBase += blocksDone;
            }
            else
            {
                BufferListItem = nullptr;
                mNumCallbackBlocks = 0;
                mCallbackBlockBase += blocksDone;
            }
        }
        else
        {
            /* Handle streaming source */
            do {
                if(BufferListItem->mSampleLen > static_cast<uint>(DataPosInt))
                    break;

                DataPosInt -= BufferListItem->mSampleLen;

                ++buffers_done;
                BufferListItem = BufferListItem->mNext.load(std::memory_order_relaxed);
                if(!BufferListItem) BufferListItem = BufferLoopItem;
            } while(BufferListItem);
        }
    }

    /* Capture the source ID in case it gets reset for stopping. */
    const uint SourceID{mSourceID.load(std::memory_order_relaxed)};

    /* Update voice info */
    mPosition.store(DataPosInt, std::memory_order_relaxed);
    mPositionFrac.store(DataPosFrac, std::memory_order_relaxed);
    mCurrentBuffer.store(BufferListItem, std::memory_order_relaxed);
    if(!BufferListItem)
    {
        mLoopBuffer.store(nullptr, std::memory_order_relaxed);
        mSourceID.store(0u, std::memory_order_relaxed);
    }
    std::atomic_thread_fence(std::memory_order_release);

    /* Send any events now, after the position/buffer info was updated. */
    const auto enabledevt = Context->mEnabledEvts.load(std::memory_order_acquire);
    if(buffers_done > 0 && enabledevt.test(AsyncEvent::BufferCompleted))
    {
        RingBuffer *ring{Context->mAsyncEvents.get()};
        auto evt_vec = ring->getWriteVector();
        if(evt_vec.first.len > 0)
        {
            AsyncEvent *evt{al::construct_at(reinterpret_cast<AsyncEvent*>(evt_vec.first.buf),
                AsyncEvent::BufferCompleted)};
            evt->u.bufcomp.id = SourceID;
            evt->u.bufcomp.count = buffers_done;
            ring->writeAdvance(1);
        }
    }

    if(!BufferListItem)
    {
        /* If the voice just ended, set it to Stopping so the next render
         * ensures any residual noise fades to 0 amplitude.
         */
        mPlayState.store(Stopping, std::memory_order_release);
        if(enabledevt.test(AsyncEvent::SourceStateChange))
            SendSourceStoppedEvent(Context, SourceID);
    }
}

void Voice::prepare(DeviceBase *device)
{
    /* Even if storing really high order ambisonics, we only mix channels for
     * orders up to the device order. The rest are simply dropped.
     */
    uint num_channels{(mFmtChannels == FmtUHJ2 || mFmtChannels == FmtSuperStereo) ? 3 :
        ChannelsFromFmt(mFmtChannels, minu(mAmbiOrder, device->mAmbiOrder))};
    if(num_channels > device->mSampleData.size()) UNLIKELY
    {
        ERR("Unexpected channel count: %u (limit: %zu, %d:%d)\n", num_channels,
            device->mSampleData.size(), mFmtChannels, mAmbiOrder);
        num_channels = static_cast<uint>(device->mSampleData.size());
    }
    if(mChans.capacity() > 2 && num_channels < mChans.capacity())
    {
        decltype(mChans){}.swap(mChans);
        decltype(mPrevSamples){}.swap(mPrevSamples);
    }
    mChans.reserve(maxu(2, num_channels));
    mChans.resize(num_channels);
    mPrevSamples.reserve(maxu(2, num_channels));
    mPrevSamples.resize(num_channels);

    mDecoder = nullptr;
    mDecoderPadding = 0;
    if(mFmtChannels == FmtSuperStereo)
    {
        switch(UhjDecodeQuality)
        {
        case UhjQualityType::IIR:
            mDecoder = std::make_unique<UhjStereoDecoderIIR>();
            mDecoderPadding = UhjStereoDecoderIIR::sInputPadding;
            break;
        case UhjQualityType::FIR256:
            mDecoder = std::make_unique<UhjStereoDecoder<UhjLength256>>();
            mDecoderPadding = UhjStereoDecoder<UhjLength256>::sInputPadding;
            break;
        case UhjQualityType::FIR512:
            mDecoder = std::make_unique<UhjStereoDecoder<UhjLength512>>();
            mDecoderPadding = UhjStereoDecoder<UhjLength512>::sInputPadding;
            break;
        }
    }
    else if(IsUHJ(mFmtChannels))
    {
        switch(UhjDecodeQuality)
        {
        case UhjQualityType::IIR:
            mDecoder = std::make_unique<UhjDecoderIIR>();
            mDecoderPadding = UhjDecoderIIR::sInputPadding;
            break;
        case UhjQualityType::FIR256:
            mDecoder = std::make_unique<UhjDecoder<UhjLength256>>();
            mDecoderPadding = UhjDecoder<UhjLength256>::sInputPadding;
            break;
        case UhjQualityType::FIR512:
            mDecoder = std::make_unique<UhjDecoder<UhjLength512>>();
            mDecoderPadding = UhjDecoder<UhjLength512>::sInputPadding;
            break;
        }
    }

    /* Clear the stepping value explicitly so the mixer knows not to mix this
     * until the update gets applied.
     */
    mStep = 0;

    /* Make sure the sample history is cleared. */
    std::fill(mPrevSamples.begin(), mPrevSamples.end(), HistoryLine{});

    if(mFmtChannels == FmtUHJ2 && !device->mUhjEncoder)
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
        const BandSplitter splitter{device->mXOverFreq / static_cast<float>(device->Frequency)};
        for(auto &chandata : mChans)
        {
            chandata.mAmbiHFScale = 1.0f;
            chandata.mAmbiLFScale = 1.0f;
            chandata.mAmbiSplitter = splitter;
            chandata.mDryParams = DirectParams{};
            chandata.mDryParams.NFCtrlFilter = device->mNFCtrlFilter;
            std::fill_n(chandata.mWetParams.begin(), device->NumAuxSends, SendParams{});
        }
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
        const uint8_t *OrderFromChan{Is2DAmbisonic(mFmtChannels) ?
            AmbiIndex::OrderFrom2DChannel().data() : AmbiIndex::OrderFromChannel().data()};
        const auto scales = AmbiScale::GetHFOrderScales(mAmbiOrder, device->mAmbiOrder,
            device->m2DMixing);

        const BandSplitter splitter{device->mXOverFreq / static_cast<float>(device->Frequency)};
        for(auto &chandata : mChans)
        {
            chandata.mAmbiHFScale = scales[*(OrderFromChan++)];
            chandata.mAmbiLFScale = 1.0f;
            chandata.mAmbiSplitter = splitter;
            chandata.mDryParams = DirectParams{};
            chandata.mDryParams.NFCtrlFilter = device->mNFCtrlFilter;
            std::fill_n(chandata.mWetParams.begin(), device->NumAuxSends, SendParams{});
        }
        mFlags.set(VoiceIsAmbisonic);
    }
    else
    {
        for(auto &chandata : mChans)
        {
            chandata.mDryParams = DirectParams{};
            chandata.mDryParams.NFCtrlFilter = device->mNFCtrlFilter;
            std::fill_n(chandata.mWetParams.begin(), device->NumAuxSends, SendParams{});
        }
        mFlags.reset(VoiceIsAmbisonic);
    }
}
