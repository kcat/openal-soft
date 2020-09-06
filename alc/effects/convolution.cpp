
#include "config.h"

#include "AL/al.h"
#include "AL/alc.h"

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcomplex.h"
#include "alcontext.h"
#include "almalloc.h"
#include "alspan.h"
#include "ambidefs.h"
#include "bformatdec.h"
#include "buffer_storage.h"
#include "effects/base.h"
#include "filters/splitter.h"
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


auto GetAmbiScales(AmbiScaling scaletype) noexcept -> const std::array<float,MAX_AMBI_CHANNELS>&
{
    if(scaletype == AmbiScaling::FuMa) return AmbiScale::FromFuMa;
    if(scaletype == AmbiScaling::SN3D) return AmbiScale::FromSN3D;
    return AmbiScale::FromN3D;
}

auto GetAmbiLayout(AmbiLayout layouttype) noexcept -> const std::array<uint8_t,MAX_AMBI_CHANNELS>&
{
    if(layouttype == AmbiLayout::FuMa) return AmbiIndex::FromFuMa;
    return AmbiIndex::FromACN;
}

auto GetAmbi2DLayout(AmbiLayout layouttype) noexcept -> const std::array<uint8_t,MAX_AMBI2D_CHANNELS>&
{
    if(layouttype == AmbiLayout::FuMa) return AmbiIndex::FromFuMa2D;
    return AmbiIndex::From2D;
}


using complex_d = std::complex<double>;

constexpr size_t ConvolveUpdateSize{1024};
constexpr size_t ConvolveUpdateSamples{ConvolveUpdateSize / 2};

struct ConvolutionFilter {
    FmtChannels mChannels{};
    AmbiLayout mAmbiLayout{};
    AmbiScaling mAmbiScaling{};
    ALuint mAmbiOrder{};

    size_t mFifoPos{0};
    al::vector<std::array<double,ConvolveUpdateSamples*2>,16> mOutput;
    alignas(16) std::array<complex_d,ConvolveUpdateSize> mFftBuffer{};

    size_t mCurrentSegment{0};
    size_t mNumConvolveSegs{0};

    struct ChannelData {
        alignas(16) FloatBufferLine mBuffer{};
        float mHfScale{};
        BandSplitter mFilter{};
        float Current[MAX_OUTPUT_CHANNELS]{};
        float Target[MAX_OUTPUT_CHANNELS]{};
    };
    using ChannelDataArray = al::FlexArray<ChannelData>;
    std::unique_ptr<ChannelDataArray> mChans;
    std::unique_ptr<complex_d[]> mComplexData;

    ConvolutionFilter(size_t numChannels) : mChans{ChannelDataArray::Create(numChannels)}
    { }

    bool init(const ALCdevice *device, const BufferStorage &buffer);

    void NormalMix(const al::span<FloatBufferLine> samplesOut, const size_t samplesToDo);
    void UpsampleMix(const al::span<FloatBufferLine> samplesOut, const size_t samplesToDo);
    void (ConvolutionFilter::*mMix)(const al::span<FloatBufferLine>,const size_t)
    {&ConvolutionFilter::NormalMix};

    void update(al::span<FloatBufferLine> &outTarget, const ALCcontext *context,
        const ALeffectslot *slot, const EffectProps *props, const EffectTarget target);
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn,
        const al::span<FloatBufferLine> samplesOut);

    DEF_NEWDEL(ConvolutionFilter)
};

