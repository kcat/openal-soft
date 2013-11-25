
#include "config.h"

#include <stdlib.h>

#include "alMain.h"

#include "backends/base.h"


/* Base ALCbackend method implementations. */
void ALCbackend_Construct(ALCbackend *self, ALCdevice *device)
{
    self->mDevice = device;
    InitializeCriticalSection(&self->mMutex);
}

void ALCbackend_Destruct(ALCbackend *self)
{
    DeleteCriticalSection(&self->mMutex);
}

ALCboolean ALCbackend_reset(ALCbackend* UNUSED(self))
{
    return ALC_FALSE;
}

ALCenum ALCbackend_captureSamples(ALCbackend* UNUSED(self), void* UNUSED(buffer), ALCuint UNUSED(samples))
{
    return ALC_INVALID_DEVICE;
}

ALCuint ALCbackend_availableSamples(ALCbackend* UNUSED(self))
{
    return 0;
}

ALint64 ALCbackend_getLatency(ALCbackend* UNUSED(self))
{
    return 0;
}

void ALCbackend_lock(ALCbackend *self)
{
    EnterCriticalSection(&self->mMutex);
}

void ALCbackend_unlock(ALCbackend *self)
{
    LeaveCriticalSection(&self->mMutex);
}


/* Base ALCbackendFactory method implementations. */
void ALCbackendFactory_deinit(ALCbackendFactory* UNUSED(self))
{
}


/* Wrappers to use an old-style backend with the new interface. */
typedef struct PlaybackWrapper {
    DERIVE_FROM_TYPE(ALCbackend);
} PlaybackWrapper;

static void PlaybackWrapper_Construct(PlaybackWrapper *self, ALCdevice *device);
static DECLARE_FORWARD(PlaybackWrapper, ALCbackend, void, Destruct)
static ALCenum PlaybackWrapper_open(PlaybackWrapper *self, const ALCchar *name);
static void PlaybackWrapper_close(PlaybackWrapper *self);
static ALCboolean PlaybackWrapper_reset(PlaybackWrapper *self);
static ALCboolean PlaybackWrapper_start(PlaybackWrapper *self);
static void PlaybackWrapper_stop(PlaybackWrapper *self);
static DECLARE_FORWARD2(PlaybackWrapper, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
static DECLARE_FORWARD(PlaybackWrapper, ALCbackend, ALCuint, availableSamples)
static ALint64 PlaybackWrapper_getLatency(PlaybackWrapper *self);
static DECLARE_FORWARD(PlaybackWrapper, ALCbackend, void, lock)
static DECLARE_FORWARD(PlaybackWrapper, ALCbackend, void, unlock)
static void PlaybackWrapper_Delete(PlaybackWrapper *self);
DEFINE_ALCBACKEND_VTABLE(PlaybackWrapper);

static void PlaybackWrapper_Construct(PlaybackWrapper *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(PlaybackWrapper, ALCbackend, self);
}

static ALCenum PlaybackWrapper_open(PlaybackWrapper *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->OpenPlayback(device, name);
}

static void PlaybackWrapper_close(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    device->Funcs->ClosePlayback(device);
}

static ALCboolean PlaybackWrapper_reset(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->ResetPlayback(device);
}

static ALCboolean PlaybackWrapper_start(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->StartPlayback(device);
}

static void PlaybackWrapper_stop(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    device->Funcs->StopPlayback(device);
}

static ALint64 PlaybackWrapper_getLatency(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->GetLatency(device);
}

static void PlaybackWrapper_Delete(PlaybackWrapper *self)
{
    free(self);
}


typedef struct CaptureWrapper {
    DERIVE_FROM_TYPE(ALCbackend);
} CaptureWrapper;

static void CaptureWrapper_Construct(CaptureWrapper *self, ALCdevice *device);
static DECLARE_FORWARD(CaptureWrapper, ALCbackend, void, Destruct)
static ALCenum CaptureWrapper_open(CaptureWrapper *self, const ALCchar *name);
static void CaptureWrapper_close(CaptureWrapper *self);
static DECLARE_FORWARD(CaptureWrapper, ALCbackend, ALCboolean, reset)
static ALCboolean CaptureWrapper_start(CaptureWrapper *self);
static void CaptureWrapper_stop(CaptureWrapper *self);
static ALCenum CaptureWrapper_captureSamples(CaptureWrapper *self, void *buffer, ALCuint samples);
static ALCuint CaptureWrapper_availableSamples(CaptureWrapper *self);
static ALint64 CaptureWrapper_getLatency(CaptureWrapper *self);
static DECLARE_FORWARD(CaptureWrapper, ALCbackend, void, lock)
static DECLARE_FORWARD(CaptureWrapper, ALCbackend, void, unlock)
static void CaptureWrapper_Delete(CaptureWrapper *self);
DEFINE_ALCBACKEND_VTABLE(CaptureWrapper);


static void CaptureWrapper_Construct(CaptureWrapper *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(CaptureWrapper, ALCbackend, self);
}

static ALCenum CaptureWrapper_open(CaptureWrapper *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->OpenCapture(device, name);
}

static void CaptureWrapper_close(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    device->Funcs->CloseCapture(device);
}

static ALCboolean CaptureWrapper_start(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    device->Funcs->StartCapture(device);
    return ALC_TRUE;
}

static void CaptureWrapper_stop(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    device->Funcs->StopCapture(device);
}

static ALCenum CaptureWrapper_captureSamples(CaptureWrapper *self, void *buffer, ALCuint samples)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->CaptureSamples(device, buffer, samples);
}

static ALCuint CaptureWrapper_availableSamples(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->AvailableSamples(device);
}

static ALint64 CaptureWrapper_getLatency(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->GetLatency(device);
}

static void CaptureWrapper_Delete(CaptureWrapper *self)
{
    free(self);
}


ALCbackend *create_backend_wrapper(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        PlaybackWrapper *backend;

        backend = malloc(sizeof(*backend));
        if(!backend) return NULL;

        PlaybackWrapper_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }

    if(type == ALCbackend_Capture)
    {
        CaptureWrapper *backend;

        backend = malloc(sizeof(*backend));
        if(!backend) return NULL;

        CaptureWrapper_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }

    return NULL;
}
