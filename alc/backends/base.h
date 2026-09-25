#ifndef ALC_BACKENDS_BASE_H
#define ALC_BACKENDS_BASE_H

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alc/events.h"
#include "gsl/gsl"

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

    gsl::not_null<DeviceBase*> const mDevice;
    std::string mDeviceName;

    BackendBase() = delete;
    BackendBase(const BackendBase&) = delete;
    BackendBase(BackendBase&&) = delete;
    explicit BackendBase(gsl::not_null<DeviceBase*> const device) noexcept : mDevice{device} { }
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

    virtual auto createBackend(gsl::not_null<DeviceBase*> device, BackendType type) -> BackendPtr
        = 0;
};

#endif /* ALC_BACKENDS_BASE_H */