bool ConvolutionFilter::init(const ALCdevice *device, const BufferStorage &buffer)
{
    constexpr size_t m{ConvolveUpdateSize/2 + 1};

    /* FIXME: Support anything. */
    if(buffer.mChannels != FmtMono && buffer.mChannels != FmtStereo
        && buffer.mChannels != FmtBFormat2D && buffer.mChannels != FmtBFormat3D)
        return false;
    if((buffer.mChannels == FmtBFormat2D || buffer.mChannels == FmtBFormat3D)
        && buffer.mAmbiOrder > 1)
        return false;

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

    auto bytesPerSample = BytesFromFmt(buffer.mType);
    auto realChannels = ChannelsFromFmt(buffer.mChannels, buffer.mAmbiOrder);
    auto numChannels = mChans->size();

    const BandSplitter splitter{400.0f / static_cast<float>(device->Frequency)};
    for(auto &e : *mChans)
        e.mFilter = splitter;

    mOutput.resize(numChannels, {});

    /* Calculate the number of segments needed to hold the impulse response and
     * the input history (rounded up), and allocate them.
     */
    mNumConvolveSegs = (resampledCount+(ConvolveUpdateSamples-1)) / ConvolveUpdateSamples;

    const size_t complex_length{mNumConvolveSegs * m * (numChannels+1)};
    mComplexData = std::make_unique<complex_d[]>(complex_length);
    std::fill_n(mComplexData.get(), complex_length, complex_d{});

    mChannels = buffer.mChannels;
    mAmbiLayout = buffer.mAmbiLayout;
    mAmbiScaling = buffer.mAmbiScaling;
    mAmbiOrder = buffer.mAmbiOrder;

    auto fftbuffer = std::make_unique<std::array<complex_d,ConvolveUpdateSize>>();
    auto srcsamples = std::make_unique<double[]>(maxz(buffer.mSampleLen, resampledCount));
    complex_d *filteriter = mComplexData.get() + mNumConvolveSegs*m;
    for(size_t c{0};c < numChannels;++c)
    {
        /* Load the samples from the buffer, and resample to match the device. */
        LoadSamples(srcsamples.get(), buffer.mData.data() + bytesPerSample*c, realChannels,
            buffer.mType, buffer.mSampleLen);
        if(device->Frequency != buffer.mSampleRate)
            resampler.process(buffer.mSampleLen, srcsamples.get(), resampledCount,
                srcsamples.get());

        size_t done{0};
        for(size_t s{0};s < mNumConvolveSegs;++s)
        {
            const size_t todo{minz(resampledCount-done, ConvolveUpdateSamples)};

            auto iter = std::copy_n(&srcsamples[done], todo, fftbuffer->begin());
            done += todo;
            std::fill(iter, fftbuffer->end(), complex_d{});

            complex_fft(*fftbuffer, -1.0);
            filteriter = std::copy_n(fftbuffer->cbegin(), m, filteriter);
        }
    }
    return true;
}

void ConvolutionFilter::NormalMix(const al::span<FloatBufferLine> samplesOut,
    const size_t samplesToDo)
{
    for(auto &chan : *mChans)
        MixSamples({chan.mBuffer.data(), samplesToDo}, samplesOut, chan.Current, chan.Target,
            samplesToDo, 0);
}

void ConvolutionFilter::UpsampleMix(const al::span<FloatBufferLine> samplesOut,
    const size_t samplesToDo)
{
    for(auto &chan : *mChans)
    {
        const al::span<float> src{chan.mBuffer.data(), samplesToDo};
        chan.mFilter.processHfScale(src, chan.mHfScale);
        MixSamples(src, samplesOut, chan.Current, chan.Target, samplesToDo, 0);
    }
}

void ConvolutionFilter::update(al::span<FloatBufferLine> &outTarget, const ALCcontext *context,
    const ALeffectslot *slot, const EffectProps* /*props*/, const EffectTarget target)
{
    ALCdevice *device{context->mDevice.get()};
    mMix = &ConvolutionFilter::NormalMix;

    /* The iFFT'd response is scaled up by the number of bins, so apply the
     * inverse to the output mixing gain.
     */
    constexpr size_t m{ConvolveUpdateSize/2 + 1};
    const float gain{slot->Params.Gain * (1.0f/m)};
    auto &chans = *mChans;
    if(mChannels == FmtBFormat3D || mChannels == FmtBFormat2D)
    {
        if(device->mAmbiOrder > mAmbiOrder)
        {
            mMix = &ConvolutionFilter::UpsampleMix;
            const auto scales = BFormatDec::GetHFOrderScales(mAmbiOrder, device->mAmbiOrder);
            chans[0].mHfScale = scales[0];
            for(size_t i{1};i < chans.size();++i)
                chans[i].mHfScale = scales[1];
        }
        outTarget = target.Main->Buffer;

        const auto &scales = GetAmbiScales(mAmbiScaling);
        const uint8_t *index_map{(mChannels == FmtBFormat2D) ?
            GetAmbi2DLayout(mAmbiLayout).data() :
            GetAmbiLayout(mAmbiLayout).data()};

        std::array<float,MAX_AMBI_CHANNELS> coeffs{};
        for(size_t c{0u};c < chans.size();++c)
        {
            const size_t acn{index_map[c]};
            coeffs[acn] = scales[acn];
            ComputePanGains(target.Main, coeffs.data(), gain, chans[c].Target);
            coeffs[acn] = 0.0f;
        }
    }
    else if(mChannels == FmtStereo)
    {
        /* TODO: Add a "direct channels" setting for this effect? */
        const ALuint lidx{!target.RealOut ? INVALID_CHANNEL_INDEX :
            GetChannelIdxByName(*target.RealOut, FrontLeft)};
        const ALuint ridx{!target.RealOut ? INVALID_CHANNEL_INDEX :
            GetChannelIdxByName(*target.RealOut, FrontRight)};
        if(lidx != INVALID_CHANNEL_INDEX && ridx != INVALID_CHANNEL_INDEX)
        {
            outTarget = target.RealOut->Buffer;
            chans[0].Target[lidx] = gain;
            chans[1].Target[ridx] = gain;
        }
        else
        {
            const auto lcoeffs = CalcDirectionCoeffs({-1.0f, 0.0f, 0.0f}, 0.0f);
            const auto rcoeffs = CalcDirectionCoeffs({ 1.0f, 0.0f, 0.0f}, 0.0f);

            outTarget = target.Main->Buffer;
            ComputePanGains(target.Main, lcoeffs.data(), gain, chans[0].Target);
            ComputePanGains(target.Main, rcoeffs.data(), gain, chans[1].Target);
        }
    }
    else if(mChannels == FmtMono)
    {
        const auto coeffs = CalcDirectionCoeffs({0.0f, 0.0f, -1.0f}, 0.0f);

        outTarget = target.Main->Buffer;
        ComputePanGains(target.Main, coeffs.data(), gain, chans[0].Target);
    }
}

