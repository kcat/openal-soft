#ifndef BACKENDS_SDL2_H
#define BACKENDS_SDL2_H

#include "backends/base.h"

struct SDL2BackendFactory final : public BackendFactory {
public:
    bool init() override;
    void deinit() override;

    bool querySupport(ALCbackend_Type type) override;

    void probe(enum DevProbe type, std::string *outnames) override;

    ALCbackend *createBackend(ALCdevice *device, ALCbackend_Type type) override;

    static BackendFactory &getFactory();
};

#endif /* BACKENDS_SDL2_H */
