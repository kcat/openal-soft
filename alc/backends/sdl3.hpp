#ifndef BACKENDS_SDL3_HPP
#define BACKENDS_SDL3_HPP

#include "base.hpp"

struct SDL3BackendFactory final : BackendFactory {
    auto init() -> bool final;

    auto querySupport(BackendType type) -> bool final;

    auto queryEventSupport(alc::EventType event, BackendType backend) -> alc::EventSupport final;

    auto enumerate(BackendType type) -> std::vector<std::string> final;

    auto createBackend(DeviceBase &device, BackendType type) -> BackendPtr final;

    static auto getFactory() -> BackendFactory&;
};

#endif /* BACKENDS_SDL3_HPP */
