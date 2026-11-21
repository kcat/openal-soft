#ifndef ALC_BACKENDS_BASE_HPP
#define ALC_BACKENDS_BASE_HPP

#include "config_backends.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alc/events.h"

struct DeviceBase;


struct ClockLatency {
    std::chrono::nanoseconds ClockTime;
    std::chrono::nanoseconds Latency;
};

struct BackendBase {
    virtual void open(std::string_view name) = 0;

    virtual auto reset() -> bool;
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void captureSamples(std::span<std::byte> outbuffer);
    virtual auto availableSamples() -> std::size_t;

    virtual auto getClockLatency() -> ClockLatency;

    DeviceBase &mDevice;
    std::string mDeviceName;

    BackendBase() = delete;
    BackendBase(const BackendBase&) = delete;
    BackendBase(BackendBase&&) = delete;
    explicit BackendBase(DeviceBase &device) noexcept : mDevice{device} { }
    virtual ~BackendBase() = default;

    void operator=(const BackendBase&) = delete;
    void operator=(BackendBase&&) = delete;

protected:
    /** Sets the default channel order used by most non-WaveFormatEx-based APIs. */
    void setDefaultChannelOrder() const;
    /** Sets the default channel order used by WaveFormatEx. */
    void setDefaultWFXChannelOrder() const;
};
using BackendPtr = std::unique_ptr<BackendBase>;

enum class BackendType {
    Playback,
    Capture
};


struct BackendFactory {
    BackendFactory() = default;
    BackendFactory(const BackendFactory&) = delete;
    BackendFactory(BackendFactory&&) = delete;
    virtual ~BackendFactory() = default;

    auto operator=(const BackendFactory&) -> BackendFactory& = delete;
    auto operator=(BackendFactory&&) -> BackendFactory& = delete;

    virtual auto init() -> bool = 0;

    virtual auto querySupport(BackendType type) -> bool = 0;

    virtual auto queryEventSupport(alc::EventType, BackendType) -> alc::EventSupport
    { return alc::EventSupport::NoSupport; }

    virtual auto enumerate(BackendType type) -> std::vector<std::string> = 0;

    virtual auto createBackend(DeviceBase &device, BackendType type) -> BackendPtr = 0;
};

#define DECL_BACKEND(Name) struct Name final : BackendFactory {               \
    auto init() -> bool override;                                             \
    auto querySupport(BackendType type) -> bool override;                     \
    auto queryEventSupport(alc::EventType eventType, BackendType type)        \
        -> alc::EventSupport override;                                        \
    auto enumerate(BackendType type) -> std::vector<std::string> override;    \
    auto createBackend(DeviceBase &device, BackendType type) -> BackendPtr    \
        override;                                                             \
    static auto getFactory() -> BackendFactory&;                              \
};
DECL_BACKEND(NullBackendFactory)
DECL_BACKEND(LoopbackBackendFactory)
#if HAVE_PIPEWIRE
DECL_BACKEND(PipeWireBackendFactory)
#endif
#if HAVE_JACK
DECL_BACKEND(JackBackendFactory)
#endif
#if HAVE_PULSEAUDIO
DECL_BACKEND(PulseBackendFactory)
#endif
#if HAVE_ALSA
DECL_BACKEND(AlsaBackendFactory)
#endif
#if HAVE_WASAPI
DECL_BACKEND(WasapiBackendFactory)
#endif
#if HAVE_COREAUDIO
DECL_BACKEND(CoreAudioBackendFactory)
#endif
#if HAVE_OPENSL
DECL_BACKEND(OSLBackendFactory)
#endif
#if HAVE_OBOE
DECL_BACKEND(OboeBackendFactory)
#endif
#if HAVE_SOLARIS
DECL_BACKEND(SolarisBackendFactory)
#endif
#if HAVE_SNDIO
DECL_BACKEND(SndIOBackendFactory)
#endif
#if HAVE_OSS
DECL_BACKEND(OSSBackendFactory)
#endif
#if HAVE_DSOUND
DECL_BACKEND(DSoundBackendFactory)
#endif
#if HAVE_WINMM
DECL_BACKEND(WinMMBackendFactory)
#endif
#if HAVE_PORTAUDIO
DECL_BACKEND(PortBackendFactory)
#endif
#if HAVE_SDL3
DECL_BACKEND(SDL3BackendFactory)
#endif
#if HAVE_SDL2
DECL_BACKEND(SDL2BackendFactory)
#endif
#if HAVE_WAVE
DECL_BACKEND(WaveBackendFactory)
#endif
#undef DECL_BACKEND

#endif /* ALC_BACKENDS_BASE_HPP */
