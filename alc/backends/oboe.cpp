
#include "config.h"

#include "oboe.h"

#include <cassert>
#include <cstring>

#include "alu.h"
#include "core/logging.h"

#include "oboe/Oboe.h"


namespace {

constexpr char device_name[] = "Oboe Default";


struct OboePlayback final : public BackendBase, public oboe::AudioStreamCallback {
    OboePlayback(ALCdevice *device) : BackendBase{device} { }

    oboe::ManagedStream mStream;

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream, void *audioData,
        int32_t numFrames) override;

    void open(const char *name) override;
    bool reset() override;
    void start() override;
    void stop() override;
};


oboe::DataCallbackResult OboePlayback::onAudioReady(oboe::AudioStream *oboeStream, void *audioData,
    int32_t numFrames)
{
    assert(numFrames > 0);
    const int32_t numChannels{oboeStream->getChannelCount()};

    if UNLIKELY(numChannels > 2 && mDevice->FmtChans == DevFmtStereo)
    {
        /* If the device is only mixing stereo but there's more than two
         * output channels, there are unused channels that need to be silenced.
         */
        if(mStream->getFormat() == oboe::AudioFormat::Float)
            memset(audioData, 0, static_cast<uint32_t>(numFrames*numChannels)*sizeof(float));
        else
            memset(audioData, 0, static_cast<uint32_t>(numFrames*numChannels)*sizeof(int16_t));
    }

    mDevice->renderSamples(audioData, static_cast<uint32_t>(numFrames),
        static_cast<uint32_t>(numChannels));
    return oboe::DataCallbackResult::Continue;
}


void OboePlayback::open(const char *name)
{
    if(!name)
        name = device_name;
    else if(std::strcmp(name, device_name) != 0)
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"%s\" not found",
            name};

    /* Open a basic output stream, just to ensure it can work. */
    oboe::Result result{oboe::AudioStreamBuilder{}.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->openManagedStream(mStream)};
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to create stream: %s",
            oboe::convertToText(result)};

    mDevice->DeviceName = name;
}

bool OboePlayback::reset()
{
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    /* Don't let Oboe convert. We should be able to handle anything it gives
     * back.
     */
    builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::None);
    builder.setChannelConversionAllowed(false);
    builder.setFormatConversionAllowed(false);
    builder.setCallback(this);

    if(mDevice->Flags.test(FrequencyRequest))
        builder.setSampleRate(static_cast<int32_t>(mDevice->Frequency));
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
        case DevFmtFloat:
            format = oboe::AudioFormat::Float;
            break;
        }
        builder.setFormat(format);
    }

    oboe::Result result{builder.openManagedStream(mStream)};
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
        result = builder.openManagedStream(mStream);
    }
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to create stream: %s",
            oboe::convertToText(result)};
    mStream->setBufferSizeInFrames(mini(static_cast<int32_t>(mDevice->BufferSize),
        mStream->getBufferCapacityInFrames()));
    TRACE("Got stream with properties:\n%s", oboe::convertToText(mStream.get()));

    switch(mStream->getChannelCount())
    {
    case oboe::ChannelCount::Mono:
        mDevice->FmtChans = DevFmtMono;
        break;
    case oboe::ChannelCount::Stereo:
        mDevice->FmtChans = DevFmtStereo;
        break;
    /* Other potential configurations. Could be wrong, but better than failing.
     * Assume WFX channel order.
     */
    case 4:
        mDevice->FmtChans = DevFmtQuad;
        break;
    case 6:
        mDevice->FmtChans = DevFmtX51Rear;
        break;
    case 7:
        mDevice->FmtChans = DevFmtX61;
        break;
    case 8:
        mDevice->FmtChans = DevFmtX71;
        break;
    default:
        if(mStream->getChannelCount() < 1)
            throw al::backend_exception{al::backend_error::DeviceError,
                "Got unhandled channel count: %d", mStream->getChannelCount()};
        /* Assume first two channels are front left/right. We can do a stereo
         * mix and keep the other channels silent.
         */
        mDevice->FmtChans = DevFmtStereo;
        break;
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
    case oboe::AudioFormat::Unspecified:
    case oboe::AudioFormat::Invalid:
        throw al::backend_exception{al::backend_error::DeviceError,
            "Got unhandled sample type: %s", oboe::convertToText(mStream->getFormat())};
    }
    mDevice->Frequency = static_cast<uint32_t>(mStream->getSampleRate());

    /* Ensure the period size is no less than 10ms. It's possible for FramesPerCallback to be 0
     * indicating variable updates, but OpenAL should have a reasonable minimum update size set.
     * FramesPerBurst may not necessarily be correct, but hopefully it can act as a minimum
     * update size.
     */
    mDevice->UpdateSize = maxu(mDevice->Frequency / 100,
        static_cast<uint32_t>(mStream->getFramesPerBurst()));
    mDevice->BufferSize = maxu(mDevice->UpdateSize * 2,
        static_cast<uint32_t>(mStream->getBufferSizeInFrames()));

    return true;
}

