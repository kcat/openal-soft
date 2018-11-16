#ifndef AL_BACKENDS_BASE_H
#define AL_BACKENDS_BASE_H

#include "alMain.h"

#include <string>
#include <mutex>

struct ClockLatency {
    ALint64 ClockTime;
    ALint64 Latency;
};

/* Helper to get the current clock time from the device's ClockBase, and
 * SamplesDone converted from the sample rate.
 */
inline ALuint64 GetDeviceClockTime(ALCdevice *device)
{
    return device->ClockBase + (device->SamplesDone * DEVICE_CLOCK_RES /
                                device->Frequency);
}

void ALCdevice_Lock(ALCdevice *device);
void ALCdevice_Unlock(ALCdevice *device);

ClockLatency GetClockLatency(ALCdevice *device);


struct ALCbackendVtable;

struct ALCbackend {
    const struct ALCbackendVtable *vtbl;

    ALCdevice *mDevice;

    std::recursive_mutex mMutex;
};

void ALCbackend_Construct(ALCbackend *self, ALCdevice *device);
void ALCbackend_Destruct(ALCbackend *self);
ALCboolean ALCbackend_reset(ALCbackend *self);
ALCenum ALCbackend_captureSamples(ALCbackend *self, void *buffer, ALCuint samples);
ALCuint ALCbackend_availableSamples(ALCbackend *self);
ClockLatency ALCbackend_getClockLatency(ALCbackend *self);
void ALCbackend_lock(ALCbackend *self);
void ALCbackend_unlock(ALCbackend *self);

struct ALCbackendVtable {
    void (*const Destruct)(ALCbackend*);

    ALCenum (*const open)(ALCbackend*, const ALCchar*);

    ALCboolean (*const reset)(ALCbackend*);
    ALCboolean (*const start)(ALCbackend*);
    void (*const stop)(ALCbackend*);

    ALCenum (*const captureSamples)(ALCbackend*, void*, ALCuint);
    ALCuint (*const availableSamples)(ALCbackend*);

    ClockLatency (*const getClockLatency)(ALCbackend*);

    void (*const lock)(ALCbackend*);
    void (*const unlock)(ALCbackend*);

    void (*const Delete)(void*);
};

#define DEFINE_ALCBACKEND_VTABLE(T)                                           \
DECLARE_THUNK(T, ALCbackend, void, Destruct)                                  \
DECLARE_THUNK1(T, ALCbackend, ALCenum, open, const ALCchar*)                  \
DECLARE_THUNK(T, ALCbackend, ALCboolean, reset)                               \
DECLARE_THUNK(T, ALCbackend, ALCboolean, start)                               \
DECLARE_THUNK(T, ALCbackend, void, stop)                                      \
DECLARE_THUNK2(T, ALCbackend, ALCenum, captureSamples, void*, ALCuint)        \
DECLARE_THUNK(T, ALCbackend, ALCuint, availableSamples)                       \
DECLARE_THUNK(T, ALCbackend, ClockLatency, getClockLatency)                   \
DECLARE_THUNK(T, ALCbackend, void, lock)                                      \
DECLARE_THUNK(T, ALCbackend, void, unlock)                                    \
static void T##_ALCbackend_Delete(void *ptr)                                  \
{ T##_Delete(STATIC_UPCAST(T, ALCbackend, (ALCbackend*)ptr)); }               \
                                                                              \
static const struct ALCbackendVtable T##_ALCbackend_vtable = {                \
    T##_ALCbackend_Destruct,                                                  \
                                                                              \
    T##_ALCbackend_open,                                                      \
    T##_ALCbackend_reset,                                                     \
    T##_ALCbackend_start,                                                     \
    T##_ALCbackend_stop,                                                      \
    T##_ALCbackend_captureSamples,                                            \
    T##_ALCbackend_availableSamples,                                          \
    T##_ALCbackend_getClockLatency,                                           \
    T##_ALCbackend_lock,                                                      \
    T##_ALCbackend_unlock,                                                    \
                                                                              \
    T##_ALCbackend_Delete,                                                    \
}


enum ALCbackend_Type {
    ALCbackend_Playback,
    ALCbackend_Capture,
    ALCbackend_Loopback
};


struct BackendFactory {
    virtual bool init() = 0;
    virtual void deinit() { }

    virtual bool querySupport(ALCbackend_Type type) = 0;

    virtual void probe(enum DevProbe type, std::string *outnames) = 0;

    virtual ALCbackend *createBackend(ALCdevice *device, ALCbackend_Type type) = 0;
};

#endif /* AL_BACKENDS_BASE_H */
