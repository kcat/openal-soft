#ifndef AL_BACKENDS_BASE_H
#define AL_BACKENDS_BASE_H

#include "alMain.h"
#include "compat.h"


struct ALCbackendVtable;

typedef struct ALCbackend {
    const struct ALCbackendVtable *vtbl;

    ALCdevice *mDevice;

    CRITICAL_SECTION mMutex;
} ALCbackend;

void ALCbackend_Construct(ALCbackend *self, ALCdevice *device);
void ALCbackend_Destruct(ALCbackend *self);
ALint64 ALCbackend_getLatency(ALCbackend *self);
void ALCbackend_lock(ALCbackend *self);
void ALCbackend_unlock(ALCbackend *self);

struct ALCbackendVtable {
    void (*const Destruct)(ALCbackend*);

    ALCenum (*const open)(ALCbackend*, const ALCchar*);
    void (*const close)(ALCbackend*);

    ALCboolean (*reset)(ALCbackend*);
    ALCboolean (*start)(ALCbackend*);
    void (*stop)(ALCbackend*);

    ALCenum (*captureSamples)(ALCbackend*, void*, ALCuint);
    ALCuint (*availableSamples)(ALCbackend*);

    ALint64 (*getLatency)(ALCbackend*);

    void (*lock)(ALCbackend*);
    void (*unlock)(ALCbackend*);

    void (*const Delete)(ALCbackend*);
};

#define DECLARE_ALCBACKEND_VTABLE(T)                                          \
static const struct ALCbackendVtable T##_ALCbackend_vtable

#define DEFINE_ALCBACKEND_VTABLE(T)                                           \
static void T##_ALCbackend_Destruct(ALCbackend *obj)                          \
{ T##_Destruct(STATIC_UPCAST(T, ALCbackend, obj)); }                          \
static ALCenum T##_ALCbackend_open(ALCbackend *obj, const ALCchar *p1)        \
{ return T##_open(STATIC_UPCAST(T, ALCbackend, obj), p1); }                   \
static void T##_ALCbackend_close(ALCbackend *obj)                             \
{ T##_close(STATIC_UPCAST(T, ALCbackend, obj)); }                             \
static ALCboolean T##_ALCbackend_reset(ALCbackend *obj)                       \
{ return T##_reset(STATIC_UPCAST(T, ALCbackend, obj)); }                      \
static ALCboolean T##_ALCbackend_start(ALCbackend *obj)                       \
{ return T##_start(STATIC_UPCAST(T, ALCbackend, obj)); }                      \
static void T##_ALCbackend_stop(ALCbackend *obj)                              \
{ T##_stop(STATIC_UPCAST(T, ALCbackend, obj)); }                              \
static ALCenum T##_ALCbackend_captureSamples(ALCbackend *obj, void *a, ALCuint b) \
{ return T##_captureSamples(STATIC_UPCAST(T, ALCbackend, obj), a, b); }           \
static ALCuint T##_ALCbackend_availableSamples(ALCbackend *obj)               \
{ return T##_availableSamples(STATIC_UPCAST(T, ALCbackend, obj)); }           \
static ALint64 T##_ALCbackend_getLatency(ALCbackend *obj)                     \
{ return T##_getLatency(STATIC_UPCAST(T, ALCbackend, obj)); }                 \
static void T##_ALCbackend_lock(ALCbackend *obj)                              \
{ T##_lock(STATIC_UPCAST(T, ALCbackend, obj)); }                              \
static void T##_ALCbackend_unlock(ALCbackend *obj)                            \
{ T##_unlock(STATIC_UPCAST(T, ALCbackend, obj)); }                            \
static void T##_ALCbackend_Delete(ALCbackend *obj)                            \
{ T##_Delete(STATIC_UPCAST(T, ALCbackend, obj)); }                            \
                                                                              \
DECLARE_ALCBACKEND_VTABLE(T) = {                                              \
    T##_ALCbackend_Destruct,                                                  \
                                                                              \
    T##_ALCbackend_open,                                                      \
    T##_ALCbackend_close,                                                     \
    T##_ALCbackend_reset,                                                     \
    T##_ALCbackend_start,                                                     \
    T##_ALCbackend_stop,                                                      \
    T##_ALCbackend_captureSamples,                                            \
    T##_ALCbackend_availableSamples,                                          \
    T##_ALCbackend_getLatency,                                                \
    T##_ALCbackend_lock,                                                      \
    T##_ALCbackend_unlock,                                                    \
                                                                              \
    T##_ALCbackend_Delete,                                                    \
}


typedef enum ALCbackend_Type {
    ALCbackend_Playback,
    ALCbackend_Capture
} ALCbackend_Type;


struct ALCbackendFactoryVtable;

typedef struct ALCbackendFactory {
    const struct ALCbackendFactoryVtable *vtbl;
} ALCbackendFactory;

struct ALCbackendFactoryVtable {
    ALCboolean (*const init)(ALCbackendFactory *self);
    void (*const deinit)(ALCbackendFactory *self);

    ALCboolean (*const querySupport)(ALCbackendFactory *self, ALCbackend_Type type);

    void (*const probe)(ALCbackendFactory *self, enum DevProbe type);

    ALCbackend* (*const createBackend)(ALCbackendFactory *self, ALCdevice *device, ALCbackend_Type type);
};

#define DEFINE_ALCBACKENDFACTORY_VTABLE(T)                                    \
static ALCboolean T##_ALCbackendFactory_init(ALCbackendFactory *obj)          \
{ return T##_init(STATIC_UPCAST(T, ALCbackendFactory, obj)); }                \
static void T##_ALCbackendFactory_deinit(ALCbackendFactory *obj)              \
{ T##_deinit(STATIC_UPCAST(T, ALCbackendFactory, obj)); }                     \
static ALCboolean T##_ALCbackendFactory_querySupport(ALCbackendFactory *obj, ALCbackend_Type a) \
{ return T##_querySupport(STATIC_UPCAST(T, ALCbackendFactory, obj), a); }                       \
static void T##_ALCbackendFactory_probe(ALCbackendFactory *obj, enum DevProbe a) \
{ T##_probe(STATIC_UPCAST(T, ALCbackendFactory, obj), a); }                      \
static ALCbackend* T##_ALCbackendFactory_createBackend(ALCbackendFactory *obj, ALCdevice *a, ALCbackend_Type b) \
{ return T##_createBackend(STATIC_UPCAST(T, ALCbackendFactory, obj), a, b); }                                   \
                                                                              \
static const struct ALCbackendFactoryVtable T##_ALCbackendFactory_vtable = {  \
    T##_ALCbackendFactory_init,                                               \
    T##_ALCbackendFactory_deinit,                                             \
    T##_ALCbackendFactory_querySupport,                                       \
    T##_ALCbackendFactory_probe,                                              \
    T##_ALCbackendFactory_createBackend,                                      \
}


ALCbackendFactory *ALCalsaBackendFactory_getFactory(void);
ALCbackendFactory *ALCnullBackendFactory_getFactory(void);
ALCbackendFactory *ALCloopbackFactory_getFactory(void);

ALCbackend *create_backend_wrapper(ALCdevice *device, ALCbackend_Type type);

#endif /* AL_BACKENDS_BASE_H */
