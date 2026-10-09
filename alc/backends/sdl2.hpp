#ifndef BACKENDS_SDL2_HPP
#define BACKENDS_SDL2_HPP

#include "base.hpp"

struct SDL2BackendFactory final : BackendFactory {
    auto init() -> bool final;

    auto querySupport(BackendType type) -> bool final;

    auto enumerate(BackendType type) -> std::vector<std::string> final;

    auto createBackend(DeviceBase &device, BackendType type) -> BackendPtr final;

    static auto getFactory() -> BackendFactory&;
};

#endif /* BACKENDS_SDL2_HPP */
