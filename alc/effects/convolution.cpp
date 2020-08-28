
#include "config.h"

#include "AL/al.h"
#include "AL/alc.h"

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcomplex.h"
#include "alcontext.h"
#include "almalloc.h"
#include "alspan.h"
#include "buffer_storage.h"
#include "effects/base.h"
#include "fmt_traits.h"
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


void LoadSamples(double *RESTRICT dst, const al::byte *src, const size_t srcstep, FmtType srctype,
    const size_t samples) noexcept
{
#define HANDLE_FMT(T)  case T: al::LoadSampleArray<T>(dst, src, srcstep, samples); break
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
    size_t mCurrentSegment{0};
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
    EffectBufferBase *createBuffer(const ALCdevice *device, const BufferStorage &buffer) override;
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
    const BufferStorage &buffer)
{
    /* An empty buffer doesn't need a convolution filter. */
    if(buffer.mSampleLen < 1) return nullptr;

    /* FIXME: Support anything. */
    if(buffer.mChannels != FmtMono && buffer.mChannels != FmtStereo)
        return nullptr;

    /* The impulse response needs to have the same sample rate as the input and
     * output. The bsinc24 resampler is decent, but there is high-frequency
     * attenation that some people may be able to pick up on. Since this is
     * very infrequent called, go ahead and use the polyphase resampler.
     */
    PPhaseResampler resampler;
    if(device->Frequency != buffer.mSampleRate)
        resampler.init(buffer.mSampleRate, device->Frequency);
    const auto resampledCount = static_cast<ALuint>(
        (uint64_t{buffer.mSampleLen}*device->Frequency + (buffer.mSampleRate-1)) /
        buffer.mSampleRate);

    al::intrusive_ptr<ConvolutionFilter> filter{new ConvolutionFilter{}};

    auto bytesPerSample = BytesFromFmt(buffer.mType);
    auto numChannels = ChannelsFromFmt(buffer.mChannels, buffer.mAmbiOrder);
    constexpr size_t m{ConvolveUpdateSize/2 + 1};

    /* Calculate the number of segments needed to hold the impulse response and
     * the input history (rounded up), and allocate them.
     */
    filter->mNumConvolveSegs = (buffer.mSampleLen+(ConvolveUpdateSamples-1)) /
        ConvolveUpdateSamples;

    const size_t complex_length{filter->mNumConvolveSegs * m * (numChannels+1)};
    filter->mComplexData = std::make_unique<complex_d[]>(complex_length);
    std::fill_n(filter->mComplexData.get(), complex_length, complex_d{});

    filter->mInputHistory = filter->mComplexData.get();
    filter->mConvolveFilter[0] = filter->mInputHistory + filter->mNumConvolveSegs*m;
    for(size_t c{1};c < numChannels;++c)
        filter->mConvolveFilter[c] = filter->mConvolveFilter[c-1] + filter->mNumConvolveSegs*m;

    filter->mChannels = buffer.mChannels;

    auto fftbuffer = std::make_unique<std::array<complex_d,ConvolveUpdateSize>>();
    auto srcsamples = std::make_unique<double[]>(maxz(buffer.mSampleLen, resampledCount));
    for(size_t c{0};c < numChannels;++c)
    {
        /* Load the samples from the buffer, and resample to match the device. */
        LoadSamples(srcsamples.get(), buffer.mData.data() + bytesPerSample*c, numChannels,
            buffer.mType, buffer.mSampleLen);
        if(device->Frequency != buffer.mSampleRate)
            resampler.process(buffer.mSampleLen, srcsamples.get(), resampledCount,
                srcsamples.get());

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

    constexpr size_t m{ConvolveUpdateSize/2 + 1};
    size_t curseg{mFilter->mCurrentSegment};

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

        std::copy_n(mFftBuffer.begin(), m, &mFilter->mInputHistory[curseg*m]);
        mFftBuffer.fill(complex_d{});

        for(size_t c{0};c < mNumChannels;++c)
        {
            /* Convolve each input segment with its IR filter counterpart
             * (aligned in time).
             */
            const complex_d *RESTRICT filter{mFilter->mConvolveFilter[c]};
            const complex_d *RESTRICT input{&mFilter->mInputHistory[curseg*m]};
            for(size_t s{curseg};s < mFilter->mNumConvolveSegs;++s)
            {
                for(size_t i{0};i < m;++i,++input,++filter)
                    mFftBuffer[i] += *input * *filter;
            }
            input = mFilter->mInputHistory;
            for(size_t s{0};s < curseg;++s)
            {
                for(size_t i{0};i < m;++i,++input,++filter)
                    mFftBuffer[i] += *input * *filter;
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
        curseg = curseg ? (curseg-1) : (mFilter->mNumConvolveSegs-1);
    }
    mFilter->mCurrentSegment = curseg;

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
