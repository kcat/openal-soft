/*
 * OpenAL Helpers
 *
 * Copyright (c) 2011 by Chris Robinson <chris.kcat@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* This file contains routines to help with some menial OpenAL-related tasks,
 * such as opening a device and setting up a context, closing the device and
 * destroying its context, converting between frame counts and byte lengths,
 * finding an appropriate buffer format, and getting readable strings for
 * channel configs and sample types. */

#include "alhelpers.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"


/* ALC_EXT_EFX */
LPALGENFILTERS alGenFilters;
LPALDELETEFILTERS alDeleteFilters;
LPALISFILTER alIsFilter;
LPALFILTERI alFilteri;
LPALFILTERIV alFilteriv;
LPALFILTERF alFilterf;
LPALFILTERFV alFilterfv;
LPALGETFILTERI alGetFilteri;
LPALGETFILTERIV alGetFilteriv;
LPALGETFILTERF alGetFilterf;
LPALGETFILTERFV alGetFilterfv;
LPALGENEFFECTS alGenEffects;
LPALDELETEEFFECTS alDeleteEffects;
LPALISEFFECT alIsEffect;
LPALEFFECTI alEffecti;
LPALEFFECTIV alEffectiv;
LPALEFFECTF alEffectf;
LPALEFFECTFV alEffectfv;
LPALGETEFFECTI alGetEffecti;
LPALGETEFFECTIV alGetEffectiv;
LPALGETEFFECTF alGetEffectf;
LPALGETEFFECTFV alGetEffectfv;
LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;

/* AL_EXT_debug */
LPALDEBUGMESSAGECALLBACKEXT alDebugMessageCallbackEXT;
LPALDEBUGMESSAGEINSERTEXT alDebugMessageInsertEXT;
LPALDEBUGMESSAGECONTROLEXT alDebugMessageControlEXT;
LPALPUSHDEBUGGROUPEXT alPushDebugGroupEXT;
LPALPOPDEBUGGROUPEXT alPopDebugGroupEXT;
LPALGETDEBUGMESSAGELOGEXT alGetDebugMessageLogEXT;
LPALOBJECTLABELEXT alObjectLabelEXT;
LPALGETOBJECTLABELEXT alGetObjectLabelEXT;
LPALGETPOINTEREXT alGetPointerEXT;
LPALGETPOINTERVEXT alGetPointervEXT;

/* AL_SOFT_source_latency */
LPALSOURCEDSOFT alSourcedSOFT;
LPALSOURCE3DSOFT alSource3dSOFT;
LPALSOURCEDVSOFT alSourcedvSOFT;
LPALGETSOURCEDSOFT alGetSourcedSOFT;
LPALGETSOURCE3DSOFT alGetSource3dSOFT;
LPALGETSOURCEDVSOFT alGetSourcedvSOFT;
LPALSOURCEI64SOFT alSourcei64SOFT;
LPALSOURCE3I64SOFT alSource3i64SOFT;
LPALSOURCEI64VSOFT alSourcei64vSOFT;
LPALGETSOURCEI64SOFT alGetSourcei64SOFT;
LPALGETSOURCE3I64SOFT alGetSource3i64SOFT;
LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT;

/* AL_SOFT_events */
LPALEVENTCONTROLSOFT alEventControlSOFT;
LPALEVENTCALLBACKSOFT alEventCallbackSOFT;

/* AL_SOFT_callback_buffer */
LPALBUFFERCALLBACKSOFT alBufferCallbackSOFT;


