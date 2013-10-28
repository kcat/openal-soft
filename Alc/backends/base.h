#ifndef AL_BACKENDS_BASE_H
#define AL_BACKENDS_BASE_H

#include "alMain.h"


struct ALCbackendVtable;

typedef struct ALCbackend {
    const struct ALCbackendVtable *vtbl;

    ALCdevice *mDevice;
} ALCbackend;

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


#endif /* AL_BACKENDS_BASE_H */
