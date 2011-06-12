/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* This is an OpenAL backend for Android using the native audio APIs based on
 * OpenSL ES 1.0.1. It is based on source code for the native-audio sample app
 * bundled with NDK.
 */

#include "config.h"

#include <stdlib.h>
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"


#include <SLES/OpenSLES.h>
#if 1
#include <SLES/OpenSLES_Android.h>
#else
extern SLAPIENTRY const SLInterfaceID SL_IID_ANDROIDSIMPLEBUFFERQUEUE;

struct SLAndroidSimpleBufferQueueItf_;
typedef const struct SLAndroidSimpleBufferQueueItf_ * const * SLAndroidSimpleBufferQueueItf;

typedef void (*slAndroidSimpleBufferQueueCallback)(SLAndroidSimpleBufferQueueItf caller, void *pContext);

typedef struct SLAndroidSimpleBufferQueueState_ {
    SLuint32 count;
    SLuint32 index;
} SLAndroidSimpleBufferQueueState;


struct SLAndroidSimpleBufferQueueItf_ {
    SLresult (*Enqueue) (
        SLAndroidSimpleBufferQueueItf self,
        const void *pBuffer,
        SLuint32 size
    );
    SLresult (*Clear) (
        SLAndroidSimpleBufferQueueItf self
    );
    SLresult (*GetState) (
        SLAndroidSimpleBufferQueueItf self,
        SLAndroidSimpleBufferQueueState *pState
    );
    SLresult (*RegisterCallback) (
        SLAndroidSimpleBufferQueueItf self,
        slAndroidSimpleBufferQueueCallback callback,
        void* pContext
    );
};

#define SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE ((SLuint32) 0x800007BD)

typedef struct SLDataLocator_AndroidSimpleBufferQueue {
    SLuint32 locatorType;
    SLuint32 numBuffers;
} SLDataLocator_AndroidSimpleBufferQueue;

#endif

/* Helper macros */
#define SLObjectItf_Realize(a,b)        ((*(a))->Realize((a),(b)))
#define SLObjectItf_GetInterface(a,b,c) ((*(a))->GetInterface((a),(b),(c)))
#define SLObjectItf_Destroy(a)          ((*(a))->Destroy((a)))

#define SLEngineItf_CreateOutputMix(a,b,c,d,e)       ((*(a))->CreateOutputMix((a),(b),(c),(d),(e)))
#define SLEngineItf_CreateAudioPlayer(a,b,c,d,e,f,g) ((*(a))->CreateAudioPlayer((a),(b),(c),(d),(e),(f),(g)))

#define SLPlayItf_SetPlayState(a,b) ((*(a))->SetPlayState((a),(b)))


typedef struct {
    /* engine interfaces */
    SLObjectItf engineObject;
    SLEngineItf engine;

    /* output mix interfaces */
    SLObjectItf outputMix;

    /* buffer queue player interfaces */
    SLObjectItf bufferQueueObject;

    void *buffer;
    ALuint bufferSize;

    ALuint frameSize;
} osl_data;


static const ALCchar opensl_device[] = "OpenSL";


/* this callback handler is called every time a buffer finishes playing */
static void opensl_callback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
    ALCdevice *Device = context;
    osl_data *data = Device->ExtraData;

    aluMixData(Device, data->buffer, data->bufferSize/data->frameSize);

    (*bq)->Enqueue(bq, data->buffer, data->bufferSize);
}


static ALCboolean opensl_open_playback(ALCdevice *Device, const ALCchar *deviceName)
{
    osl_data *data = NULL;
    SLresult result;

    if(!deviceName)
        deviceName = opensl_device;
    else if(strcmp(deviceName, opensl_device) != 0)
        return ALC_FALSE;

    data = calloc(1, sizeof(*data));
    if(!data)
    {
        alcSetError(Device, ALC_OUT_OF_MEMORY);
        return ALC_FALSE;
    }

    // create engine
    result = slCreateEngine(&data->engineObject, 0, NULL, 0, NULL, NULL);
    if(SL_RESULT_SUCCESS == result)
        result = SLObjectItf_Realize(data->engineObject, SL_BOOLEAN_FALSE);
    if(SL_RESULT_SUCCESS == result)
        result = SLObjectItf_GetInterface(data->engineObject, SL_IID_ENGINE, &data->engine);
    if(SL_RESULT_SUCCESS == result)
        result = SLEngineItf_CreateOutputMix(data->engine, &data->outputMix, 0, NULL, NULL);
    if(SL_RESULT_SUCCESS == result)
        result = SLObjectItf_Realize(data->outputMix, SL_BOOLEAN_FALSE);

    if(SL_RESULT_SUCCESS != result)
    {
        if(data->outputMix != NULL)
            SLObjectItf_Destroy(data->outputMix);
        data->outputMix = NULL;

        if(data->engineObject != NULL)
            SLObjectItf_Destroy(data->engineObject);
        data->engineObject = NULL;
        data->engine = NULL;

        free(data);
        return ALC_FALSE;
    }

    Device->szDeviceName = strdup(deviceName);
    Device->ExtraData = data;

    return ALC_TRUE;
}


