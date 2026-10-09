#ifndef BACKENDS_WINMM_HPP
#define BACKENDS_WINMM_HPP

#include "base.hpp"

struct WinMMBackendFactory final : BackendFactory {
    auto init() -> bool final;

    auto querySupport(BackendType type) -> bool final;

    auto enumerate(BackendType type) -> std::vector<std::string> final;

    auto createBackend(DeviceBase &device, BackendType type) -> BackendPtr final;

    static auto getFactory() -> BackendFactory&;
};

#endif /* BACKENDS_WINMM_HPP */