void LoadALExtensions(void)
{
    ALCdevice *device = alcGetContextsDevice(alcGetCurrentContext());
    if(!device)
        return;

#define LOAD_PROC(T, x)  ((x) = FUNCTION_CAST(T, alGetProcAddress(#x)))
    if(alcIsExtensionPresent(device, "ALC_EXT_EFX"))
    {
        LOAD_PROC(LPALGENFILTERS, alGenFilters);
        LOAD_PROC(LPALDELETEFILTERS, alDeleteFilters);
        LOAD_PROC(LPALISFILTER, alIsFilter);
        LOAD_PROC(LPALFILTERI, alFilteri);
        LOAD_PROC(LPALFILTERIV, alFilteriv);
        LOAD_PROC(LPALFILTERF, alFilterf);
        LOAD_PROC(LPALFILTERFV, alFilterfv);
        LOAD_PROC(LPALGETFILTERI, alGetFilteri);
        LOAD_PROC(LPALGETFILTERIV, alGetFilteriv);
        LOAD_PROC(LPALGETFILTERF, alGetFilterf);
        LOAD_PROC(LPALGETFILTERFV, alGetFilterfv);

        LOAD_PROC(LPALGENEFFECTS, alGenEffects);
        LOAD_PROC(LPALDELETEEFFECTS, alDeleteEffects);
        LOAD_PROC(LPALISEFFECT, alIsEffect);
        LOAD_PROC(LPALEFFECTI, alEffecti);
        LOAD_PROC(LPALEFFECTIV, alEffectiv);
        LOAD_PROC(LPALEFFECTF, alEffectf);
        LOAD_PROC(LPALEFFECTFV, alEffectfv);
        LOAD_PROC(LPALGETEFFECTI, alGetEffecti);
        LOAD_PROC(LPALGETEFFECTIV, alGetEffectiv);
        LOAD_PROC(LPALGETEFFECTF, alGetEffectf);
        LOAD_PROC(LPALGETEFFECTFV, alGetEffectfv);

        LOAD_PROC(LPALGENAUXILIARYEFFECTSLOTS, alGenAuxiliaryEffectSlots);
        LOAD_PROC(LPALDELETEAUXILIARYEFFECTSLOTS, alDeleteAuxiliaryEffectSlots);
        LOAD_PROC(LPALISAUXILIARYEFFECTSLOT, alIsAuxiliaryEffectSlot);
        LOAD_PROC(LPALAUXILIARYEFFECTSLOTI, alAuxiliaryEffectSloti);
        LOAD_PROC(LPALAUXILIARYEFFECTSLOTIV, alAuxiliaryEffectSlotiv);
        LOAD_PROC(LPALAUXILIARYEFFECTSLOTF, alAuxiliaryEffectSlotf);
        LOAD_PROC(LPALAUXILIARYEFFECTSLOTFV, alAuxiliaryEffectSlotfv);
        LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTI, alGetAuxiliaryEffectSloti);
        LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTIV, alGetAuxiliaryEffectSlotiv);
        LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTF, alGetAuxiliaryEffectSlotf);
        LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTFV, alGetAuxiliaryEffectSlotfv);
    }
    else
    {
        alGenFilters = NULL;
        alDeleteFilters = NULL;
        alIsFilter = NULL;
        alFilteri = NULL;
        alFilteriv = NULL;
        alFilterf = NULL;
        alFilterfv = NULL;
        alGetFilteri = NULL;
        alGetFilteriv = NULL;
        alGetFilterf = NULL;
        alGetFilterfv = NULL;

        alGenEffects = NULL;
        alDeleteEffects = NULL;
        alIsEffect = NULL;
        alEffecti = NULL;
        alEffectiv = NULL;
        alEffectf = NULL;
        alEffectfv = NULL;
        alGetEffecti = NULL;
        alGetEffectiv = NULL;
        alGetEffectf = NULL;
        alGetEffectfv = NULL;

        alGenAuxiliaryEffectSlots = NULL;
        alDeleteAuxiliaryEffectSlots = NULL;
        alIsAuxiliaryEffectSlot = NULL;
        alAuxiliaryEffectSloti = NULL;
        alAuxiliaryEffectSlotiv = NULL;
        alAuxiliaryEffectSlotf = NULL;
        alAuxiliaryEffectSlotfv = NULL;
        alGetAuxiliaryEffectSloti = NULL;
        alGetAuxiliaryEffectSlotiv = NULL;
        alGetAuxiliaryEffectSlotf = NULL;
        alGetAuxiliaryEffectSlotfv = NULL;
    }

    if(alIsExtensionPresent("AL_EXT_debug"))
    {
        LOAD_PROC(LPALDEBUGMESSAGECALLBACKEXT, alDebugMessageCallbackEXT);
        LOAD_PROC(LPALDEBUGMESSAGEINSERTEXT, alDebugMessageInsertEXT);
        LOAD_PROC(LPALDEBUGMESSAGECONTROLEXT, alDebugMessageControlEXT);
        LOAD_PROC(LPALPUSHDEBUGGROUPEXT, alPushDebugGroupEXT);
        LOAD_PROC(LPALPOPDEBUGGROUPEXT, alPopDebugGroupEXT);
        LOAD_PROC(LPALGETDEBUGMESSAGELOGEXT, alGetDebugMessageLogEXT);
        LOAD_PROC(LPALOBJECTLABELEXT, alObjectLabelEXT);
        LOAD_PROC(LPALGETOBJECTLABELEXT, alGetObjectLabelEXT);
        LOAD_PROC(LPALGETPOINTEREXT, alGetPointerEXT);
        LOAD_PROC(LPALGETPOINTERVEXT, alGetPointervEXT);
    }
    else
    {
        alDebugMessageCallbackEXT = NULL;
        alDebugMessageInsertEXT = NULL;
        alDebugMessageControlEXT = NULL;
        alPushDebugGroupEXT = NULL;
        alPopDebugGroupEXT = NULL;
        alGetDebugMessageLogEXT = NULL;
        alObjectLabelEXT = NULL;
        alGetObjectLabelEXT = NULL;
        alGetPointerEXT = NULL;
        alGetPointervEXT = NULL;
    }

    if(alIsExtensionPresent("AL_SOFT_source_latency"))
    {
        LOAD_PROC(LPALSOURCEDSOFT, alSourcedSOFT);
        LOAD_PROC(LPALSOURCE3DSOFT, alSource3dSOFT);
        LOAD_PROC(LPALSOURCEDVSOFT, alSourcedvSOFT);
        LOAD_PROC(LPALGETSOURCEDSOFT, alGetSourcedSOFT);
        LOAD_PROC(LPALGETSOURCE3DSOFT, alGetSource3dSOFT);
        LOAD_PROC(LPALGETSOURCEDVSOFT, alGetSourcedvSOFT);
        LOAD_PROC(LPALSOURCEI64SOFT, alSourcei64SOFT);
        LOAD_PROC(LPALSOURCE3I64SOFT, alSource3i64SOFT);
        LOAD_PROC(LPALSOURCEI64VSOFT, alSourcei64vSOFT);
        LOAD_PROC(LPALGETSOURCEI64SOFT, alGetSourcei64SOFT);
        LOAD_PROC(LPALGETSOURCE3I64SOFT, alGetSource3i64SOFT);
        LOAD_PROC(LPALGETSOURCEI64VSOFT, alGetSourcei64vSOFT);
    }
    else
    {
        alSourcedSOFT = NULL;
        alSource3dSOFT = NULL;
        alSourcedvSOFT = NULL;
        alGetSourcedSOFT = NULL;
        alGetSource3dSOFT = NULL;
        alGetSourcedvSOFT = NULL;
        alSourcei64SOFT = NULL;
        alSource3i64SOFT = NULL;
        alSourcei64vSOFT = NULL;
        alGetSourcei64SOFT = NULL;
        alGetSource3i64SOFT = NULL;
        alGetSourcei64vSOFT = NULL;
    }

    if(alIsExtensionPresent("AL_SOFT_events"))
    {
        LOAD_PROC(LPALEVENTCONTROLSOFT, alEventControlSOFT);
        LOAD_PROC(LPALEVENTCALLBACKSOFT, alEventCallbackSOFT);
    }
    else
    {
        alEventControlSOFT = NULL;
        alEventCallbackSOFT = NULL;
    }

    if(alIsExtensionPresent("AL_SOFT_callback_buffer"))
    {
        LOAD_PROC(LPALBUFFERCALLBACKSOFT, alBufferCallbackSOFT);
    }
    else
    {
        alBufferCallbackSOFT = NULL;
    }