static void opensl_close_playback(ALCdevice *Device)
{
    osl_data *data = Device->ExtraData;

    SLObjectItf_Destroy(data->outputMix);
    data->outputMix = NULL;

    SLObjectItf_Destroy(data->engineObject);
    data->engineObject = NULL;
    data->engine = NULL;

    free(data);
    Device->ExtraData = NULL;
}

static ALCboolean opensl_reset_playback(ALCdevice *Device)
{
    osl_data *data = Device->ExtraData;
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq;
    SLAndroidSimpleBufferQueueItf bufferQueue;
    SLDataLocator_OutputMix loc_outmix;
    SLDataFormat_PCM format_pcm;
    SLDataSource audioSrc;
    SLDataSink audioSnk;
    SLPlayItf player;
    SLInterfaceID id;
    SLboolean req;
    SLresult result;
    ALuint i;


    Device->UpdateSize = (ALuint64)Device->UpdateSize * 44100 / Device->Frequency;
    Device->UpdateSize = Device->UpdateSize * Device->NumUpdates / 2;
    Device->NumUpdates = 2;

    Device->Frequency = 44100;
    Device->FmtChans = DevFmtStereo;
    Device->FmtType = DevFmtShort;

    SetDefaultWFXChannelOrder(Device);


    id  = SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
    req = SL_BOOLEAN_TRUE;

    loc_bufq.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
    loc_bufq.numBuffers = 2;

    format_pcm.formatType = SL_DATAFORMAT_PCM;
    format_pcm.numChannels = 2;
    format_pcm.samplesPerSec = SL_SAMPLINGRATE_44_1;
    format_pcm.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    format_pcm.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
    format_pcm.channelMask = SL_SPEAKER_FRONT_LEFT|SL_SPEAKER_FRONT_RIGHT;
    format_pcm.endianness = SL_BYTEORDER_NATIVE;

    audioSrc.pLocator = &loc_bufq;
    audioSrc.pFormat = &format_pcm;

    loc_outmix.locatorType = SL_DATALOCATOR_OUTPUTMIX;
    loc_outmix.outputMix = data->outputMix;
    audioSnk.pLocator = &loc_outmix;
    audioSnk.pFormat = NULL;


    result = SLEngineItf_CreateAudioPlayer(data->engine, &data->bufferQueueObject, &audioSrc, &audioSnk, 1, &id, &req);
    if(SL_RESULT_SUCCESS == result)
        result = SLObjectItf_Realize(data->bufferQueueObject, SL_BOOLEAN_FALSE);
    if(SL_RESULT_SUCCESS == result)
        result = SLObjectItf_GetInterface(data->bufferQueueObject, SL_IID_BUFFERQUEUE, &bufferQueue);
    if(SL_RESULT_SUCCESS == result)
        result = (*bufferQueue)->RegisterCallback(bufferQueue, opensl_callback, Device);
    if(SL_RESULT_SUCCESS == result)
    {
        data->frameSize = FrameSizeFromDevFmt(Device->FmtChans, Device->FmtType);
        data->bufferSize = Device->UpdateSize * data->frameSize;
        data->buffer = calloc(1, data->bufferSize);
        if(!data->buffer)
            result = SL_RESULT_MEMORY_FAILURE;
    }
    /* enqueue the first buffer to kick off the callbacks */
    for(i = 0;i < loc_bufq.numBuffers;i++)
    {
        if(SL_RESULT_SUCCESS == result)
            result = (*bufferQueue)->Enqueue(bufferQueue, data->buffer, data->bufferSize);
    }
    if(SL_RESULT_SUCCESS == result)
        result = SLObjectItf_GetInterface(data->bufferQueueObject, SL_IID_PLAY, &player);
    if(SL_RESULT_SUCCESS == result)
        result = SLPlayItf_SetPlayState(player, SL_PLAYSTATE_PLAYING);

    if(SL_RESULT_SUCCESS != result)
    {
        if(data->bufferQueueObject != NULL)
            SLObjectItf_Destroy(data->bufferQueueObject);
        data->bufferQueueObject = NULL;

        free(data->buffer);
        data->buffer = NULL;
        data->bufferSize = 0;

        return ALC_FALSE;
    }

    return ALC_TRUE;
}


static void opensl_stop_playback(ALCdevice *Device)
{
    osl_data *data = Device->ExtraData;

    if(data->bufferQueueObject != NULL)
        SLObjectItf_Destroy(data->bufferQueueObject);
    data->bufferQueueObject = NULL;

    free(data->buffer);
    data->buffer = NULL;
    data->bufferSize = 0
}

static ALCboolean opensl_open_capture(ALCdevice *Device, const ALCchar *deviceName)
{
    return ALC_FALSE;
    (void)Device;
    (void)deviceName;
}


static const BackendFuncs opensl_funcs = {
    opensl_open_playback,
    opensl_close_playback,
    opensl_reset_playback,
    opensl_stop_playback,
    opensl_open_capture,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


void alc_opensl_init(BackendFuncs *func_list)
{
    *func_list = opensl_funcs;
}

void alc_opensl_deinit(void)
{
}

void alc_opensl_probe(int type)
{
    switch(type)
    {
        case DEVICE_PROBE:
            AppendDeviceList(opensl_device);
            break;
        case ALL_DEVICE_PROBE:
            AppendAllDeviceList(opensl_device);
            break;
        default:
            break;
    }
}
