#ifndef BACKENDS_WASAPI_H
#define BACKENDS_WASAPI_H

#include "backends/base.h"

struct WasapiBackendFactory final : public BackendFactory {
public:
    bool init() override;
    void deinit() override;

    bool querySupport(BackendType type) override;

    void probe(DevProbe type, std::string *outnames) override;

    BackendBase *createBackend(ALCdevice *device, BackendType type) override;

    static BackendFactory &getFactory();
};

#endif /* BACKENDS_WASAPI_H */
