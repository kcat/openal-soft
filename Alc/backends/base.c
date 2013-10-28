
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


/* Wrappers to use an old-style backend with the new interface. */
typedef struct PlaybackWrapper {
    DERIVE_FROM_TYPE(ALCbackend);
} PlaybackWrapper;
#define PLAYBACKWRAPPER_INITIALIZER { { GET_VTABLE2(ALCbackend, PlaybackWrapper) } }

static void PlaybackWrapper_Construct(PlaybackWrapper *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
}

static void PlaybackWrapper_Destruct(PlaybackWrapper *self)
{
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
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

ALCenum PlaybackWrapper_captureSamples(PlaybackWrapper* UNUSED(self), void* UNUSED(buffer), ALCuint UNUSED(samples))
{
    return ALC_INVALID_VALUE;
}

ALCuint PlaybackWrapper_availableSamples(PlaybackWrapper* UNUSED(self))
{
    return 0;
}

static ALint64 PlaybackWrapper_getLatency(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->GetLatency(device);
}

static void PlaybackWrapper_lock(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->Lock(device);
}

static void PlaybackWrapper_unlock(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->Unlock(device);
}

static void PlaybackWrapper_Delete(PlaybackWrapper *self)
{
    free(self);
}

DEFINE_ALCBACKEND_VTABLE(PlaybackWrapper);


typedef struct CaptureWrapper {
    DERIVE_FROM_TYPE(ALCbackend);
} CaptureWrapper;
#define CAPTUREWRAPPER_INITIALIZER { { GET_VTABLE2(ALCbackend, PlaybackWrapper) } }

static void CaptureWrapper_Construct(CaptureWrapper *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
}

static void CaptureWrapper_Destruct(CaptureWrapper *self)
{
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
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

static ALCboolean CaptureWrapper_reset(CaptureWrapper* UNUSED(self))
{
    return ALC_FALSE;
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

ALCenum CaptureWrapper_captureSamples(CaptureWrapper *self, void *buffer, ALCuint samples)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->CaptureSamples(device, buffer, samples);
}

ALCuint CaptureWrapper_availableSamples(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->AvailableSamples(device);
}

static ALint64 CaptureWrapper_getLatency(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->GetLatency(device);
}

static void CaptureWrapper_lock(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->Lock(device);
}

static void CaptureWrapper_unlock(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->Unlock(device);
}

static void CaptureWrapper_Delete(CaptureWrapper *self)
{
    free(self);
}

DEFINE_ALCBACKEND_VTABLE(CaptureWrapper);


ALCbackend *create_backend_wrapper(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        PlaybackWrapper *backend;

        backend = malloc(sizeof(*backend));
        if(!backend) return NULL;
        SET_VTABLE2(PlaybackWrapper, ALCbackend, backend);

        PlaybackWrapper_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }

    if(type == ALCbackend_Capture)
    {
        CaptureWrapper *backend;

        backend = malloc(sizeof(*backend));
        if(!backend) return NULL;
        SET_VTABLE2(CaptureWrapper, ALCbackend, backend);

        CaptureWrapper_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }

    return NULL;
}
