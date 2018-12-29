#ifndef BACKENDS_DSOUND_H
#define BACKENDS_DSOUND_H

#include "backends/base.h"

struct DSoundBackendFactory final : public BackendFactory {
public:
    bool init() override;
    void deinit() override;

    bool querySupport(ALCbackend_Type type) override;

    void probe(DevProbe type, std::string *outnames) override;

    BackendBase *createBackend(ALCdevice *device, ALCbackend_Type type) override;

    static BackendFactory &getFactory();
};

#endif /* BACKENDS_DSOUND_H */