void OboePlayback::start()
{
    const oboe::Result result{mStream->start()};
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to start stream: %s",
            oboe::convertToText(result)};
}

void OboePlayback::stop()
{
    oboe::Result result{mStream->stop()};
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to stop stream: %s",
            oboe::convertToText(result)};
}


struct OboeCapture final : public BackendBase {
    OboeCapture(ALCdevice *device) : BackendBase{device} { }

    oboe::ManagedStream mStream;

    void open(const char *name) override;
    void start() override;
    void stop() override;
    void captureSamples(al::byte *buffer, uint samples) override;
    uint availableSamples() override;
};

void OboeCapture::open(const char *name)
{
    if(!name)
        name = device_name;
    else if(std::strcmp(name, device_name) != 0)
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"%s\" not found",
            name};

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::High)
        ->setChannelConversionAllowed(true)
        ->setFormatConversionAllowed(true)
        ->setBufferCapacityInFrames(static_cast<int32_t>(mDevice->BufferSize))
        ->setSampleRate(static_cast<int32_t>(mDevice->Frequency));
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
    case DevFmtX51Rear:
    case DevFmtX61:
    case DevFmtX71:
    case DevFmtAmbi3D:
        throw al::backend_exception{al::backend_error::DeviceError, "%s capture not supported",
            DevFmtChannelsString(mDevice->FmtChans)};
    }

    /* FIXME: This really should support UByte, but Oboe doesn't. We'll need to
     * use a temp buffer and convert.
     */
    switch(mDevice->FmtType)
    {
    case DevFmtShort:
        builder.setFormat(oboe::AudioFormat::I16);
        break;
    case DevFmtFloat:
        builder.setFormat(oboe::AudioFormat::Float);
        break;
    case DevFmtByte:
    case DevFmtUByte:
    case DevFmtUShort:
    case DevFmtInt:
    case DevFmtUInt:
        throw al::backend_exception{al::backend_error::DeviceError,
            "%s capture samples not supported", DevFmtTypeString(mDevice->FmtType)};
    }

    oboe::Result result{builder.openManagedStream(mStream)};
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to create stream: %s",
            oboe::convertToText(result)};
    if(static_cast<int32_t>(mDevice->BufferSize) > mStream->getBufferCapacityInFrames())
        throw al::backend_exception{al::backend_error::DeviceError,
            "Buffer size too large (%u > %d)", mDevice->BufferSize,
            mStream->getBufferCapacityInFrames()};
    auto buffer_result = mStream->setBufferSizeInFrames(static_cast<int32_t>(mDevice->BufferSize));
    if(!buffer_result)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to set buffer size: %s", oboe::convertToText(buffer_result.error())};
    else if(buffer_result.value() < static_cast<int32_t>(mDevice->BufferSize))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to set large enough buffer size (%u > %d)", mDevice->BufferSize,
            buffer_result.value()};
    mDevice->BufferSize = static_cast<uint>(buffer_result.value());

    TRACE("Got stream with properties:\n%s", oboe::convertToText(mStream.get()));

    mDevice->DeviceName = name;
}

void OboeCapture::start()
{
    const oboe::Result result{mStream->start()};
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to start stream: %s",
            oboe::convertToText(result)};
}

void OboeCapture::stop()
{
    const oboe::Result result{mStream->stop()};
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to stop stream: %s",
            oboe::convertToText(result)};
}

uint OboeCapture::availableSamples()
{
    auto result = mStream->getAvailableFrames();
    /* FIXME: This shouldn't report less samples than have been previously
     * reported and not captured.
     */
    if(!result) return 0;
    return static_cast<uint>(result.value());
}

void OboeCapture::captureSamples(al::byte *buffer, uint samples)
{
    auto result = mStream->read(buffer, static_cast<int32_t>(samples), 0);
    uint got{bool{result} ? static_cast<uint>(result.value()) : 0u};
    if(got < samples)
    {
        auto frame_size = static_cast<uint>(mStream->getBytesPerFrame());
        std::fill_n(buffer + got*frame_size, (samples-got)*frame_size, al::byte{});
    }
}

} // namespace

bool OboeBackendFactory::init() { return true; }

bool OboeBackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback || type == BackendType::Capture; }

std::string OboeBackendFactory::probe(BackendType type)
{
    switch(type)
    {
    case BackendType::Playback:
    case BackendType::Capture:
        /* Includes null char. */
        return std::string{device_name, sizeof(device_name)};
    }
    return std::string{};
}

BackendPtr OboeBackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new OboePlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new OboeCapture{device}};
    return BackendPtr{};
}

BackendFactory &OboeBackendFactory::getFactory()
{
    static OboeBackendFactory factory{};
    return factory;
}
