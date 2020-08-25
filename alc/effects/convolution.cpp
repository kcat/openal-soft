
#include "config.h"

#include "AL/al.h"
#include "AL/alc.h"

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcomplex.h"
#include "alcontext.h"
#include "almalloc.h"
#include "alspan.h"
#include "effects/base.h"
#include "logging.h"
#include "polyphase_resampler.h"


namespace {

/* Convolution reverb is implemented using a segmented overlap-add method. The
 * impulse response is broken up into multiple segments of 512 samples, and
 * each segment has an FFT applied with a 1024-sample buffer (the latter half
 * left silent) to get its frequency-domain response. The resulting response
 * has its positive/non-mirrored frequencies saved (513 bins) in each segment.
 *
 * Input samples are similarly broken up into 512-sample segments, with an FFT
 * applied to each new incoming segment to get its 513 bins. A history of FFT'd
 * input segments is maintained, equal to the length of the impulse response.
 *
 * To apply the reverberation, each impulse response segment is convolved with
 * its paired input segment (using complex multiplies, far cheaper than FIRs),
 * accumulating into a 1024-bin FFT buffer. The input history is then shifted
 * to align with later impulse response segments for next time.
 *
 * An inverse FFT is then applied to the accumulated FFT buffer to get a 1024-
 * sample time-domain response for output, which is split in two halves. The
 * first half is the 512-sample output, and the second half is a 512-sample
 * (really, 511) delayed extension, which gets added to the output next time.
 * Convolving two time-domain responses of lengths N and M results in a time-
 * domain signal of length N+M-1, and this holds true regardless of the
 * convolution being applied in the frequency domain, so these "overflow"
 * samples need to be accounted for.
 *
 * Limitations:
 * There is currently a 512-sample delay on the output, as a result of needing
 * to collect that many input samples to do an FFT with. This can be fixed by
 * excluding the first impulse response segment from being FFT'd, and applying
 * it directly in the time domain. This will have higher CPU consumption, but
 * it won't have to wait before generating output.
 */


/* TODO: De-duplicate this load stuff (also in voice.cpp). */

constexpr int16_t muLawDecompressionTable[256] = {
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

constexpr int16_t aLawDecompressionTable[256] = {
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
    using Type = uint8_t;
    static constexpr inline double to_double(const Type val) noexcept
    { return val*(1.0/128.0) - 1.0; }
};
template<>
struct FmtTypeTraits<FmtShort> {
    using Type = int16_t;
    static constexpr inline double to_double(const Type val) noexcept { return val*(1.0/32768.0); }
};
template<>
struct FmtTypeTraits<FmtFloat> {
    using Type = float;
    static constexpr inline double to_double(const Type val) noexcept { return val; }
};
template<>
struct FmtTypeTraits<FmtDouble> {
    using Type = double;
    static constexpr inline double to_double(const Type val) noexcept { return val; }
};
template<>
struct FmtTypeTraits<FmtMulaw> {
    using Type = uint8_t;
    static constexpr inline double to_double(const Type val) noexcept
    { return muLawDecompressionTable[val] * (1.0/32768.0); }
};
template<>
struct FmtTypeTraits<FmtAlaw> {
    using Type = uint8_t;
    static constexpr inline double to_double(const Type val) noexcept
    { return aLawDecompressionTable[val] * (1.0/32768.0); }
};


template<FmtType T>
inline void LoadSampleArray(double *RESTRICT dst, const al::byte *src, const size_t srcstep,
    const size_t samples) noexcept
{
    using SampleType = typename FmtTypeTraits<T>::Type;

    const SampleType *RESTRICT ssrc{reinterpret_cast<const SampleType*>(src)};
    for(size_t i{0u};i < samples;i++)
        dst[i] = FmtTypeTraits<T>::to_double(ssrc[i*srcstep]);
}

void LoadSamples(double *RESTRICT dst, const al::byte *src, const size_t srcstep, FmtType srctype,
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


using complex_d = std::complex<double>;

constexpr size_t ConvolveUpdateSize{1024};
constexpr size_t ConvolveUpdateSamples{ConvolveUpdateSize / 2};

#define MAX_FILTER_CHANNELS 2


struct ConvolutionFilter final : public EffectBufferBase {
    size_t mNumConvolveSegs{0};
    complex_d *mInputHistory{};
    complex_d *mConvolveFilter[MAX_FILTER_CHANNELS]{};

    FmtChannels mChannels;

    std::unique_ptr<complex_d[]> mComplexData;

    DEF_NEWDEL(ConvolutionFilter)
};

struct ConvolutionState final : public EffectState {
    ConvolutionFilter *mFilter{};

    size_t mFifoPos{0};
    alignas(16) std::array<double,ConvolveUpdateSamples*2> mOutput[MAX_FILTER_CHANNELS]{};
    alignas(16) std::array<complex_d,ConvolveUpdateSize> mFftBuffer{};

    ALuint mNumChannels;
    alignas(16) FloatBufferLine mTempBuffer[MAX_FILTER_CHANNELS]{};

    struct {
        float Current[MAX_OUTPUT_CHANNELS]{};
        float Target[MAX_OUTPUT_CHANNELS]{};
    } mGains[MAX_FILTER_CHANNELS];

    ConvolutionState() = default;
    ~ConvolutionState() override = default;

    void deviceUpdate(const ALCdevice *device) override;
    EffectBufferBase *createBuffer(const ALCdevice *device, const al::byte *sampleData,
        ALuint sampleRate, FmtType sampleType, FmtChannels channelType, ALuint numSamples) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(ConvolutionState)
};

void ConvolutionState::deviceUpdate(const ALCdevice* /*device*/)
{
    mFifoPos = 0;
    for(auto &buffer : mOutput)
        buffer.fill(0.0f);
    mFftBuffer.fill(complex_d{});

    for(auto &buffer : mTempBuffer)
        buffer.fill(0.0);

    for(auto &e : mGains)
    {
        std::fill(std::begin(e.Current), std::end(e.Current), 0.0f);
        std::fill(std::begin(e.Target), std::end(e.Target), 0.0f);
    }
}

EffectBufferBase *ConvolutionState::createBuffer(const ALCdevice *device,
    const al::byte *sampleData, ALuint sampleRate, FmtType sampleType,
    FmtChannels channelType, ALuint numSamples)
{
    /* FIXME: Support anything. */
    if(channelType != FmtMono && channelType != FmtStereo)
        return nullptr;

    /* The impulse response needs to have the same sample rate as the input and
     * output. The bsinc24 resampler is decent, but there is high-frequency
     * attenation that some people may be able to pick up on. Since this is
     * very infrequent called, go ahead and use the polyphase resampler.
     */
    PPhaseResampler resampler;
    if(device->Frequency != sampleRate)
        resampler.init(sampleRate, device->Frequency);
    const auto resampledCount = static_cast<ALuint>(
        (uint64_t{numSamples}*device->Frequency + (sampleRate-1)) / sampleRate);

    al::intrusive_ptr<ConvolutionFilter> filter{new ConvolutionFilter{}};

    auto bytesPerSample = BytesFromFmt(sampleType);
    auto numChannels = ChannelsFromFmt(channelType, 1);
    constexpr size_t m{ConvolveUpdateSize/2 + 1};

    /* Calculate the number of segments needed to hold the impulse response and
     * the input history (rounded up), and allocate them.
     */
    filter->mNumConvolveSegs = (numSamples+(ConvolveUpdateSamples-1)) / ConvolveUpdateSamples;

    const size_t complex_length{filter->mNumConvolveSegs * m * (numChannels+1)};
    filter->mComplexData = std::make_unique<complex_d[]>(complex_length);
    std::fill_n(filter->mComplexData.get(), complex_length, complex_d{});

    filter->mInputHistory = filter->mComplexData.get();
    filter->mConvolveFilter[0] = filter->mInputHistory + filter->mNumConvolveSegs*m;
    for(size_t c{1};c < numChannels;++c)
        filter->mConvolveFilter[c] = filter->mConvolveFilter[c-1] + filter->mNumConvolveSegs*m;

    filter->mChannels = channelType;

    auto fftbuffer = std::make_unique<std::array<complex_d,ConvolveUpdateSize>>();
    auto srcsamples = std::make_unique<double[]>(maxz(numSamples, resampledCount));
    for(size_t c{0};c < numChannels;++c)
    {
        /* Load the samples from the buffer, and resample to match the device. */
        LoadSamples(srcsamples.get(), sampleData + bytesPerSample*c, numChannels, sampleType,
            numSamples);
        if(device->Frequency != sampleRate)
            resampler.process(numSamples, srcsamples.get(), resampledCount, srcsamples.get());

        size_t done{0};
        complex_d *filteriter = filter->mConvolveFilter[c];
        for(size_t s{0};s < filter->mNumConvolveSegs;++s)
        {
            const size_t todo{minz(resampledCount-done, ConvolveUpdateSamples)};

            auto iter = std::copy_n(&srcsamples[done], todo, fftbuffer->begin());
            done += todo;
            std::fill(iter, fftbuffer->end(), complex_d{});

            complex_fft(*fftbuffer, -1.0);
            filteriter = std::copy_n(fftbuffer->cbegin(), m, filteriter);
        }
    }

    return filter.release();
}

void ConvolutionState::update(const ALCcontext* /*context*/, const ALeffectslot *slot,
    const EffectProps* /*props*/, const EffectTarget target)
{
    mFilter = static_cast<ConvolutionFilter*>(slot->Params.mEffectBuffer);
    mNumChannels = ChannelsFromFmt(mFilter->mChannels, 1);

    /* The iFFT'd response is scaled up by the number of bins, so apply the
     * inverse to the output mixing gain.
     */
    constexpr size_t m{ConvolveUpdateSize/2 + 1};
    const float gain{slot->Params.Gain * (1.0f/m)};
    if(mFilter->mChannels == FmtStereo)
    {
        /* TODO: Add a "direct channels" setting for this effect? */
        const ALuint lidx{!target.RealOut ? INVALID_CHANNEL_INDEX :
            GetChannelIdxByName(*target.RealOut, FrontLeft)};
        const ALuint ridx{!target.RealOut ? INVALID_CHANNEL_INDEX :
            GetChannelIdxByName(*target.RealOut, FrontRight)};
        if(lidx != INVALID_CHANNEL_INDEX && ridx != INVALID_CHANNEL_INDEX)
        {
            mOutTarget = target.RealOut->Buffer;
            mGains[0].Target[lidx] = gain;
            mGains[1].Target[ridx] = gain;
        }
        else
        {
            const auto lcoeffs = CalcDirectionCoeffs({-1.0f, 0.0f, 0.0f}, 0.0f);
            const auto rcoeffs = CalcDirectionCoeffs({ 1.0f, 0.0f, 0.0f}, 0.0f);

            mOutTarget = target.Main->Buffer;
            ComputePanGains(target.Main, lcoeffs.data(), gain, mGains[0].Target);
            ComputePanGains(target.Main, rcoeffs.data(), gain, mGains[1].Target);
        }
    }
    else if(mFilter->mChannels == FmtMono)
    {
        const auto coeffs = CalcDirectionCoeffs({0.0f, 0.0f, -1.0f}, 0.0f);

        mOutTarget = target.Main->Buffer;
        ComputePanGains(target.Main, coeffs.data(), gain, mGains[0].Target);
    }
}

void ConvolutionState::process(const size_t samplesToDo,
    const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    /* No filter, no response. */
    if(!mFilter) return;

    for(size_t base{0u};base < samplesToDo;)
    {
        const size_t todo{minz(ConvolveUpdateSamples-mFifoPos, samplesToDo-base)};

        /* Retrieve the output samples from the FIFO and fill in the new input
         * samples.
         */
        for(size_t c{0};c < mNumChannels;++c)
        {
            auto fifo_iter = mOutput[c].begin() + mFifoPos;
            std::transform(fifo_iter, fifo_iter+todo, mTempBuffer[c].begin()+base,
                [](double d) noexcept -> float { return static_cast<float>(d); });
        }

        std::copy_n(samplesIn[0].begin()+base, todo, mFftBuffer.begin()+mFifoPos);
        mFifoPos += todo;
        base += todo;

        /* Check whether FIFO buffer is filled with new samples. */
        if(mFifoPos < ConvolveUpdateSamples) break;
        mFifoPos = 0;

        /* Calculate the frequency domain response and add the relevant
         * frequency bins to the input history.
         */
        complex_fft(mFftBuffer, -1.0);

        constexpr size_t m{ConvolveUpdateSize/2 + 1};
        std::copy_n(mFftBuffer.begin(), m, mFilter->mInputHistory);
        mFftBuffer.fill(complex_d{});

        for(size_t c{0};c < mNumChannels;++c)
        {
            /* Convolve each input segment with its IR filter counterpart
             * (aligned in time).
             */
            for(size_t s{0};s < mFilter->mNumConvolveSegs;++s)
            {
                const complex_d *RESTRICT input{&mFilter->mInputHistory[s*m]};
                const complex_d *RESTRICT filter{&mFilter->mConvolveFilter[c][s*m]};
                for(size_t i{0};i < m;++i)
                    mFftBuffer[i] += input[i] * filter[i];
            }

            /* Apply iFFT to get the 1024 (really 1023) samples for output. The
             * 512 output samples are combined with the last output's 511
             * second-half samples (and this output's second half is
             * subsequently saved for next time).
             */
            complex_fft(mFftBuffer, 1.0);

            for(size_t i{0};i < ConvolveUpdateSamples;++i)
                mOutput[c][i] = mFftBuffer[i].real() + mOutput[c][ConvolveUpdateSamples+i];
            for(size_t i{0};i < ConvolveUpdateSamples;++i)
                mOutput[c][ConvolveUpdateSamples+i] = mFftBuffer[ConvolveUpdateSamples+i].real();
            mFftBuffer.fill(complex_d{});
        }

        /* Shift the input history. */
        std::copy_backward(mFilter->mInputHistory,
            mFilter->mInputHistory + (mFilter->mNumConvolveSegs-1)*m,
            mFilter->mInputHistory + mFilter->mNumConvolveSegs*m);
    }

    /* Finally, mix to the output. */
    for(size_t c{0};c < mNumChannels;++c)
        MixSamples({mTempBuffer[c].data(), samplesToDo}, samplesOut, mGains[c].Current,
            mGains[c].Target, samplesToDo, 0);
}


void ConvolutionEffect_setParami(EffectProps* /*props*/, ALenum param, int /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x",
            param};
    }
}
void ConvolutionEffect_setParamiv(EffectProps *props, ALenum param, const int *vals)
{
    switch(param)
    {
    default:
        ConvolutionEffect_setParami(props, param, vals[0]);
    }
}
void ConvolutionEffect_setParamf(EffectProps* /*props*/, ALenum param, float /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect float property 0x%04x",
            param};
    }
}
void ConvolutionEffect_setParamfv(EffectProps *props, ALenum param, const float *vals)
{
    switch(param)
    {
    default:
        ConvolutionEffect_setParamf(props, param, vals[0]);
    }
}

