
#include "config.h"

#include "oboe.h"

#include <cassert>
#include <cstring>
#include <stdint.h>

#include "alnumeric.h"
#include "core/device.h"
#include "core/logging.h"
#include "ringbuffer.h"

#include "oboe/Oboe.h"


namespace {

constexpr char device_name[] = "Oboe Default";


struct OboePlayback final : public BackendBase, public oboe::AudioStreamCallback {
    OboePlayback(DeviceBase *device) : BackendBase{device} { }

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
    oboe::ManagedStream stream;
    oboe::Result result{oboe::AudioStreamBuilder{}.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->openManagedStream(stream)};
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
    {
        builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::High);
        builder.setSampleRate(static_cast<int32_t>(mDevice->Frequency));
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

    if(static_cast<uint>(mStream->getChannelCount()) != mDevice->channelsFromFmt())
    {
        if(mStream->getChannelCount() >= 2)
            mDevice->FmtChans = DevFmtStereo;
        else if(mStream->getChannelCount() == 1)
            mDevice->FmtChans = DevFmtMono;
        else
            throw al::backend_exception{al::backend_error::DeviceError,
                "Got unhandled channel count: %d", mStream->getChannelCount()};
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


struct OboeCapture final : public BackendBase, public oboe::AudioStreamCallback {
    OboeCapture(DeviceBase *device) : BackendBase{device} { }

    oboe::ManagedStream mStream;

    RingBufferPtr mRing{nullptr};

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream, void *audioData,
        int32_t numFrames) override;

    void open(const char *name) override;
    void start() override;
    void stop() override;
    void captureSamples(al::byte *buffer, uint samples) override;
    uint availableSamples() override;
};

oboe::DataCallbackResult OboeCapture::onAudioReady(oboe::AudioStream*, void *audioData,
    int32_t numFrames)
{
    mRing->write(audioData, static_cast<uint32_t>(numFrames));
    return oboe::DataCallbackResult::Continue;
}


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
        ->setSampleRate(static_cast<int32_t>(mDevice->Frequency))
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
    case DevFmtX3D71:
    case DevFmtAmbi3D:
        throw al::backend_exception{al::backend_error::DeviceError, "%s capture not supported",
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
            "%s capture samples not supported", DevFmtTypeString(mDevice->FmtType)};
    }

    oboe::Result result{builder.openManagedStream(mStream)};
    if(result != oboe::Result::OK)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to create stream: %s",
            oboe::convertToText(result)};

    TRACE("Got stream with properties:\n%s", oboe::convertToText(mStream.get()));

    /* Ensure a minimum ringbuffer size of 100ms. */
    mRing = RingBuffer::Create(maxu(mDevice->BufferSize, mDevice->Frequency/10),
        static_cast<uint32_t>(mStream->getBytesPerFrame()), false);

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
{ return static_cast<uint>(mRing->readSpace()); }

void OboeCapture::captureSamples(al::byte *buffer, uint samples)
{ mRing->read(buffer, samples); }

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

BackendPtr OboeBackendFactory::createBackend(DeviceBase *device, BackendType type)
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
