
#include "config.h"

#include "oboe.h"

#include <cassert>
#include <cstring>

#include "alu.h"
#include "logging.h"

#include "oboe/Oboe.h"


namespace {

constexpr char device_name[] = "Oboe Default";


struct OboePlayback final : public BackendBase, public oboe::AudioStreamCallback {
    OboePlayback(ALCdevice *device) : BackendBase{device} { }

    oboe::ManagedStream mStream;

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream, void *audioData,
        int32_t numFrames) override;

    void open(const ALCchar *name) override;
    bool reset() override;
    bool start() override;
    void stop() override;
};


oboe::DataCallbackResult OboePlayback::onAudioReady(oboe::AudioStream *oboeStream, void *audioData,
    int32_t numFrames)
{
    assert(numFrames > 0);
    aluMixData(mDevice, audioData, static_cast<uint32_t>(numFrames),
        static_cast<uint32_t>(oboeStream->getChannelCount()));
    return oboe::DataCallbackResult::Continue;
}


void OboePlayback::open(const ALCchar *name)
{
    if(!name)
        name = device_name;
    else if(std::strcmp(name, device_name) != 0)
        throw al::backend_exception{ALC_INVALID_VALUE, "Device name \"%s\" not found", name};

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);

    oboe::Result result{builder.openManagedStream(mStream)};
    if(result != oboe::Result::OK)
        throw al::backend_exception{ALC_INVALID_VALUE, "Failed to create stream. Error: %s",
            oboe::convertToText(result)};
}

bool OboePlayback::reset()
{
    mStream = nullptr;

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setCallback(this);

    if(mDevice->Flags.get<FrequencyRequest>())
        builder.setSampleRate(static_cast<int32_t>(mDevice->Frequency));
    if(mDevice->Flags.get<ChannelsRequest>())
        builder.setChannelCount((mDevice->FmtChans==DevFmtMono) ? oboe::ChannelCount::Mono
            : oboe::ChannelCount::Stereo);
    if(mDevice->Flags.get<SampleTypeRequest>())
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
    if(result == oboe::Result::ErrorInvalidFormat)
    {
        builder.setSampleRate(oboe::kUnspecified);
        builder.setChannelCount(oboe::ChannelCount::Stereo);
        builder.setFormat(oboe::AudioFormat::Unspecified);
        result = builder.openManagedStream(mStream);
    }
    if(result != oboe::Result::OK)
        throw al::backend_exception{ALC_INVALID_DEVICE, "Failed to create stream: %s",
            oboe::convertToText(result)};

    switch(mStream->getChannelCount())
    {
    case oboe::ChannelCount::Mono:
        mDevice->FmtChans = DevFmtMono;
        break;
    case oboe::ChannelCount::Stereo:
        mDevice->FmtChans = DevFmtStereo;
        break;
    /* Other potential configurations. Assume WFX channel order. */
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
        throw al::backend_exception{ALC_INVALID_DEVICE, "Got unhandled channel count: %d",
            mStream->getChannelCount()};
    }
    SetDefaultWFXChannelOrder(mDevice);

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
        throw al::backend_exception{ALC_INVALID_DEVICE, "Got unhandled sample type: %d",
            mStream->getFormat()};
    }
    mDevice->Frequency = static_cast<uint32_t>(mStream->getSampleRate());

    /* Ensure the period size is no less than 10ms. It's possible for FramesPerBurst to be 0
     * indicating variable updates, but OpenAL should have a reasonable minimum update size set.
     */
    mDevice->UpdateSize = maxu(mDevice->Frequency / 100,
        static_cast<uint32_t>(mStream->getFramesPerBurst()));
    mDevice->BufferSize = maxu(mDevice->UpdateSize * 2,
        static_cast<uint32_t>(mStream->getBufferSizeInFrames()));

    return true;
}

bool OboePlayback::start()
{
    oboe::Result result{mStream->start()};
    if(result != oboe::Result::OK)
        throw al::backend_exception{ALC_INVALID_DEVICE, "Failed to start stream: %s",
            oboe::convertToText(result)};
    return true;
}

void OboePlayback::stop()
{
    oboe::Result result{mStream->stop()};
    if(result != oboe::Result::OK)
        throw al::backend_exception{ALC_INVALID_DEVICE, "Failed to stop stream: %s",
            oboe::convertToText(result)};
}

} // namespace

bool OboeBackendFactory::init() { return true; }

bool OboeBackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback; }

std::string OboeBackendFactory::probe(BackendType type)
{
    switch(type)
    {
    case BackendType::Playback:
        /* Includes null char. */
        return std::string{device_name, sizeof(device_name)};
    case BackendType::Capture:
        break;
    }
    return std::string{};
}

BackendPtr OboeBackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new OboePlayback{device}};
    return nullptr;
}

BackendFactory &OboeBackendFactory::getFactory()
{
    static OboeBackendFactory factory{};
    return factory;
}
