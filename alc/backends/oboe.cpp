
#include "config.h"

#include "oboe.h"

#include <cstring>

#include "alnumeric.h"
#include "alstring.h"
#include "core/device.h"
#include "core/logging.h"
#include "gsl/gsl"
#include "ringbuffer.h"

#include "oboe/Oboe.h"


namespace {

using namespace std::string_view_literals;

[[nodiscard]] constexpr auto GetDeviceName() noexcept { return "Oboe Default"sv; }


struct OboePlayback final : BackendBase, oboe::AudioStreamCallback {
    explicit OboePlayback(gsl::not_null<DeviceBase*> const device) : BackendBase{device} { }

    std::shared_ptr<oboe::AudioStream> mStream;

    auto onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames)
        -> oboe::DataCallbackResult override;

    void onErrorAfterClose(oboe::AudioStream* /* audioStream */, oboe::Result /* error */) override;

    void open(std::string_view name) override;
    auto reset() -> bool override;
    void start() override;
    void stop() override;
};


auto OboePlayback::onAudioReady(oboe::AudioStream *const oboeStream, void *const audioData,
    int32_t const numFrames) -> oboe::DataCallbackResult
{
    mDevice->renderSamples(audioData, gsl::narrow_cast<uint32_t>(numFrames),
        gsl::narrow_cast<uint32_t>(oboeStream->getChannelCount()));
    return oboe::DataCallbackResult::Continue;
}

void OboePlayback::onErrorAfterClose(oboe::AudioStream*, oboe::Result const error)
{
    if(error == oboe::Result::ErrorDisconnected)
        mDevice->handleDisconnect("Oboe AudioStream was disconnected: {}",
            oboe::convertToText(error));
    TRACE("Error was {}", oboe::convertToText(error));
}

void OboePlayback::open(std::string_view name)
{
    if(name.empty())
        name = GetDeviceName();
    else if(name != GetDeviceName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};

    /* Open a basic output stream, just to ensure it can work. */
    auto stream = std::shared_ptr<oboe::AudioStream>{};
    const auto result = oboe::AudioStreamBuilder{}.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->openStream(stream);
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to create stream: {}",
            oboe::convertToText(result)};

    mDeviceName = name;
}

