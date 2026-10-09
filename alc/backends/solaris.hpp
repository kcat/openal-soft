#ifndef BACKENDS_SOLARIS_HPP
#define BACKENDS_SOLARIS_HPP

#include "base.hpp"

struct SolarisBackendFactory final : BackendFactory {
    auto init() -> bool final;

    auto querySupport(BackendType type) -> bool final;

    auto enumerate(BackendType type) -> std::vector<std::string> final;

    auto createBackend(DeviceBase &device, BackendType type) -> BackendPtr final;

    static auto getFactory() -> BackendFactory&;
};

#endif /* BACKENDS_SOLARIS_HPP */
