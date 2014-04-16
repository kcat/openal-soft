
#include "config.h"

#include <stdlib.h>

#include "alMain.h"

#include "backends/base.h"


/* Base ALCbackend method implementations. */
void ALCbackend_Construct(ALCbackend *self, ALCdevice *device)
{
    int ret;
    self->mDevice = device;
    ret = almtx_init(&self->mMutex, almtx_recursive);
    assert(ret == althrd_success);
}

void ALCbackend_Destruct(ALCbackend *self)
{
    almtx_destroy(&self->mMutex);
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
    int ret = almtx_lock(&self->mMutex);
    assert(ret == althrd_success);
}

void ALCbackend_unlock(ALCbackend *self)
{
    int ret = almtx_unlock(&self->mMutex);
    assert(ret == althrd_success);
}


/* Base ALCbackendFactory method implementations. */
void ALCbackendFactory_deinit(ALCbackendFactory* UNUSED(self))
{
}


/* Wrappers to use an old-style backend with the new interface. */
typedef struct PlaybackWrapper {
    DERIVE_FROM_TYPE(ALCbackend);

    const BackendFuncs *Funcs;
} PlaybackWrapper;

static void PlaybackWrapper_Construct(PlaybackWrapper *self, ALCdevice *device, const BackendFuncs *funcs);
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
DECLARE_DEFAULT_ALLOCATORS(PlaybackWrapper)
DEFINE_ALCBACKEND_VTABLE(PlaybackWrapper);

static void PlaybackWrapper_Construct(PlaybackWrapper *self, ALCdevice *device, const BackendFuncs *funcs)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(PlaybackWrapper, ALCbackend, self);

    self->Funcs = funcs;
}

static ALCenum PlaybackWrapper_open(PlaybackWrapper *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return self->Funcs->OpenPlayback(device, name);
}

static void PlaybackWrapper_close(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    self->Funcs->ClosePlayback(device);
}

static ALCboolean PlaybackWrapper_reset(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return self->Funcs->ResetPlayback(device);
}

static ALCboolean PlaybackWrapper_start(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return self->Funcs->StartPlayback(device);
}

static void PlaybackWrapper_stop(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    self->Funcs->StopPlayback(device);
}

static ALint64 PlaybackWrapper_getLatency(PlaybackWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return self->Funcs->GetLatency(device);
}


typedef struct CaptureWrapper {
    DERIVE_FROM_TYPE(ALCbackend);

    const BackendFuncs *Funcs;
} CaptureWrapper;

static void CaptureWrapper_Construct(CaptureWrapper *self, ALCdevice *device, const BackendFuncs *funcs);
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
DECLARE_DEFAULT_ALLOCATORS(CaptureWrapper)
DEFINE_ALCBACKEND_VTABLE(CaptureWrapper);


static void CaptureWrapper_Construct(CaptureWrapper *self, ALCdevice *device, const BackendFuncs *funcs)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(CaptureWrapper, ALCbackend, self);

    self->Funcs = funcs;
}

static ALCenum CaptureWrapper_open(CaptureWrapper *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return self->Funcs->OpenCapture(device, name);
}

static void CaptureWrapper_close(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    self->Funcs->CloseCapture(device);
}

static ALCboolean CaptureWrapper_start(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    self->Funcs->StartCapture(device);
    return ALC_TRUE;
}

static void CaptureWrapper_stop(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    self->Funcs->StopCapture(device);
}

static ALCenum CaptureWrapper_captureSamples(CaptureWrapper *self, void *buffer, ALCuint samples)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return self->Funcs->CaptureSamples(device, buffer, samples);
}

static ALCuint CaptureWrapper_availableSamples(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return self->Funcs->AvailableSamples(device);
}

static ALint64 CaptureWrapper_getLatency(CaptureWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return self->Funcs->GetLatency(device);
}


ALCbackend *create_backend_wrapper(ALCdevice *device, const BackendFuncs *funcs, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        PlaybackWrapper *backend;

        backend = PlaybackWrapper_New(sizeof(*backend));
        if(!backend) return NULL;

        PlaybackWrapper_Construct(backend, device, funcs);

        return STATIC_CAST(ALCbackend, backend);
    }

    if(type == ALCbackend_Capture)
    {
        CaptureWrapper *backend;

        backend = CaptureWrapper_New(sizeof(*backend));
        if(!backend) return NULL;

        CaptureWrapper_Construct(backend, device, funcs);

        return STATIC_CAST(ALCbackend, backend);
    }

    return NULL;
}