auto OboePlayback::reset() -> bool
{
    auto builder = oboe::AudioStreamBuilder{};
    builder.setDirection(oboe::Direction::Output);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setUsage(oboe::Usage::Game);
    /* Don't let Oboe convert. We should be able to handle anything it gives
     * back.
     */
    builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::None);
    builder.setChannelConversionAllowed(false);
    builder.setFormatConversionAllowed(false);
    builder.setCallback(this);

    if(mDevice->Flags.test(FrequencyRequest))
    {
        builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::High);
        builder.setSampleRate(gsl::narrow_cast<int32_t>(mDevice->mSampleRate));
    }
    if(mDevice->Flags.test(ChannelsRequest))
    {
        /* Only use mono or stereo at user request. There's no telling what
         * other counts may be inferred as.
         */
        builder.setChannelCount((mDevice->FmtChans==DevFmtMono) ? oboe::ChannelCount::Mono
            : (mDevice->FmtChans==DevFmtStereo) ? oboe::ChannelCount::Stereo
            : oboe::ChannelCount::Unspecified);
    }
    if(mDevice->Flags.test(SampleTypeRequest))
    {
        oboe::AudioFormat format{oboe::AudioFormat::Unspecified};
        switch(mDevice->FmtType)
        {
        case DevFmtByte:
        case DevFmtUByte:
        case DevFmtShort:
        case DevFmtUShort:
            format = oboe::AudioFormat::I16;
            break;
        case DevFmtInt:
        case DevFmtUInt:
#if OBOE_VERSION_MAJOR > 1 || (OBOE_VERSION_MAJOR == 1 && OBOE_VERSION_MINOR >= 6)
            format = oboe::AudioFormat::I32;
            break;
#endif
        case DevFmtFloat:
            format = oboe::AudioFormat::Float;
            break;
        }
        builder.setFormat(format);
    }

    auto result = builder.openStream(mStream);
    /* If the format failed, try asking for the defaults. */
    while(result == oboe::Result::ErrorInvalidFormat)
    {
        if(builder.getFormat() != oboe::AudioFormat::Unspecified)
            builder.setFormat(oboe::AudioFormat::Unspecified);
        else if(builder.getSampleRate() != oboe::kUnspecified)
            builder.setSampleRate(oboe::kUnspecified);
        else if(builder.getChannelCount() != oboe::ChannelCount::Unspecified)
            builder.setChannelCount(oboe::ChannelCount::Unspecified);
        else
            break;
        result = builder.openStream(mStream);
    }
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to create stream: {}",
            oboe::convertToText(result)};
    mStream->setBufferSizeInFrames(std::min(gsl::narrow_cast<int32_t>(mDevice->mBufferSize),
        mStream->getBufferCapacityInFrames()));
    TRACE("Got stream with properties:\n{}", oboe::convertToText(mStream.get()));

    if(std::cmp_not_equal(mStream->getChannelCount(), mDevice->channelsFromFmt()))
    {
        if(mStream->getChannelCount() >= 2)
            mDevice->FmtChans = DevFmtStereo;
        else if(mStream->getChannelCount() == 1)
            mDevice->FmtChans = DevFmtMono;
        else
            throw al::backend_exception{al::backend_error::DeviceError,
                "Got unhandled channel count: {}", mStream->getChannelCount()};
    }
    setDefaultWFXChannelOrder();

    switch(mStream->getFormat())
    {
    case oboe::AudioFormat::I16:
        mDevice->FmtType = DevFmtShort;
        break;
    case oboe::AudioFormat::Float:
        mDevice->FmtType = DevFmtFloat;
        break;
#if OBOE_VERSION_MAJOR > 1 || (OBOE_VERSION_MAJOR == 1 && OBOE_VERSION_MINOR >= 6)
    case oboe::AudioFormat::I32:
        mDevice->FmtType = DevFmtInt;
        break;
    case oboe::AudioFormat::I24:
#endif
#if OBOE_VERSION_MAJOR > 1 || (OBOE_VERSION_MAJOR == 1 && OBOE_VERSION_MINOR >= 8)
    case oboe::AudioFormat::IEC61937:
#endif
    case oboe::AudioFormat::Unspecified:
    case oboe::AudioFormat::Invalid:
        throw al::backend_exception{al::backend_error::DeviceError,
            "Got unhandled sample type: {}", oboe::convertToText(mStream->getFormat())};
    }
    mDevice->mSampleRate = gsl::narrow_cast<u32>(mStream->getSampleRate());

    /* Ensure the period size is no less than 10ms. It's possible for FramesPerCallback to be 0
     * indicating variable updates, but OpenAL should have a reasonable minimum update size set.
     * FramesPerBurst may not necessarily be correct, but hopefully it can act as a minimum
     * update size.
     */
    mDevice->mUpdateSize = std::max(mDevice->mSampleRate/100u,
        gsl::narrow_cast<u32>(mStream->getFramesPerBurst()));
    mDevice->mBufferSize = std::max(mDevice->mUpdateSize*2u,
        gsl::narrow_cast<u32>(mStream->getBufferSizeInFrames()));

    return true;
}

void OboePlayback::start()
{
    if(const auto result = mStream->start(); result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to start stream: {}",
            oboe::convertToText(result)};
}

void OboePlayback::stop()
{
    if(const auto result = mStream->stop(); result != oboe::Result::OK)
        ERR("Failed to stop stream: {}", oboe::convertToText(result));
}


struct OboeCapture final : BackendBase, oboe::AudioStreamCallback {
    explicit OboeCapture(gsl::not_null<DeviceBase*> const device) : BackendBase{device} { }

    std::shared_ptr<oboe::AudioStream> mStream;

    RingBufferPtr<std::byte> mRing;

    auto onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames)
        -> oboe::DataCallbackResult override;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    auto availableSamples() -> usize override;
};

auto OboeCapture::onAudioReady(oboe::AudioStream*, void *const audioData, int32_t const numFrames)
    -> oboe::DataCallbackResult
{
    std::ignore = mRing->write(std::span{static_cast<const std::byte*>(audioData),
        gsl::narrow_cast<uint32_t>(numFrames)*mRing->getElemSize()});
    return oboe::DataCallbackResult::Continue;
}