void ConvolutionFilter::process(const size_t samplesToDo,
    const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    constexpr size_t m{ConvolveUpdateSize/2 + 1};
    size_t curseg{mCurrentSegment};
    auto &chans = *mChans;

    for(size_t base{0u};base < samplesToDo;)
    {
        const size_t todo{minz(ConvolveUpdateSamples-mFifoPos, samplesToDo-base)};

        /* Retrieve the output samples from the FIFO and fill in the new input
         * samples.
         */
        for(size_t c{0};c < chans.size();++c)
        {
            auto fifo_iter = mOutput[c].begin() + mFifoPos;
            std::transform(fifo_iter, fifo_iter+todo, chans[c].mBuffer.begin()+base,
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

        std::copy_n(mFftBuffer.begin(), m, &mComplexData[curseg*m]);
        mFftBuffer.fill(complex_d{});

        const complex_d *RESTRICT filter{mComplexData.get() + mNumConvolveSegs*m};
        for(size_t c{0};c < chans.size();++c)
        {
            /* Convolve each input segment with its IR filter counterpart
             * (aligned in time).
             */
            const complex_d *RESTRICT input{&mComplexData[curseg*m]};
            for(size_t s{curseg};s < mNumConvolveSegs;++s)
            {
                for(size_t i{0};i < m;++i,++input,++filter)
                    mFftBuffer[i] += *input * *filter;
            }
            input = mComplexData.get();
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
        curseg = curseg ? (curseg-1) : (mNumConvolveSegs-1);
    }
    mCurrentSegment = curseg;

    /* Finally, mix to the output. */
    (this->*mMix)(samplesOut, samplesToDo);
}


struct ConvolutionState final : public EffectState {
    std::unique_ptr<ConvolutionFilter> mFilter;

    ConvolutionState() = default;
    ~ConvolutionState() override = default;

    void deviceUpdate(const ALCdevice *device) override;
    void setBuffer(const ALCdevice *device, const BufferStorage *buffer) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(ConvolutionState)
};

void ConvolutionState::deviceUpdate(const ALCdevice* /*device*/)
{
}

void ConvolutionState::setBuffer(const ALCdevice *device, const BufferStorage *buffer)
{
    mFilter = nullptr;
    /* An empty buffer doesn't need a convolution filter. */
    if(!buffer || buffer->mSampleLen < 1) return;

    auto numChannels = ChannelsFromFmt(buffer->mChannels,
        minu(buffer->mAmbiOrder, device->mAmbiOrder));

    mFilter.reset(new ConvolutionFilter{numChannels});
    if(!mFilter->init(device, *buffer))
        mFilter = nullptr;
}


void ConvolutionState::update(const ALCcontext *context, const ALeffectslot *slot,
    const EffectProps *props, const EffectTarget target)
{
    if(mFilter)
        mFilter->update(mOutTarget, context, slot, props, target);
}

void ConvolutionState::process(const size_t samplesToDo,
    const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    if(mFilter)
        mFilter->process(samplesToDo, samplesIn, samplesOut);
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