#undef LOAD_PROC
}

/* InitAL opens a device and sets up a context using default attributes, making
 * the program ready to call OpenAL functions. */
int InitAL(char ***argv, int *argc)
{
    const ALCchar *name;
    ALCdevice *device;
    ALCcontext *ctx;

    /* Open and initialize a device */
    device = NULL;
    if(argc && argv && *argc > 1 && strcmp((*argv)[0], "-device") == 0)
    {
        device = alcOpenDevice((*argv)[1]);
        if(!device)
            fprintf(stderr, "Failed to open \"%s\", trying default\n", (*argv)[1]);
        (*argv) += 2;
        (*argc) -= 2;
    }
    if(!device)
        device = alcOpenDevice(NULL);
    if(!device)
    {
        fprintf(stderr, "Could not open a device!\n");
        return 1;
    }

    ctx = alcCreateContext(device, NULL);
    if(ctx == NULL || alcMakeContextCurrent(ctx) == ALC_FALSE)
    {
        if(ctx != NULL)
            alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not set a context!\n");
        return 1;
    }

    name = NULL;
    if(alcIsExtensionPresent(device, "ALC_ENUMERATE_ALL_EXT"))
        name = alcGetString(device, ALC_ALL_DEVICES_SPECIFIER);
    if(!name || alcGetError(device) != AL_NO_ERROR)
        name = alcGetString(device, ALC_DEVICE_SPECIFIER);
    printf("Opened \"%s\"\n", name);

    return 0;
}

