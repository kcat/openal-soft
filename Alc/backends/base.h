#ifndef AL_BACKENDS_BASE_H
#define AL_BACKENDS_BASE_H

#include "alMain.h"


struct ALCbackendVtable;

typedef struct ALCbackend {
    const struct ALCbackendVtable *vtbl;

    ALCdevice *mDevice;
} ALCbackend;

ALint64 ALCbackend_getLatency(ALCbackend *self);
void ALCbackend_lock(ALCbackend *self);
void ALCbackend_unlock(ALCbackend *self);

struct ALCbackendVtable {
    void (*const Destruct)(ALCbackend *state);

    ALCenum (*const open)(ALCbackend*, const ALCchar*);
    void (*const close)(ALCbackend*);

    ALCboolean (*reset)(ALCbackend*);
    ALCboolean (*start)(ALCbackend*);
    void (*stop)(ALCbackend*);

    ALint64 (*getLatency)(ALCbackend*);

    void (*lock)(ALCbackend*);
    void (*unlock)(ALCbackend*);

    void (*const Delete)(ALCbackend *state);
};

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
static ALint64 T##_ALCbackend_getLatency(ALCbackend *obj)                     \
{ return T##_getLatency(STATIC_UPCAST(T, ALCbackend, obj)); }                 \
static void T##_ALCbackend_lock(ALCbackend *obj)                              \
{ T##_lock(STATIC_UPCAST(T, ALCbackend, obj)); }                              \
static void T##_ALCbackend_unlock(ALCbackend *obj)                            \
{ T##_unlock(STATIC_UPCAST(T, ALCbackend, obj)); }                            \
static void T##_ALCbackend_Delete(ALCbackend *obj)                            \
{ T##_Delete(STATIC_UPCAST(T, ALCbackend, obj)); }                            \
                                                                              \
static const struct ALCbackendVtable T##_ALCbackend_vtable = {                \
    T##_ALCbackend_Destruct,                                                  \
                                                                              \
    T##_ALCbackend_open,                                                      \
    T##_ALCbackend_close,                                                     \
    T##_ALCbackend_reset,                                                     \
    T##_ALCbackend_start,                                                     \
    T##_ALCbackend_stop,                                                      \
    T##_ALCbackend_getLatency,                                                \
    T##_ALCbackend_lock,                                                      \
    T##_ALCbackend_unlock,                                                    \
                                                                              \
    T##_ALCbackend_Delete,                                                    \
}


typedef enum ALCbackend_Type {
    ALCbackend_Playback
} ALCbackend_Type;


struct ALCbackendFactoryVtable;

typedef struct ALCbackendFactory {
    const struct ALCbackendFactoryVtable *vtbl;
} ALCbackendFactory;

struct ALCbackendFactoryVtable {
    ALCboolean (*const init)(ALCbackendFactory *self);
    void (*const deinit)(ALCbackendFactory *self);

    ALCboolean (*const support)(ALCbackendFactory *self, ALCbackend_Type type);

    void (*const probe)(ALCbackendFactory *self, enum DevProbe type);

    ALCbackend* (*const createBackend)(ALCbackendFactory *self, ALCdevice *device);
};

#define DEFINE_ALCBACKENDFACTORY_VTABLE(T)                                    \
static ALCboolean T##_ALCbackendFactory_init(ALCbackendFactory *obj)          \
{ return T##_init(STATIC_UPCAST(T, ALCbackendFactory, obj)); }                \
static void T##_ALCbackendFactory_deinit(ALCbackendFactory *obj)              \
{ T##_deinit(STATIC_UPCAST(T, ALCbackendFactory, obj)); }                     \
static ALCboolean T##_ALCbackendFactory_support(ALCbackendFactory *obj, ALCbackend_Type a) \
{ return T##_support(STATIC_UPCAST(T, ALCbackendFactory, obj), a); }                       \
static void T##_ALCbackendFactory_probe(ALCbackendFactory *obj, enum DevProbe a) \
{ T##_probe(STATIC_UPCAST(T, ALCbackendFactory, obj), a); }                      \
static ALCbackend* T##_ALCbackendFactory_createBackend(ALCbackendFactory *obj, ALCdevice *a) \
{ return T##_createBackend(STATIC_UPCAST(T, ALCbackendFactory, obj), a); }                   \
                                                                              \
static const struct ALCbackendFactoryVtable T##_ALCbackendFactory_vtable = {  \
    T##_ALCbackendFactory_init,                                               \
    T##_ALCbackendFactory_deinit,                                             \
    T##_ALCbackendFactory_support,                                            \
    T##_ALCbackendFactory_probe,                                              \
    T##_ALCbackendFactory_createBackend,                                      \
}


#endif /* AL_BACKENDS_BASE_H */
