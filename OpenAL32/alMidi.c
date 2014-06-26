
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "alMain.h"
#include "alMidi.h"
#include "alError.h"
#include "alThunk.h"
#include "evtqueue.h"
#include "rwlock.h"
#include "alu.h"

#include "midi/base.h"


MidiSynth *SynthCreate(ALCdevice *device)
{
    MidiSynth *synth = NULL;
    if(!synth) synth = SSynth_create(device);
    if(!synth) synth = FSynth_create(device);
    if(!synth) synth = DSynth_create(device);
    return synth;
}


AL_API void AL_APIENTRY alMidiSoundfontSOFT(ALuint id)
{
    alMidiSoundfontvSOFT(1, &id);
}

AL_API void AL_APIENTRY alMidiSoundfontvSOFT(ALsizei count, const ALuint *ids)
{
    ALCdevice *device;
    ALCcontext *context;
    MidiSynth *synth;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    if(count < 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    synth = device->Synth;

    WriteLock(&synth->Lock);
    if(synth->State == AL_PLAYING || synth->State == AL_PAUSED)
        alSetError(context, AL_INVALID_OPERATION);
    else
    {
        err = V(synth,selectSoundfonts)(context, count, ids);
        if(err != AL_NO_ERROR)
            alSetError(context, err);
    }
    WriteUnlock(&synth->Lock);

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alMidiEventSOFT(ALuint64SOFT time, ALenum event, ALsizei channel, ALsizei param1, ALsizei param2)
{
    ALCdevice *device;
    ALCcontext *context;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    if(!(event == AL_NOTEOFF_SOFT || event == AL_NOTEON_SOFT ||
         event == AL_KEYPRESSURE_SOFT || event == AL_CONTROLLERCHANGE_SOFT ||
         event == AL_PROGRAMCHANGE_SOFT || event == AL_CHANNELPRESSURE_SOFT ||
         event == AL_PITCHBEND_SOFT))
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    if(!(channel >= 0 && channel <= 15))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(!(param1 >= 0 && param1 <= 127))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(!(param2 >= 0 && param2 <= 127))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    ALCdevice_Lock(device);
    err = MidiSynth_insertEvent(device->Synth, time, event|channel, param1, param2);
    ALCdevice_Unlock(device);
    if(err != AL_NO_ERROR)
        alSetError(context, err);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiSysExSOFT(ALuint64SOFT time, const ALbyte *data, ALsizei size)
{
    ALCdevice *device;
    ALCcontext *context;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    if(!data || size < 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    ALCdevice_Lock(device);
    err = MidiSynth_insertSysExEvent(device->Synth, time, data, size);
    ALCdevice_Unlock(device);
    if(err != AL_NO_ERROR)
        alSetError(context, err);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiPlaySOFT(void)
{
    ALCcontext *context;
    MidiSynth *synth;

    context = GetContextRef();
    if(!context) return;

    synth = context->Device->Synth;
    WriteLock(&synth->Lock);
    MidiSynth_setState(synth, AL_PLAYING);
    WriteUnlock(&synth->Lock);

    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiPauseSOFT(void)
{
    ALCcontext *context;
    MidiSynth *synth;

    context = GetContextRef();
    if(!context) return;

    synth = context->Device->Synth;
    WriteLock(&synth->Lock);
    MidiSynth_setState(synth, AL_PAUSED);
    WriteUnlock(&synth->Lock);

    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiStopSOFT(void)
{
    ALCdevice *device;
    ALCcontext *context;
    MidiSynth *synth;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    synth = device->Synth;

    WriteLock(&synth->Lock);
    MidiSynth_setState(synth, AL_STOPPED);

    ALCdevice_Lock(device);
    V0(synth,stop)();
    ALCdevice_Unlock(device);
    WriteUnlock(&synth->Lock);

    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiResetSOFT(void)
{
    ALCdevice *device;
    ALCcontext *context;
    MidiSynth *synth;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    synth = device->Synth;

    WriteLock(&synth->Lock);
    MidiSynth_setState(synth, AL_INITIAL);

    ALCdevice_Lock(device);
    V0(synth,reset)();
    ALCdevice_Unlock(device);
    WriteUnlock(&synth->Lock);

    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alMidiGainSOFT(ALfloat value)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value >= 0.0f && isfinite(value)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    V(device->Synth,setGain)(value);

done:
    ALCcontext_DecRef(context);
}