void OboeCapture::open(std::string_view name)
{
    if(name.empty())
        name = GetDeviceName();
    else if(name != GetDeviceName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};

    auto builder = oboe::AudioStreamBuilder{};
    builder.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::High)
        ->setChannelConversionAllowed(true)
        ->setFormatConversionAllowed(true)
        ->setSampleRate(gsl::narrow_cast<int32_t>(mDevice->mSampleRate))
        ->setCallback(this);
    /* Only use mono or stereo at user request. There's no telling what
     * other counts may be inferred as.
     */
    switch(mDevice->FmtChans)
    {
    case DevFmtMono:
        builder.setChannelCount(oboe::ChannelCount::Mono);
        break;
    case DevFmtStereo:
        builder.setChannelCount(oboe::ChannelCount::Stereo);
        break;
    case DevFmtQuad:
    case DevFmtX51:
    case DevFmtX61:
    case DevFmtX71:
    case DevFmtX714:
    case DevFmtX7144:
    case DevFmtX3D71:
    case DevFmtAmbi3D:
        throw al::backend_exception{al::backend_error::DeviceError, "{} capture not supported",
            DevFmtChannelsString(mDevice->FmtChans)};
    }

    /* FIXME: This really should support UByte, but Oboe doesn't. We'll need to
     * convert.
     */
    switch(mDevice->FmtType)
    {
    case DevFmtShort:
        builder.setFormat(oboe::AudioFormat::I16);
        break;
    case DevFmtFloat:
        builder.setFormat(oboe::AudioFormat::Float);
        break;
    case DevFmtInt:
#if OBOE_VERSION_MAJOR > 1 || (OBOE_VERSION_MAJOR == 1 && OBOE_VERSION_MINOR >= 6)
        builder.setFormat(oboe::AudioFormat::I32);
        break;
#endif
    case DevFmtByte:
    case DevFmtUByte:
    case DevFmtUShort:
    case DevFmtUInt:
        throw al::backend_exception{al::backend_error::DeviceError,
            "{} capture samples not supported", DevFmtTypeString(mDevice->FmtType)};
    }

    if(const auto result = builder.openStream(mStream); result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to create stream: {}",
            oboe::convertToText(result)};

    TRACE("Got stream with properties:\n{}", oboe::convertToText(mStream.get()));

    /* Ensure a minimum ringbuffer size of 100ms. */
    mRing = RingBuffer<std::byte>::Create(
        std::max(mDevice->mBufferSize, mDevice->mSampleRate/10u),
        gsl::narrow_cast<u32>(mStream->getBytesPerFrame()), false);

    mDeviceName = name;
}

void OboeCapture::start()
{
    if(const auto result = mStream->start(); result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to start stream: {}",
            oboe::convertToText(result)};
}

void OboeCapture::stop()
{
    if(const auto result = mStream->stop(); result != oboe::Result::OK)
        ERR("Failed to stop stream: {}", oboe::convertToText(result));
}

auto OboeCapture::availableSamples() -> usize
{ return mRing->readSpace(); }

void OboeCapture::captureSamples(std::span<std::byte> const outbuffer)
{ std::ignore = mRing->read(outbuffer); }

} // namespace

auto OboeBackendFactory::init() -> bool { return true; }

auto OboeBackendFactory::querySupport(BackendType const type) -> bool
{ return type == BackendType::Playback || type == BackendType::Capture; }

auto OboeBackendFactory::enumerate(BackendType const type) -> std::vector<std::string>
{
    switch(type)
    {
    case BackendType::Playback:
    case BackendType::Capture:
        return std::vector{std::string{GetDeviceName()}};
    }
    return {};
}

auto OboeBackendFactory::createBackend(gsl::not_null<DeviceBase*> const device,
    BackendType const type) -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new OboePlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new OboeCapture{device}};
    return BackendPtr{};
}

auto OboeBackendFactory::getFactory() -> BackendFactory&
{
    static OboeBackendFactory factory{};
    return factory;
}
