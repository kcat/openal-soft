
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


/* Wrapper to use an old-style backend with the new interface. */
typedef struct BackendWrapper {
    DERIVE_FROM_TYPE(ALCbackend);
} BackendWrapper;
#define BACKENDWRAPPER_INITIALIZER { { GET_VTABLE2(ALCbackend, BackendWrapper) } }

static void BackendWrapper_Construct(BackendWrapper *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
}

static void BackendWrapper_Destruct(BackendWrapper *self)
{
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}

static ALCenum BackendWrapper_open(BackendWrapper *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->OpenPlayback(device, name);
}

static void BackendWrapper_close(BackendWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    device->Funcs->ClosePlayback(device);
}

static ALCboolean BackendWrapper_reset(BackendWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->ResetPlayback(device);
}

static ALCboolean BackendWrapper_start(BackendWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->StartPlayback(device);
}

static void BackendWrapper_stop(BackendWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    device->Funcs->StopPlayback(device);
}

static ALint64 BackendWrapper_getLatency(BackendWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->GetLatency(device);
}

static void BackendWrapper_lock(BackendWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->Lock(device);
}

static void BackendWrapper_unlock(BackendWrapper *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return device->Funcs->Unlock(device);
}

static void BackendWrapper_Delete(BackendWrapper *self)
{
    free(self);
}

DEFINE_ALCBACKEND_VTABLE(BackendWrapper);


ALCbackend *create_backend_wrapper(ALCdevice *device)
{
    BackendWrapper *backend;

    backend = malloc(sizeof(*backend));
    if(!backend) return NULL;
    SET_VTABLE2(BackendWrapper, ALCbackend, backend);

    BackendWrapper_Construct(backend, device);

    return STATIC_CAST(ALCbackend, backend);
}
