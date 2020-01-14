/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "voice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <new>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"

#include "al/buffer.h"
#include "al/event.h"
#include "al/source.h"
#include "alcmain.h"
#include "albyte.h"
#include "alconfig.h"
#include "alcontext.h"
#include "alnumeric.h"
#include "aloptional.h"
#include "alspan.h"
#include "alstring.h"
#include "alu.h"
#include "cpu_caps.h"
#include "devformat.h"
#include "filters/biquad.h"
#include "filters/nfc.h"
#include "filters/splitter.h"
#include "hrtf.h"
#include "inprogext.h"
#include "logging.h"
#include "mixer/defs.h"
#include "opthelpers.h"
#include "ringbuffer.h"
#include "threads.h"
#include "vector.h"


static_assert((INT_MAX>>FRACTIONBITS)/MAX_PITCH > BUFFERSIZE,
              "MAX_PITCH and/or BUFFERSIZE are too large for FRACTIONBITS!");


Resampler ResamplerDefault{Resampler::Linear};

namespace {

using HrtfMixerFunc = void(*)(const ALfloat *InSamples, float2 *AccumSamples, const ALuint IrSize,
    const MixHrtfFilter *hrtfparams, const size_t BufferSize);
using HrtfMixerBlendFunc = void(*)(const ALfloat *InSamples, float2 *AccumSamples,
    const ALuint IrSize, const HrtfFilter *oldparams, const MixHrtfFilter *newparams,
    const size_t BufferSize);

HrtfMixerFunc MixHrtfSamples = MixHrtf_<CTag>;
HrtfMixerBlendFunc MixHrtfBlendSamples = MixHrtfBlend_<CTag>;

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


void aluInitMixer()
{
    if(auto resopt = ConfigValueStr(nullptr, nullptr, "resampler"))
    {
        struct ResamplerEntry {
            const char name[16];
            const Resampler resampler;
        };
        constexpr ResamplerEntry ResamplerList[]{
            { "none", Resampler::Point },
            { "point", Resampler::Point },
            { "cubic", Resampler::Cubic },
            { "bsinc12", Resampler::BSinc12 },
            { "fast_bsinc12", Resampler::FastBSinc12 },
            { "bsinc24", Resampler::BSinc24 },
            { "fast_bsinc24", Resampler::FastBSinc24 },
        };

        const char *str{resopt->c_str()};
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

    MixHrtfBlendSamples = SelectHrtfBlendMixer();
    MixHrtfSamples = SelectHrtfMixer();
}


namespace {

/* A quick'n'dirty lookup table to decode a muLaw-encoded byte sample into a
 * signed 16-bit sample */
constexpr ALshort muLawDecompressionTable[256] = {
    -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
    -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
    -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
    -11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
     -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
     -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
     -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
     -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
     -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
     -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
      -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,
      -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
      -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,
      -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
      -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,
       -56,   -48,   -40,   -32,   -24,   -16,    -8,     0,
     32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
     23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
     15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
     11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
      7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
      5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
      3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
      2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
      1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
      1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
       876,   844,   812,   780,   748,   716,   684,   652,
       620,   588,   556,   524,   492,   460,   428,   396,
       372,   356,   340,   324,   308,   292,   276,   260,
       244,   228,   212,   196,   180,   164,   148,   132,
       120,   112,   104,    96,    88,    80,    72,    64,
        56,    48,    40,    32,    24,    16,     8,     0
};

/* A quick'n'dirty lookup table to decode an aLaw-encoded byte sample into a
 * signed 16-bit sample */
constexpr ALshort aLawDecompressionTable[256] = {
     -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
     -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
     -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
     -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
    -22016,-20992,-24064,-23040,-17920,-16896,-19968,-18944,
    -30208,-29184,-32256,-31232,-26112,-25088,-28160,-27136,
    -11008,-10496,-12032,-11520, -8960, -8448, -9984, -9472,
    -15104,-14592,-16128,-15616,-13056,-12544,-14080,-13568,
      -344,  -328,  -376,  -360,  -280,  -264,  -312,  -296,
      -472,  -456,  -504,  -488,  -408,  -392,  -440,  -424,
       -88,   -72,  -120,  -104,   -24,    -8,   -56,   -40,
      -216,  -200,  -248,  -232,  -152,  -136,  -184,  -168,
     -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
     -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
      -688,  -656,  -752,  -720,  -560,  -528,  -624,  -592,
      -944,  -912, -1008,  -976,  -816,  -784,  -880,  -848,
      5504,  5248,  6016,  5760,  4480,  4224,  4992,  4736,
      7552,  7296,  8064,  7808,  6528,  6272,  7040,  6784,
      2752,  2624,  3008,  2880,  2240,  2112,  2496,  2368,
      3776,  3648,  4032,  3904,  3264,  3136,  3520,  3392,
     22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
     30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
     11008, 10496, 12032, 11520,  8960,  8448,  9984,  9472,
     15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
       344,   328,   376,   360,   280,   264,   312,   296,
       472,   456,   504,   488,   408,   392,   440,   424,
        88,    72,   120,   104,    24,     8,    56,    40,
       216,   200,   248,   232,   152,   136,   184,   168,
      1376,  1312,  1504,  1440,  1120,  1056,  1248,  1184,
      1888,  1824,  2016,  1952,  1632,  1568,  1760,  1696,
       688,   656,   752,   720,   560,   528,   624,   592,
       944,   912,  1008,   976,   816,   784,   880,   848
};

template<FmtType T>
struct FmtTypeTraits { };

template<>
struct FmtTypeTraits<FmtUByte> {
    using Type = ALubyte;
    static constexpr inline float to_float(const Type val) noexcept
    { return val*(1.0f/128.0f) - 1.0f; }
};
template<>
struct FmtTypeTraits<FmtShort> {
    using Type = ALshort;
    static constexpr inline float to_float(const Type val) noexcept { return val*(1.0f/32768.0f); }
};
template<>
struct FmtTypeTraits<FmtFloat> {
    using Type = ALfloat;
    static constexpr inline float to_float(const Type val) noexcept { return val; }
};
template<>
struct FmtTypeTraits<FmtDouble> {
    using Type = ALdouble;
    static constexpr inline float to_float(const Type val) noexcept
    { return static_cast<ALfloat>(val); }
};
template<>
struct FmtTypeTraits<FmtMulaw> {
    using Type = ALubyte;
    static constexpr inline float to_float(const Type val) noexcept
    { return muLawDecompressionTable[val] * (1.0f/32768.0f); }
};
template<>
struct FmtTypeTraits<FmtAlaw> {
    using Type = ALubyte;
    static constexpr inline float to_float(const Type val) noexcept
    { return aLawDecompressionTable[val] * (1.0f/32768.0f); }
};


void SendSourceStoppedEvent(ALCcontext *context, ALuint id)
{
    RingBuffer *ring{context->mAsyncEvents.get()};
    auto evt_vec = ring->getWriteVector();
    if(evt_vec.first.len < 1) return;

    AsyncEvent *evt{new (evt_vec.first.buf) AsyncEvent{EventType_SourceStateChange}};
    evt->u.srcstate.id = id;
    evt->u.srcstate.state = AL_STOPPED;

    ring->writeAdvance(1);
}


const float *DoFilters(BiquadFilter *lpfilter, BiquadFilter *hpfilter, float *dst,
    const al::span<const float> src, int type)
{
    switch(type)
    {
    case AF_None:
        lpfilter->clear();
        hpfilter->clear();
        break;

    case AF_LowPass:
        lpfilter->process(src, dst);
        hpfilter->clear();
        return dst;
    case AF_HighPass:
        lpfilter->clear();
        hpfilter->process(src, dst);
        return dst;

    case AF_BandPass:
        lpfilter->process(src, dst);
        hpfilter->process({dst, src.size()}, dst);
        return dst;
    }
    return src.data();
}


template<FmtType T>
inline void LoadSampleArray(ALfloat *RESTRICT dst, const al::byte *src, const size_t srcstep,
    const size_t samples) noexcept
{
    using SampleType = typename FmtTypeTraits<T>::Type;

    const SampleType *RESTRICT ssrc{reinterpret_cast<const SampleType*>(src)};
    for(size_t i{0u};i < samples;i++)
        dst[i] = FmtTypeTraits<T>::to_float(ssrc[i*srcstep]);
}

void LoadSamples(ALfloat *RESTRICT dst, const al::byte *src, const size_t srcstep, FmtType srctype,
    const size_t samples) noexcept
{
#define HANDLE_FMT(T)  case T: LoadSampleArray<T>(dst, src, srcstep, samples); break
    switch(srctype)
    {
        HANDLE_FMT(FmtUByte);
        HANDLE_FMT(FmtShort);
        HANDLE_FMT(FmtFloat);
        HANDLE_FMT(FmtDouble);
        HANDLE_FMT(FmtMulaw);
        HANDLE_FMT(FmtAlaw);
    }
#undef HANDLE_FMT
}

ALfloat *LoadBufferStatic(ALbufferlistitem *BufferListItem, ALbufferlistitem *&BufferLoopItem,
    const size_t NumChannels, const size_t SampleSize, const size_t chan, size_t DataPosInt,
    al::span<ALfloat> SrcBuffer)
{
    const ALbuffer *Buffer{BufferListItem->mBuffer};
    const ALuint LoopStart{Buffer->LoopStart};
    const ALuint LoopEnd{Buffer->LoopEnd};
    ASSUME(LoopEnd > LoopStart);

    /* If current pos is beyond the loop range, do not loop */
    if(!BufferLoopItem || DataPosInt >= LoopEnd)
    {
        BufferLoopItem = nullptr;

        /* Load what's left to play from the buffer */
        const size_t DataRem{minz(SrcBuffer.size(), Buffer->SampleLen-DataPosInt)};

        const al::byte *Data{Buffer->mData.data()};
        Data += (DataPosInt*NumChannels + chan)*SampleSize;

        LoadSamples(SrcBuffer.data(), Data, NumChannels, Buffer->mFmtType, DataRem);
        SrcBuffer = SrcBuffer.subspan(DataRem);
    }
    else
    {
        /* Load what's left of this loop iteration */
        const size_t DataRem{minz(SrcBuffer.size(), LoopEnd-DataPosInt)};

        const al::byte *Data{Buffer->mData.data()};
        Data += (DataPosInt*NumChannels + chan)*SampleSize;

        LoadSamples(SrcBuffer.data(), Data, NumChannels, Buffer->mFmtType, DataRem);
        SrcBuffer = SrcBuffer.subspan(DataRem);

        /* Load any repeats of the loop we can to fill the buffer. */
        const auto LoopSize = static_cast<size_t>(LoopEnd - LoopStart);
        while(!SrcBuffer.empty())
        {
            const size_t DataSize{minz(SrcBuffer.size(), LoopSize)};

            Data = Buffer->mData.data() + (LoopStart*NumChannels + chan)*SampleSize;

            LoadSamples(SrcBuffer.data(), Data, NumChannels, Buffer->mFmtType, DataSize);
            SrcBuffer = SrcBuffer.subspan(DataSize);
        }
    }
    return SrcBuffer.begin();
}

ALfloat *LoadBufferQueue(ALbufferlistitem *BufferListItem, ALbufferlistitem *BufferLoopItem,
    const size_t NumChannels, const size_t SampleSize, const size_t chan, size_t DataPosInt,
    al::span<ALfloat> SrcBuffer)
{
    /* Crawl the buffer queue to fill in the temp buffer */
    while(BufferListItem && !SrcBuffer.empty())
    {
        ALbuffer *Buffer{BufferListItem->mBuffer};
        if(!(Buffer && DataPosInt < Buffer->SampleLen))
        {
            if(Buffer) DataPosInt -= Buffer->SampleLen;
            BufferListItem = BufferListItem->mNext.load(std::memory_order_acquire);
            if(!BufferListItem) BufferListItem = BufferLoopItem;
            continue;
        }

        const size_t DataSize{minz(SrcBuffer.size(), Buffer->SampleLen-DataPosInt)};

        const al::byte *Data{Buffer->mData.data()};
        Data += (DataPosInt*NumChannels + chan)*SampleSize;

        LoadSamples(SrcBuffer.data(), Data, NumChannels, Buffer->mFmtType, DataSize);
        SrcBuffer = SrcBuffer.subspan(DataSize);
        if(SrcBuffer.empty()) break;

        DataPosInt = 0;
        BufferListItem = BufferListItem->mNext.load(std::memory_order_acquire);
        if(!BufferListItem) BufferListItem = BufferLoopItem;
    }

    return SrcBuffer.begin();
}


void DoHrtfMix(const float *samples, const ALuint DstBufferSize, DirectParams &parms,
    const float TargetGain, const ALuint Counter, ALuint OutPos, const ALuint IrSize,
    ALCdevice *Device)
{
    auto &HrtfSamples = Device->HrtfSourceData;
    auto &AccumSamples = Device->HrtfAccumData;

    /* Copy the HRTF history and new input samples into a temp buffer. */
    auto src_iter = std::copy(parms.Hrtf.History.begin(), parms.Hrtf.History.end(),
        std::begin(HrtfSamples));
    std::copy_n(samples, DstBufferSize, src_iter);
    /* Copy the last used samples back into the history buffer for later. */
    std::copy_n(std::begin(HrtfSamples) + DstBufferSize, parms.Hrtf.History.size(),
        parms.Hrtf.History.begin());

    /* If fading, the old gain is not silence, and this is the first mixing
     * pass, fade between the IRs.
     */
    ALuint fademix{0u};
    if(Counter && parms.Hrtf.Old.Gain > GAIN_SILENCE_THRESHOLD && OutPos == 0)
    {
        fademix = minu(DstBufferSize, 128);

        float gain{TargetGain};

        /* The new coefficients need to fade in completely since they're
         * replacing the old ones. To keep the gain fading consistent,
         * interpolate between the old and new target gains given how much of
         * the fade time this mix handles.
         */
        if LIKELY(Counter > fademix)
        {
            const ALfloat a{static_cast<float>(fademix) / static_cast<float>(Counter)};
            gain = lerp(parms.Hrtf.Old.Gain, TargetGain, a);
        }
        MixHrtfFilter hrtfparams;
        hrtfparams.Coeffs = &parms.Hrtf.Target.Coeffs;
        hrtfparams.Delay[0] = parms.Hrtf.Target.Delay[0];
        hrtfparams.Delay[1] = parms.Hrtf.Target.Delay[1];
        hrtfparams.Gain = 0.0f;
        hrtfparams.GainStep = gain / static_cast<float>(fademix);

        MixHrtfBlendSamples(HrtfSamples, AccumSamples+OutPos, IrSize, &parms.Hrtf.Old, &hrtfparams,
            fademix);
        /* Update the old parameters with the result. */
        parms.Hrtf.Old = parms.Hrtf.Target;
        parms.Hrtf.Old.Gain = gain;
        OutPos += fademix;
    }

    if LIKELY(fademix < DstBufferSize)
    {
        const ALuint todo{DstBufferSize - fademix};
        float gain{TargetGain};

        /* Interpolate the target gain if the gain fading lasts longer than
         * this mix.
         */
        if(Counter > DstBufferSize)
        {
            const float a{static_cast<float>(todo) / static_cast<float>(Counter-fademix)};
            gain = lerp(parms.Hrtf.Old.Gain, TargetGain, a);
        }

        MixHrtfFilter hrtfparams;
        hrtfparams.Coeffs = &parms.Hrtf.Target.Coeffs;
        hrtfparams.Delay[0] = parms.Hrtf.Target.Delay[0];
        hrtfparams.Delay[1] = parms.Hrtf.Target.Delay[1];
        hrtfparams.Gain = parms.Hrtf.Old.Gain;
        hrtfparams.GainStep = (gain - parms.Hrtf.Old.Gain) / static_cast<float>(todo);
        MixHrtfSamples(HrtfSamples+fademix, AccumSamples+OutPos, IrSize, &hrtfparams, todo);
        /* Store the now-current gain for next time. */
        parms.Hrtf.Old.Gain = gain;
    }
}

void DoNfcMix(const al::span<const float> samples, FloatBufferLine *OutBuffer, DirectParams &parms,
    const float *TargetGains, const ALuint Counter, const ALuint OutPos, ALCdevice *Device)
{
    using FilterProc = void (NfcFilter::*)(const al::span<const float>, float*);
    static constexpr FilterProc NfcProcess[MAX_AMBI_ORDER+1]{
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
        if(++order == MAX_AMBI_ORDER+1)
            break;
    }
}

} // namespace

void ALvoice::mix(const State vstate, ALCcontext *Context, const ALuint SamplesToDo)
{
    static constexpr std::array<float,MAX_OUTPUT_CHANNELS> SilentTarget{};

    ASSUME(SamplesToDo > 0);

    /* Get voice info */
    const bool isstatic{(mFlags&VOICE_IS_STATIC) != 0};
    ALuint DataPosInt{mPosition.load(std::memory_order_relaxed)};
    ALuint DataPosFrac{mPositionFrac.load(std::memory_order_relaxed)};
    ALbufferlistitem *BufferListItem{mCurrentBuffer.load(std::memory_order_relaxed)};
    ALbufferlistitem *BufferLoopItem{mLoopBuffer.load(std::memory_order_relaxed)};
    const ALuint NumChannels{mNumChannels};
    const ALuint SampleSize{mSampleSize};
    const ALuint increment{mStep};
    if UNLIKELY(increment < 1)
    {
        /* If the voice is supposed to be stopping but can't be mixed, just
         * stop it before bailing.
         */
        if(vstate == ALvoice::Stopping)
            mPlayState.store(ALvoice::Stopped, std::memory_order_release);
        return;
    }

    ASSUME(NumChannels > 0);
    ASSUME(SampleSize > 0);
    ASSUME(increment > 0);

    ALCdevice *Device{Context->mDevice.get()};
    const ALuint NumSends{Device->NumAuxSends};
    const ALuint IrSize{Device->mHrtf ? Device->mHrtf->irSize : 0};

    ResamplerFunc Resample{(increment == FRACTIONONE && DataPosFrac == 0) ?
                           Resample_<CopyTag,CTag> : mResampler};

    ALuint Counter{(mFlags&VOICE_IS_FADING) ? SamplesToDo : 0};
    if(!Counter)
    {
        /* No fading, just overwrite the old/current params. */
        for(ALuint chan{0};chan < NumChannels;chan++)
        {
            ChannelData &chandata = mChans[chan];
            {
                DirectParams &parms = chandata.mDryParams;
                if(!(mFlags&VOICE_HAS_HRTF))
                    parms.Gains.Current = parms.Gains.Target;
                else
                    parms.Hrtf.Old = parms.Hrtf.Target;
            }
            for(ALuint send{0};send < NumSends;++send)
            {
                if(mSend[send].Buffer.empty())
                    continue;

                SendParams &parms = chandata.mWetParams[send];
                parms.Gains.Current = parms.Gains.Target;
            }
        }
    }
    else if((mFlags&VOICE_HAS_HRTF))
    {
        for(ALuint chan{0};chan < NumChannels;chan++)
        {
            DirectParams &parms = mChans[chan].mDryParams;
            if(!(parms.Hrtf.Old.Gain > GAIN_SILENCE_THRESHOLD))
            {
                /* The old HRTF params are silent, so overwrite the old
                 * coefficients with the new, and reset the old gain to 0. The
                 * future mix will then fade from silence.
                 */
                parms.Hrtf.Old = parms.Hrtf.Target;
                parms.Hrtf.Old.Gain = 0.0f;
            }
        }
    }

    ALuint buffers_done{0u};
    ALuint OutPos{0u};
    do {
        /* Figure out how many buffer samples will be needed */
        ALuint DstBufferSize{SamplesToDo - OutPos};

        /* Calculate the last written dst sample pos. */
        uint64_t DataSize64{DstBufferSize - 1};
        /* Calculate the last read src sample pos. */
        DataSize64 = (DataSize64*increment + DataPosFrac) >> FRACTIONBITS;
        /* +1 to get the src sample count, include padding. */
        DataSize64 += 1 + MAX_RESAMPLER_PADDING;

        auto SrcBufferSize = static_cast<ALuint>(
            minu64(DataSize64, BUFFERSIZE + MAX_RESAMPLER_PADDING + 1));
        if(SrcBufferSize > BUFFERSIZE + MAX_RESAMPLER_PADDING)
        {
            SrcBufferSize = BUFFERSIZE + MAX_RESAMPLER_PADDING;
            /* If the source buffer got saturated, we can't fill the desired
             * dst size. Figure out how many samples we can actually mix from
             * this.
             */
            DataSize64 = SrcBufferSize - MAX_RESAMPLER_PADDING;
            DataSize64 = ((DataSize64<<FRACTIONBITS) - DataPosFrac + increment-1) / increment;
            DstBufferSize = static_cast<ALuint>(minu64(DataSize64, DstBufferSize));

            /* Some mixers like having a multiple of 4, so try to give that
             * unless this is the last update.
             */
            if(DstBufferSize < SamplesToDo-OutPos)
                DstBufferSize &= ~3u;
        }

        ASSUME(DstBufferSize > 0);
        for(ALuint chan{0};chan < NumChannels;chan++)
        {
            ChannelData &chandata = mChans[chan];
            const al::span<ALfloat> SrcData{Device->SourceData, SrcBufferSize};

            /* Load the previous samples into the source data first, then load
             * what we can from the buffer queue.
             */
            auto srciter = std::copy_n(chandata.mPrevSamples.begin(), MAX_RESAMPLER_PADDING>>1,
                SrcData.begin());

            if UNLIKELY(!BufferListItem)
                srciter = std::copy(chandata.mPrevSamples.begin()+(MAX_RESAMPLER_PADDING>>1),
                    chandata.mPrevSamples.end(), srciter);
            else if(isstatic)
                srciter = LoadBufferStatic(BufferListItem, BufferLoopItem, NumChannels,
                    SampleSize, chan, DataPosInt, {srciter, SrcData.end()});
            else
                srciter = LoadBufferQueue(BufferListItem, BufferLoopItem, NumChannels,
                    SampleSize, chan, DataPosInt, {srciter, SrcData.end()});

            if UNLIKELY(srciter != SrcData.end())
            {
                /* If the source buffer wasn't filled, copy the last sample for
                 * the remaining buffer. Ideally it should have ended with
                 * silence, but if not the gain fading should help avoid clicks
                 * from sudden amplitude changes.
                 */
                const ALfloat sample{*(srciter-1)};
                std::fill(srciter, SrcData.end(), sample);
            }

            /* Store the last source samples used for next time. */
            std::copy_n(&SrcData[(increment*DstBufferSize + DataPosFrac)>>FRACTIONBITS],
                chandata.mPrevSamples.size(), chandata.mPrevSamples.begin());

            /* Resample, then apply ambisonic upsampling as needed. */
            const ALfloat *ResampledData{Resample(&mResampleState,
                &SrcData[MAX_RESAMPLER_PADDING>>1], DataPosFrac, increment,
                {Device->ResampledData, DstBufferSize})};
            if((mFlags&VOICE_IS_AMBISONIC))
            {
                const float hfscale{chandata.mAmbiScale};
                /* Beware the evil const_cast. It's safe since it's pointing to
                 * either SourceData or ResampledData (both non-const), but the
                 * resample method takes the source as const float* and may
                 * return it without copying to output, making it currently
                 * unavoidable.
                 */
                const al::span<float> samples{const_cast<float*>(ResampledData), DstBufferSize};
                chandata.mAmbiSplitter.applyHfScale(samples, hfscale);
            }

            /* Now filter and mix to the appropriate outputs. */
            ALfloat (&FilterBuf)[BUFFERSIZE] = Device->FilteredData;
            {
                DirectParams &parms = chandata.mDryParams;
                const ALfloat *samples{DoFilters(&parms.LowPass, &parms.HighPass, FilterBuf,
                    {ResampledData, DstBufferSize}, mDirect.FilterType)};

                if((mFlags&VOICE_HAS_HRTF))
                {
                    const ALfloat TargetGain{UNLIKELY(vstate == ALvoice::Stopping) ? 0.0f :
                        parms.Hrtf.Target.Gain};
                    DoHrtfMix(samples, DstBufferSize, parms, TargetGain, Counter, OutPos, IrSize,
                        Device);
                }
                else if((mFlags&VOICE_HAS_NFC))
                {
                    const float *TargetGains{UNLIKELY(vstate == ALvoice::Stopping) ?
                        SilentTarget.data() : parms.Gains.Target.data()};
                    DoNfcMix({samples, DstBufferSize}, mDirect.Buffer.data(), parms, TargetGains,
                        Counter, OutPos, Device);
                }
                else
                {
                    const float *TargetGains{UNLIKELY(vstate == ALvoice::Stopping) ?
                        SilentTarget.data() : parms.Gains.Target.data()};
                    MixSamples({samples, DstBufferSize}, mDirect.Buffer,
                        parms.Gains.Current.data(), TargetGains, Counter, OutPos);
                }
            }

            for(ALuint send{0};send < NumSends;++send)
            {
                if(mSend[send].Buffer.empty())
                    continue;

                SendParams &parms = chandata.mWetParams[send];
                const ALfloat *samples{DoFilters(&parms.LowPass, &parms.HighPass, FilterBuf,
                    {ResampledData, DstBufferSize}, mSend[send].FilterType)};

                const float *TargetGains{UNLIKELY(vstate == ALvoice::Stopping) ?
                    SilentTarget.data() : parms.Gains.Target.data()};
                MixSamples({samples, DstBufferSize}, mSend[send].Buffer,
                    parms.Gains.Current.data(), TargetGains, Counter, OutPos);
            }
        }
        /* Update positions */
        DataPosFrac += increment*DstBufferSize;
        DataPosInt  += DataPosFrac>>FRACTIONBITS;
        DataPosFrac &= FRACTIONMASK;

        OutPos += DstBufferSize;
        Counter = maxu(DstBufferSize, Counter) - DstBufferSize;

        if UNLIKELY(!BufferListItem)
        {
            /* Do nothing extra when there's no buffers. */
        }
        else if(isstatic)
        {
            if(BufferLoopItem)
            {
                /* Handle looping static source */
                const ALbuffer *Buffer{BufferListItem->mBuffer};
                const ALuint LoopStart{Buffer->LoopStart};
                const ALuint LoopEnd{Buffer->LoopEnd};
                if(DataPosInt >= LoopEnd)
                {
                    assert(LoopEnd > LoopStart);
                    DataPosInt = ((DataPosInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                }
            }
            else
            {
                /* Handle non-looping static source */
                if(DataPosInt >= BufferListItem->mSampleLen)
                {
                    BufferListItem = nullptr;
                    break;
                }
            }
        }
        else
        {
            /* Handle streaming source */
            do {
                if(BufferListItem->mSampleLen > DataPosInt)
                    break;

                DataPosInt -= BufferListItem->mSampleLen;

                ++buffers_done;
                BufferListItem = BufferListItem->mNext.load(std::memory_order_relaxed);
                if(!BufferListItem) BufferListItem = BufferLoopItem;
            } while(BufferListItem);
        }
    } while(OutPos < SamplesToDo);

    mFlags |= VOICE_IS_FADING;

    /* Don't update positions and buffers if we were stopping. */
    if UNLIKELY(vstate == ALvoice::Stopping)
    {
        mPlayState.store(ALvoice::Stopped, std::memory_order_release);
        return;
    }

    /* Capture the source ID in case it's reset for stopping. */
    const ALuint SourceID{mSourceID.load(std::memory_order_relaxed)};

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
    const ALbitfieldSOFT enabledevt{Context->mEnabledEvts.load(std::memory_order_acquire)};
    if(buffers_done > 0 && (enabledevt&EventType_BufferCompleted))
    {
        RingBuffer *ring{Context->mAsyncEvents.get()};
        auto evt_vec = ring->getWriteVector();
        if(evt_vec.first.len > 0)
        {
            AsyncEvent *evt{new (evt_vec.first.buf) AsyncEvent{EventType_BufferCompleted}};
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
        mPlayState.store(ALvoice::Stopping, std::memory_order_release);
        if((enabledevt&EventType_SourceStateChange))
            SendSourceStoppedEvent(Context, SourceID);
    }
}