/* CloseAL closes the device belonging to the current context, and destroys the
 * context. */
void CloseAL(void)
{
    ALCdevice *device;
    ALCcontext *ctx;

    ctx = alcGetCurrentContext();
    if(ctx == NULL)
        return;

    device = alcGetContextsDevice(ctx);

    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(device);
}


const char *FormatName(ALenum format)
{
    switch(format)
    {
    case AL_FORMAT_MONO8: return "Mono, U8";
    case AL_FORMAT_MONO16: return "Mono, S16";
    case AL_FORMAT_MONO_FLOAT32: return "Mono, Float32";
    case AL_FORMAT_MONO_MULAW: return "Mono, muLaw";
    case AL_FORMAT_MONO_ALAW_EXT: return "Mono, aLaw";
    case AL_FORMAT_MONO_IMA4: return "Mono, IMA4 ADPCM";
    case AL_FORMAT_MONO_MSADPCM_SOFT: return "Mono, MS ADPCM";
    case AL_FORMAT_STEREO8: return "Stereo, U8";
    case AL_FORMAT_STEREO16: return "Stereo, S16";
    case AL_FORMAT_STEREO_FLOAT32: return "Stereo, Float32";
    case AL_FORMAT_STEREO_MULAW: return "Stereo, muLaw";
    case AL_FORMAT_STEREO_ALAW_EXT: return "Stereo, aLaw";
    case AL_FORMAT_STEREO_IMA4: return "Stereo, IMA4 ADPCM";
    case AL_FORMAT_STEREO_MSADPCM_SOFT: return "Stereo, MS ADPCM";
    case AL_FORMAT_QUAD8: return "Quadraphonic, U8";
    case AL_FORMAT_QUAD16: return "Quadraphonic, S16";
    case AL_FORMAT_QUAD32: return "Quadraphonic, Float32";
    case AL_FORMAT_QUAD_MULAW: return "Quadraphonic, muLaw";
    case AL_FORMAT_51CHN8: return "5.1 Surround, U8";
    case AL_FORMAT_51CHN16: return "5.1 Surround, S16";
    case AL_FORMAT_51CHN32: return "5.1 Surround, Float32";
    case AL_FORMAT_51CHN_MULAW: return "5.1 Surround, muLaw";
    case AL_FORMAT_61CHN8: return "6.1 Surround, U8";
    case AL_FORMAT_61CHN16: return "6.1 Surround, S16";
    case AL_FORMAT_61CHN32: return "6.1 Surround, Float32";
    case AL_FORMAT_61CHN_MULAW: return "6.1 Surround, muLaw";
    case AL_FORMAT_71CHN8: return "7.1 Surround, U8";
    case AL_FORMAT_71CHN16: return "7.1 Surround, S16";
    case AL_FORMAT_71CHN32: return "7.1 Surround, Float32";
    case AL_FORMAT_71CHN_MULAW: return "7.1 Surround, muLaw";
    case AL_FORMAT_BFORMAT2D_8: return "B-Format 2D, U8";
    case AL_FORMAT_BFORMAT2D_16: return "B-Format 2D, S16";
    case AL_FORMAT_BFORMAT2D_FLOAT32: return "B-Format 2D, Float32";
    case AL_FORMAT_BFORMAT2D_MULAW: return "B-Format 2D, muLaw";
    case AL_FORMAT_BFORMAT3D_8: return "B-Format 3D, U8";
    case AL_FORMAT_BFORMAT3D_16: return "B-Format 3D, S16";
    case AL_FORMAT_BFORMAT3D_FLOAT32: return "B-Format 3D, Float32";
    case AL_FORMAT_BFORMAT3D_MULAW: return "B-Format 3D, muLaw";
    case AL_FORMAT_UHJ2CHN8_SOFT: return "UHJ 2-channel, U8";
    case AL_FORMAT_UHJ2CHN16_SOFT: return "UHJ 2-channel, S16";
    case AL_FORMAT_UHJ2CHN_FLOAT32_SOFT: return "UHJ 2-channel, Float32";
    case AL_FORMAT_UHJ3CHN8_SOFT: return "UHJ 3-channel, U8";
    case AL_FORMAT_UHJ3CHN16_SOFT: return "UHJ 3-channel, S16";
    case AL_FORMAT_UHJ3CHN_FLOAT32_SOFT: return "UHJ 3-channel, Float32";
    case AL_FORMAT_UHJ4CHN8_SOFT: return "UHJ 4-channel, U8";
    case AL_FORMAT_UHJ4CHN16_SOFT: return "UHJ 4-channel, S16";
    case AL_FORMAT_UHJ4CHN_FLOAT32_SOFT: return "UHJ 4-channel, Float32";
    }
    return "Unknown Format";
}


