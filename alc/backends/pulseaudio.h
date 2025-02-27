#ifndef BACKENDS_PULSEAUDIO_H
#define BACKENDS_PULSEAUDIO_H

#include <string>
#include <vector>

#include "alc/events.h"
#include "base.h"

struct DeviceBase;

class PulseBackendFactory final : public BackendFactory {
public:
    auto init() -> bool final;

    auto querySupport(BackendType type) -> bool final;

    auto queryEventSupport(alc::EventType eventType, BackendType type) -> alc::EventSupport final;

    auto enumerate(BackendType type) -> std::vector<std::string> final;

    auto createBackend(DeviceBase *device, BackendType type) -> BackendPtr final;

    static auto getFactory() -> BackendFactory&;
};

#endif /* BACKENDS_PULSEAUDIO_H */
