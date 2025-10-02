#ifndef ALC_BACKENDS_BASE_H
#define ALC_BACKENDS_BASE_H

#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alc/events.h"
#include "core/device.h"
#include "core/except.h"
#include "gsl/gsl"
#include "opthelpers.h"

using uint = unsigned int;


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
    virtual auto availableSamples() -> uint;

    virtual auto getClockLatency() -> ClockLatency;

    gsl::not_null<DeviceBase*> const mDevice;
    std::string mDeviceName;

    BackendBase() = delete;
    BackendBase(const BackendBase&) = delete;
    BackendBase(BackendBase&&) = delete;
    explicit BackendBase(gsl::not_null<DeviceBase*> device) noexcept : mDevice{device} { }
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


/* Helper to get the device latency from the backend, including any fixed
 * latency from post-processing.
 */
inline ClockLatency GetClockLatency(DeviceBase *device, BackendBase *backend)
{
    ClockLatency ret{backend->getClockLatency()};
    ret.Latency += device->FixedLatency;
    return ret;
}


struct BackendFactory {
    BackendFactory() = default;
    BackendFactory(const BackendFactory&) = delete;
    BackendFactory(BackendFactory&&) = delete;
    virtual ~BackendFactory() = default;

    void operator=(const BackendFactory&) = delete;
    void operator=(BackendFactory&&) = delete;

    virtual auto init() -> bool = 0;

    virtual auto querySupport(BackendType type) -> bool = 0;

    virtual auto queryEventSupport(alc::EventType, BackendType) -> alc::EventSupport
    { return alc::EventSupport::NoSupport; }

    virtual auto enumerate(BackendType type) -> std::vector<std::string> = 0;

    virtual auto createBackend(gsl::not_null<DeviceBase*> device, BackendType type) -> BackendPtr
        = 0;
};

namespace al {

enum class backend_error {
    NoDevice,
    DeviceError,
    OutOfMemory
};

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
class backend_exception final : public base_exception {
    backend_error mErrorCode;

    static auto make_string(std::string_view fmt, std::format_args args) -> std::string;

public:
    template<typename ...Args>
    backend_exception(backend_error code, std::format_string<Args...> fmt, Args&& ...args)
        : base_exception{make_string(fmt.get(), std::make_format_args(args...))}, mErrorCode{code}
    { }
    backend_exception(const backend_exception&) = default;
    backend_exception(backend_exception&&) = default;
    NOINLINE ~backend_exception() override = default;

    backend_exception& operator=(const backend_exception&) = default;
    backend_exception& operator=(backend_exception&&) = default;

    [[nodiscard]] auto errorCode() const noexcept -> backend_error { return mErrorCode; }
};

} // namespace al

#endif /* ALC_BACKENDS_BASE_H */
