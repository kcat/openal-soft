
#include "config.h"

#include <stddef.h>

#include "AL/alc.h"
#include "router.h"


ALC_API ALCdevice* ALC_APIENTRY alcOpenDevice(const ALCchar *devicename)
{
    return NULL;
}

ALC_API ALCboolean ALC_APIENTRY alcCloseDevice(ALCdevice *device)
{
    return ALC_FALSE;
}


ALC_API ALCcontext* ALC_APIENTRY alcCreateContext(ALCdevice *device, const ALCint* attrlist)
{
    return NULL;
}

ALC_API ALCboolean ALC_APIENTRY alcMakeContextCurrent(ALCcontext *context)
{
    return ALC_FALSE;
}

ALC_API void ALC_APIENTRY alcProcessContext(ALCcontext *context)
{
}

ALC_API void ALC_APIENTRY alcSuspendContext(ALCcontext *context)
{
}

ALC_API void ALC_APIENTRY alcDestroyContext(ALCcontext *context)
{
}

ALC_API ALCcontext* ALC_APIENTRY alcGetCurrentContext(void)
{
    return NULL;
}

ALC_API ALCdevice* ALC_APIENTRY alcGetContextsDevice(ALCcontext *context)
{
    return NULL;
}


ALC_API ALCenum ALC_APIENTRY alcGetError(ALCdevice *device)
{
    return ALC_NO_ERROR;
}

ALC_API ALCboolean ALC_APIENTRY alcIsExtensionPresent(ALCdevice *device, const ALCchar *extname)
{
    return ALC_FALSE;
}

ALC_API void* ALC_APIENTRY alcGetProcAddress(ALCdevice *device, const ALCchar *funcname)
{
    return NULL;
}

ALC_API ALCenum ALC_APIENTRY alcGetEnumValue(ALCdevice *device, const ALCchar *enumname)
{
    return 0;
}

ALC_API const ALCchar* ALC_APIENTRY alcGetString(ALCdevice *device, ALCenum param)
{
    return NULL;
}

ALC_API void ALC_APIENTRY alcGetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values)
{
}


ALC_API ALCdevice* ALC_APIENTRY alcCaptureOpenDevice(const ALCchar *devicename, ALCuint frequency, ALCenum format, ALCsizei buffersize)
{
    return NULL;
}

ALC_API ALCboolean ALC_APIENTRY alcCaptureCloseDevice(ALCdevice *device)
{
    return ALC_FALSE;
}

ALC_API void ALC_APIENTRY alcCaptureStart(ALCdevice *device)
{
}

ALC_API void ALC_APIENTRY alcCaptureStop(ALCdevice *device)
{
}

ALC_API void ALC_APIENTRY alcCaptureSamples(ALCdevice *device, ALCvoid *buffer, ALCsizei samples)
{
}
