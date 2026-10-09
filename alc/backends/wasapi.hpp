#ifndef BACKENDS_WASAPI_HPP
#define BACKENDS_WASAPI_HPP

#include "base.hpp"

struct WasapiBackendFactory final : BackendFactory {
    auto init() -> bool final;

    auto querySupport(BackendType type) -> bool final;

    auto queryEventSupport(alc::EventType eventType, BackendType type) -> alc::EventSupport final;

    auto enumerate(BackendType type) -> std::vector<std::string> final;

    auto createBackend(DeviceBase &device, BackendType type) -> BackendPtr final;

    static auto getFactory() -> BackendFactory&;
};

#endif /* BACKENDS_WASAPI_HPP */
