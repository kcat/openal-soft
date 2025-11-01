#ifndef BACKENDS_SDL3_H
#define BACKENDS_SDL3_H

#include "base.h"

struct SDL3BackendFactory final : BackendFactory {
    auto init() -> bool final;

    auto querySupport(BackendType type) -> bool final;

    auto enumerate(BackendType type) -> std::vector<std::string> final;

    auto createBackend(gsl::not_null<DeviceBase*> device, BackendType type) -> BackendPtr final;

    static auto getFactory() -> BackendFactory&;
};

#endif /* BACKENDS_SDL3_H */
