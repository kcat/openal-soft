#ifndef BACKENDS_LOOPBACK_HPP
#define BACKENDS_LOOPBACK_HPP

#include "base.hpp"

struct LoopbackBackendFactory final : BackendFactory {
    auto init() -> bool final;

    auto querySupport(BackendType type) -> bool final;

    auto enumerate(BackendType type) -> std::vector<std::string> final;

    auto createBackend(DeviceBase &device, BackendType type) -> BackendPtr final;

    static auto getFactory() -> BackendFactory&;
};

#endif /* BACKENDS_LOOPBACK_HPP */
