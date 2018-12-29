#ifndef BACKENDS_SOLARIS_H
#define BACKENDS_SOLARIS_H

#include "backends/base.h"

struct SolarisBackendFactory final : public BackendFactory {
public:
    bool init() override;
    /*void deinit() override;*/

    bool querySupport(BackendType type) override;

    void probe(DevProbe type, std::string *outnames) override;

    BackendBase *createBackend(ALCdevice *device, BackendType type) override;

    static BackendFactory &getFactory();
};

#endif /* BACKENDS_SOLARIS_H */