void ConvolutionEffect_getParami(const EffectProps* /*props*/, ALenum param, int* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x",
            param};
    }
}
void ConvolutionEffect_getParamiv(const EffectProps *props, ALenum param, int *vals)
{
    switch(param)
    {
    default:
        ConvolutionEffect_getParami(props, param, vals);
    }
}
void ConvolutionEffect_getParamf(const EffectProps* /*props*/, ALenum param, float* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect float property 0x%04x",
            param};
    }
}
void ConvolutionEffect_getParamfv(const EffectProps *props, ALenum param, float *vals)
{
    switch(param)
    {
    default:
        ConvolutionEffect_getParamf(props, param, vals);
    }
}

DEFINE_ALEFFECT_VTABLE(ConvolutionEffect);


struct ConvolutionStateFactory final : public EffectStateFactory {
    EffectState *create() override;
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override;
};

/* Creates EffectState objects of the appropriate type. */
EffectState *ConvolutionStateFactory::create()
{ return new ConvolutionState{}; }

/* Returns an ALeffectProps initialized with this effect type's default
 * property values.
 */
EffectProps ConvolutionStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    return props;
}

/* Returns a pointer to this effect type's global set/get vtable. */
const EffectVtable *ConvolutionStateFactory::getEffectVtable() const noexcept
{ return &ConvolutionEffect_vtable; }

} // namespace

EffectStateFactory *ConvolutionStateFactory_getFactory()
{
    static ConvolutionStateFactory ConvolutionFactory{};
    return &ConvolutionFactory;
}
