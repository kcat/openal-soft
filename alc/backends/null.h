#ifndef BACKENDS_NULL_H
#define BACKENDS_NULL_H

#include "base.h"

struct NullBackendFactory final : public BackendFactory {
public:
    auto init() -> bool final;

    auto querySupport(BackendType type) -> bool final;

    auto enumerate(BackendType type) -> std::vector<std::string> final;

    auto createBackend(gsl::not_null<DeviceBase*> device, BackendType type) -> BackendPtr final;

    static auto getFactory() -> BackendFactory&;
};

#endif /* BACKENDS_NULL_H */