#ifdef _WIN32

#include <windows.h>
#include <mmsystem.h>

int altime_get(void)
{
    static int start_time = 0;
    int cur_time;
    union {
        FILETIME ftime;
        ULARGE_INTEGER ulint;
    } systime;
    GetSystemTimeAsFileTime(&systime.ftime);
    /* FILETIME is in 100-nanosecond units, or 1/10th of a microsecond. */
    cur_time = (int)(systime.ulint.QuadPart/10000);

    if(!start_time)
        start_time = cur_time;
    return cur_time - start_time;
}

void al_nssleep(unsigned long nsec)
{
    Sleep(nsec / 1000000);
}

#else

#include <sys/time.h>
#include <unistd.h>
#include <time.h>

int altime_get(void)
{
    static int start_time = 0u;
    int cur_time;

#if _POSIX_TIMERS > 0
    struct timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME, &ts);
    if(ret != 0) return 0;
    cur_time = (int)(ts.tv_sec*1000 + ts.tv_nsec/1000000);
#else /* _POSIX_TIMERS > 0 */
    struct timeval tv;
    int ret = gettimeofday(&tv, NULL);
    if(ret != 0) return 0;
    cur_time = (int)(tv.tv_sec*1000 + tv.tv_usec/1000);
#endif

    if(!start_time)
        start_time = cur_time;
    return cur_time - start_time;
}

void al_nssleep(unsigned long nsec)
{
    struct timespec ts;
    struct timespec rem;
    ts.tv_sec = (time_t)(nsec / 1000000000ul);
    ts.tv_nsec = (long)(nsec % 1000000000ul);
    while(nanosleep(&ts, &rem) == -1 && errno == EINTR)
        ts = rem;
}

#endif
