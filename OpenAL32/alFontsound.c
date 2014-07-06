
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "alMidi.h"
#include "alError.h"
#include "alThunk.h"
#include "alBuffer.h"

#include "midi/base.h"


extern inline struct ALfontsound *LookupFontsound(ALCdevice *device, ALuint id);
extern inline struct ALfontsound *RemoveFontsound(ALCdevice *device, ALuint id);


static void ALfontsound_Construct(ALfontsound *self);
static void ALfontsound_Destruct(ALfontsound *self);
void ALfontsound_setPropi(ALfontsound *self, ALCcontext *context, ALenum param, ALint value);
static ALsfmodulator *ALfontsound_getModStage(ALfontsound *self, ALsizei stage);
void ALfontsound_setModStagei(ALfontsound *self, ALCcontext *context, ALsizei stage, ALenum param, ALint value);
static void ALfontsound_getModStagei(ALfontsound *self, ALCcontext *context, ALsizei stage, ALenum param, ALint *values);

static inline struct ALsfmodulator *LookupModulator(ALfontsound *sound, ALuint id)
{
    ALsfmodulator *mod = LookupUIntMapKey(&sound->ModulatorMap, id>>2);
    if(mod) mod += id&3;
    return mod;
}


AL_API void AL_APIENTRY alGenFontsoundsSOFT(ALsizei n, ALuint *ids)
{
    ALCcontext *context;
    ALsizei cur = 0;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    for(cur = 0;cur < n;cur++)
    {
        ALfontsound *sound = NewFontsound(context);
        if(!sound)
        {
            alDeleteFontsoundsSOFT(cur, ids);
            break;
        }

        ids[cur] = sound->id;
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDeleteFontsoundsSOFT(ALsizei n, const ALuint *ids)
{
    ALCdevice *device;
    ALCcontext *context;
    ALfontsound *inst;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    for(i = 0;i < n;i++)
    {
        /* Check for valid ID */
        if((inst=LookupFontsound(device, ids[i])) == NULL)
            SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
        if(ReadRef(&inst->ref) != 0)
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    for(i = 0;i < n;i++)
    {
        if((inst=LookupFontsound(device, ids[i])) != NULL)
            DeleteFontsound(device, inst);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsFontsoundSOFT(ALuint id)
{
    ALCcontext *context;
    ALboolean ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    ret = LookupFontsound(context->Device, id) ? AL_TRUE : AL_FALSE;

    ALCcontext_DecRef(context);

    return ret;
}

AL_API void AL_APIENTRY alFontsoundiSOFT(ALuint id, ALenum param, ALint value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALfontsound *sound;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(!(sound=LookupFontsound(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(ReadRef(&sound->ref) != 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);

    ALfontsound_setPropi(sound, context, param, value);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alFontsound2iSOFT(ALuint id, ALenum param, ALint value1, ALint value2)
{
    ALCdevice *device;
    ALCcontext *context;
    ALfontsound *sound;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(!(sound=LookupFontsound(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(ReadRef(&sound->ref) != 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    switch(param)
    {
        case AL_KEY_RANGE_SOFT:
            if(!(value1 >= 0 && value1 <= 127 && value2 >= 0 && value2 <= 127 && value2 >= value1))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            sound->MinKey = value1;
            sound->MaxKey = value2;
            break;

        case AL_VELOCITY_RANGE_SOFT:
            if(!(value1 >= 0 && value1 <= 127 && value2 >= 0 && value2 <= 127 && value2 >= value1))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            sound->MinVelocity = value1;
            sound->MaxVelocity = value2;
            break;

        default:
            SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alFontsoundivSOFT(ALuint id, ALenum param, const ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    ALfontsound *sound;

    switch(param)
    {
        case AL_KEY_RANGE_SOFT:
        case AL_VELOCITY_RANGE_SOFT:
            alFontsound2iSOFT(id, param, values[0], values[1]);
            return;

        case AL_MOD_LFO_TO_PITCH_SOFT:
        case AL_VIBRATO_LFO_TO_PITCH_SOFT:
        case AL_MOD_ENV_TO_PITCH_SOFT:
        case AL_FILTER_CUTOFF_SOFT:
        case AL_FILTER_RESONANCE_SOFT:
        case AL_MOD_LFO_TO_FILTER_CUTOFF_SOFT:
        case AL_MOD_ENV_TO_FILTER_CUTOFF_SOFT:
        case AL_MOD_LFO_TO_VOLUME_SOFT:
        case AL_CHORUS_SEND_SOFT:
        case AL_REVERB_SEND_SOFT:
        case AL_PAN_SOFT:
        case AL_MOD_LFO_DELAY_SOFT:
        case AL_MOD_LFO_FREQUENCY_SOFT:
        case AL_VIBRATO_LFO_DELAY_SOFT:
        case AL_VIBRATO_LFO_FREQUENCY_SOFT:
        case AL_MOD_ENV_DELAYTIME_SOFT:
        case AL_MOD_ENV_ATTACKTIME_SOFT:
        case AL_MOD_ENV_HOLDTIME_SOFT:
        case AL_MOD_ENV_DECAYTIME_SOFT:
        case AL_MOD_ENV_SUSTAINVOLUME_SOFT:
        case AL_MOD_ENV_RELEASETIME_SOFT:
        case AL_MOD_ENV_KEY_TO_HOLDTIME_SOFT:
        case AL_MOD_ENV_KEY_TO_DECAYTIME_SOFT:
        case AL_VOLUME_ENV_DELAYTIME_SOFT:
        case AL_VOLUME_ENV_ATTACKTIME_SOFT:
        case AL_VOLUME_ENV_HOLDTIME_SOFT:
        case AL_VOLUME_ENV_DECAYTIME_SOFT:
        case AL_VOLUME_ENV_SUSTAINVOLUME_SOFT:
        case AL_VOLUME_ENV_RELEASETIME_SOFT:
        case AL_VOLUME_ENV_KEY_TO_HOLDTIME_SOFT:
        case AL_VOLUME_ENV_KEY_TO_DECAYTIME_SOFT:
        case AL_ATTENUATION_SOFT:
        case AL_TUNING_COARSE_SOFT:
        case AL_TUNING_FINE_SOFT:
        case AL_LOOP_MODE_SOFT:
        case AL_TUNING_SCALE_SOFT:
        case AL_EXCLUSIVE_CLASS_SOFT:
        case AL_SAMPLE_START_SOFT:
        case AL_SAMPLE_END_SOFT:
        case AL_SAMPLE_LOOP_START_SOFT:
        case AL_SAMPLE_LOOP_END_SOFT:
        case AL_SAMPLE_RATE_SOFT:
        case AL_BASE_KEY_SOFT:
        case AL_KEY_CORRECTION_SOFT:
        case AL_SAMPLE_TYPE_SOFT:
        case AL_FONTSOUND_LINK_SOFT:
            alFontsoundiSOFT(id, param, values[0]);
            return;
    }

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(!(sound=LookupFontsound(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(ReadRef(&sound->ref) != 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    switch(param)
    {
        default:
            SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alGetFontsoundivSOFT(ALuint id, ALenum param, ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    const ALfontsound *sound;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(!(sound=LookupFontsound(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
        case AL_BUFFER:
            values[0] = (sound->Buffer ? sound->Buffer->id : 0);
            break;

        case AL_MOD_LFO_TO_PITCH_SOFT:
            values[0] = sound->ModLfoToPitch;
            break;

        case AL_VIBRATO_LFO_TO_PITCH_SOFT:
            values[0] = sound->VibratoLfoToPitch;
            break;

        case AL_MOD_ENV_TO_PITCH_SOFT:
            values[0] = sound->ModEnvToPitch;
            break;

        case AL_FILTER_CUTOFF_SOFT:
            values[0] = sound->FilterCutoff;
            break;

        case AL_FILTER_RESONANCE_SOFT:
            values[0] = sound->FilterQ;
            break;

        case AL_MOD_LFO_TO_FILTER_CUTOFF_SOFT:
            values[0] = sound->ModLfoToFilterCutoff;
            break;

        case AL_MOD_ENV_TO_FILTER_CUTOFF_SOFT:
            values[0] = sound->ModEnvToFilterCutoff;
            break;

        case AL_MOD_LFO_TO_VOLUME_SOFT:
            values[0] = sound->ModLfoToVolume;
            break;

        case AL_CHORUS_SEND_SOFT:
            values[0] = sound->ChorusSend;
            break;

        case AL_REVERB_SEND_SOFT:
            values[0] = sound->ReverbSend;
            break;

        case AL_PAN_SOFT:
            values[0] = sound->Pan;
            break;

        case AL_MOD_LFO_DELAY_SOFT:
            values[0] = sound->ModLfo.Delay;
            break;
        case AL_MOD_LFO_FREQUENCY_SOFT:
            values[0] = sound->ModLfo.Frequency;
            break;

        case AL_VIBRATO_LFO_DELAY_SOFT:
            values[0] = sound->VibratoLfo.Delay;
            break;
        case AL_VIBRATO_LFO_FREQUENCY_SOFT:
            values[0] = sound->VibratoLfo.Frequency;
            break;

        case AL_MOD_ENV_DELAYTIME_SOFT:
            values[0] = sound->ModEnv.DelayTime;
            break;
        case AL_MOD_ENV_ATTACKTIME_SOFT:
            values[0] = sound->ModEnv.AttackTime;
            break;
        case AL_MOD_ENV_HOLDTIME_SOFT:
            values[0] = sound->ModEnv.HoldTime;
            break;
        case AL_MOD_ENV_DECAYTIME_SOFT:
            values[0] = sound->ModEnv.DecayTime;
            break;
        case AL_MOD_ENV_SUSTAINVOLUME_SOFT:
            values[0] = sound->ModEnv.SustainAttn;
            break;
        case AL_MOD_ENV_RELEASETIME_SOFT:
            values[0] = sound->ModEnv.ReleaseTime;
            break;
        case AL_MOD_ENV_KEY_TO_HOLDTIME_SOFT:
            values[0] = sound->ModEnv.KeyToHoldTime;
            break;
        case AL_MOD_ENV_KEY_TO_DECAYTIME_SOFT:
            values[0] = sound->ModEnv.KeyToDecayTime;
            break;

        case AL_VOLUME_ENV_DELAYTIME_SOFT:
            values[0] = sound->VolEnv.DelayTime;
            break;
        case AL_VOLUME_ENV_ATTACKTIME_SOFT:
            values[0] = sound->VolEnv.AttackTime;
            break;
        case AL_VOLUME_ENV_HOLDTIME_SOFT:
            values[0] = sound->VolEnv.HoldTime;
            break;
        case AL_VOLUME_ENV_DECAYTIME_SOFT:
            values[0] = sound->VolEnv.DecayTime;
            break;
        case AL_VOLUME_ENV_SUSTAINVOLUME_SOFT:
            values[0] = sound->VolEnv.SustainAttn;
            break;
        case AL_VOLUME_ENV_RELEASETIME_SOFT:
            values[0] = sound->VolEnv.ReleaseTime;
            break;
        case AL_VOLUME_ENV_KEY_TO_HOLDTIME_SOFT:
            values[0] = sound->VolEnv.KeyToHoldTime;
            break;
        case AL_VOLUME_ENV_KEY_TO_DECAYTIME_SOFT:
            values[0] = sound->VolEnv.KeyToDecayTime;
            break;

        case AL_KEY_RANGE_SOFT:
            values[0] = sound->MinKey;
            values[1] = sound->MaxKey;
            break;

        case AL_VELOCITY_RANGE_SOFT:
            values[0] = sound->MinVelocity;
            values[1] = sound->MaxVelocity;
            break;

        case AL_ATTENUATION_SOFT:
            values[0] = sound->Attenuation;
            break;

        case AL_TUNING_COARSE_SOFT:
            values[0] = sound->CoarseTuning;
            break;
        case AL_TUNING_FINE_SOFT:
            values[0] = sound->FineTuning;
            break;

        case AL_LOOP_MODE_SOFT:
            values[0] = sound->LoopMode;
            break;

        case AL_TUNING_SCALE_SOFT:
            values[0] = sound->TuningScale;
            break;

        case AL_EXCLUSIVE_CLASS_SOFT:
            values[0] = sound->ExclusiveClass;
            break;

        case AL_SAMPLE_START_SOFT:
            values[0] = sound->Start;
            break;

        case AL_SAMPLE_END_SOFT:
            values[0] = sound->End;
            break;

        case AL_SAMPLE_LOOP_START_SOFT:
            values[0] = sound->LoopStart;
            break;

        case AL_SAMPLE_LOOP_END_SOFT:
            values[0] = sound->LoopEnd;
            break;

        case AL_SAMPLE_RATE_SOFT:
            values[0] = sound->SampleRate;
            break;

        case AL_BASE_KEY_SOFT:
            values[0] = sound->PitchKey;
            break;

        case AL_KEY_CORRECTION_SOFT:
            values[0] = sound->PitchCorrection;
            break;

        case AL_SAMPLE_TYPE_SOFT:
            values[0] = sound->SampleType;
            break;

        case AL_FONTSOUND_LINK_SOFT:
            values[0] = (sound->Link ? sound->Link->id : 0);
            break;

        default:
            SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alFontsoundModulatoriSOFT(ALuint id, ALsizei stage, ALenum param, ALint value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALfontsound *sound;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(!(sound=LookupFontsound(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    ALfontsound_setModStagei(sound, context, stage, param, value);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alGetFontsoundModulatorivSOFT(ALuint id, ALsizei stage, ALenum param, ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    ALfontsound *sound;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(!(sound=LookupFontsound(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    ALfontsound_getModStagei(sound, context, stage, param, values);

done:
    ALCcontext_DecRef(context);
}


ALfontsound *NewFontsound(ALCcontext *context)
{
    ALCdevice *device = context->Device;
    ALfontsound *sound;
    ALenum  err;

    sound = calloc(1, sizeof(*sound));
    if(!sound)
        SET_ERROR_AND_RETURN_VALUE(context, AL_OUT_OF_MEMORY, NULL);
    ALfontsound_Construct(sound);

    err = NewThunkEntry(&sound->id);
    if(err == AL_NO_ERROR)
        err = InsertUIntMapEntry(&device->FontsoundMap, sound->id, sound);
    if(err != AL_NO_ERROR)
    {
        ALfontsound_Destruct(sound);
        memset(sound, 0, sizeof(*sound));
        free(sound);

        SET_ERROR_AND_RETURN_VALUE(context, err, NULL);
    }

    return sound;
}

void DeleteFontsound(ALCdevice *device, ALfontsound *sound)
{
    RemoveFontsound(device, sound->id);

    ALfontsound_Destruct(sound);

    memset(sound, 0, sizeof(*sound));
    free(sound);
}


static void ALfontsound_Construct(ALfontsound *self)
{
    InitRef(&self->ref, 0);

    self->Buffer = NULL;

    self->MinKey = 0;
    self->MaxKey = 127;
    self->MinVelocity = 0;
    self->MaxVelocity = 127;

    self->ModLfoToPitch = 0;
    self->VibratoLfoToPitch = 0;
    self->ModEnvToPitch = 0;

    self->FilterCutoff = 13500;
    self->FilterQ = 0;
    self->ModLfoToFilterCutoff = 0;
    self->ModEnvToFilterCutoff = 0;
    self->ModLfoToVolume = 0;

    self->ChorusSend = 0;
    self->ReverbSend = 0;

    self->Pan = 0;

    self->ModLfo.Delay = 0;
    self->ModLfo.Frequency = 0;

    self->VibratoLfo.Delay = 0;
    self->VibratoLfo.Frequency = 0;

    self->ModEnv.DelayTime = -12000;
    self->ModEnv.AttackTime = -12000;
    self->ModEnv.HoldTime = -12000;
    self->ModEnv.DecayTime = -12000;
    self->ModEnv.SustainAttn = 0;
    self->ModEnv.ReleaseTime = -12000;
    self->ModEnv.KeyToHoldTime = 0;
    self->ModEnv.KeyToDecayTime = 0;

    self->VolEnv.DelayTime = -12000;
    self->VolEnv.AttackTime = -12000;
    self->VolEnv.HoldTime = -12000;
    self->VolEnv.DecayTime = -12000;
    self->VolEnv.SustainAttn = 0;
    self->VolEnv.ReleaseTime = -12000;
    self->VolEnv.KeyToHoldTime = 0;
    self->VolEnv.KeyToDecayTime = 0;

    self->Attenuation = 0;

    self->CoarseTuning = 0;
    self->FineTuning = 0;

    self->LoopMode = AL_NONE;

    self->TuningScale = 100;

    self->ExclusiveClass = 0;

    self->Start = 0;
    self->End = 0;
    self->LoopStart = 0;
    self->LoopEnd = 0;
    self->SampleRate = 0;
    self->PitchKey = 0;
    self->PitchCorrection = 0;
    self->SampleType = AL_MONO_SOFT;
    self->Link = NULL;

    InitUIntMap(&self->ModulatorMap, ~0);

    self->id = 0;
}

static void ALfontsound_Destruct(ALfontsound *self)
{
    ALsizei i;

    FreeThunkEntry(self->id);
    self->id = 0;

    if(self->Buffer)
        DecrementRef(&self->Buffer->ref);
    self->Buffer = NULL;
    if(self->Link)
        DecrementRef(&self->Link->ref);
    self->Link = NULL;

    for(i = 0;i < self->ModulatorMap.size;i++)
    {
        free(self->ModulatorMap.array[i].value);
        self->ModulatorMap.array[i].value = NULL;
    }
    ResetUIntMap(&self->ModulatorMap);
}

void ALfontsound_setPropi(ALfontsound *self, ALCcontext *context, ALenum param, ALint value)
{
    ALfontsound *link;
    ALbuffer *buffer;

    switch(param)
    {
        case AL_BUFFER:
            buffer = value ? LookupBuffer(context->Device, value) : NULL;
            if(value && !buffer)
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            else if(buffer)
            {
                /* Buffer must have a non-0 length, and must be mono. */
                if(buffer->SampleLen <= 0 || buffer->FmtChannels != FmtMono)
                    SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            }

            if(buffer) IncrementRef(&buffer->ref);
            if((buffer=ExchangePtr((XchgPtr*)&self->Buffer, buffer)) != NULL)
                DecrementRef(&buffer->ref);
            break;

        case AL_MOD_LFO_TO_PITCH_SOFT:
            self->ModLfoToPitch = value;
            break;

        case AL_VIBRATO_LFO_TO_PITCH_SOFT:
            self->VibratoLfoToPitch = value;
            break;

        case AL_MOD_ENV_TO_PITCH_SOFT:
            self->ModEnvToPitch = value;
            break;

        case AL_FILTER_CUTOFF_SOFT:
            self->FilterCutoff = value;
            break;

        case AL_FILTER_RESONANCE_SOFT:
            if(!(value >= 0))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            self->FilterQ = value;
            break;

        case AL_MOD_LFO_TO_FILTER_CUTOFF_SOFT:
            self->ModLfoToFilterCutoff = value;
            break;

        case AL_MOD_ENV_TO_FILTER_CUTOFF_SOFT:
            self->ModEnvToFilterCutoff = value;
            break;

        case AL_MOD_LFO_TO_VOLUME_SOFT:
            self->ModLfoToVolume = value;
            break;

        case AL_CHORUS_SEND_SOFT:
            if(!(value >= 0 && value <= 1000))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            self->ChorusSend = value;
            break;
        case AL_REVERB_SEND_SOFT:
            if(!(value >= 0 && value <= 1000))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            self->ReverbSend = value;
            break;

        case AL_PAN_SOFT:
            self->Pan = value;
            break;

        case AL_MOD_LFO_DELAY_SOFT:
            self->ModLfo.Delay = value;
            break;
        case AL_MOD_LFO_FREQUENCY_SOFT:
            self->ModLfo.Frequency = value;
            break;

        case AL_VIBRATO_LFO_DELAY_SOFT:
            self->VibratoLfo.Delay = value;
            break;
        case AL_VIBRATO_LFO_FREQUENCY_SOFT:
            self->VibratoLfo.Frequency = value;
            break;

        case AL_MOD_ENV_DELAYTIME_SOFT:
            self->ModEnv.DelayTime = value;
            break;
        case AL_MOD_ENV_ATTACKTIME_SOFT:
            self->ModEnv.AttackTime = value;
            break;
        case AL_MOD_ENV_HOLDTIME_SOFT:
            self->ModEnv.HoldTime = value;
            break;
        case AL_MOD_ENV_DECAYTIME_SOFT:
            self->ModEnv.DecayTime = value;
            break;
        case AL_MOD_ENV_SUSTAINVOLUME_SOFT:
            self->ModEnv.SustainAttn = value;
            break;
        case AL_MOD_ENV_RELEASETIME_SOFT:
            self->ModEnv.ReleaseTime = value;
            break;
        case AL_MOD_ENV_KEY_TO_HOLDTIME_SOFT:
            self->ModEnv.KeyToHoldTime = value;
            break;
        case AL_MOD_ENV_KEY_TO_DECAYTIME_SOFT:
            self->ModEnv.KeyToDecayTime = value;
            break;

        case AL_VOLUME_ENV_DELAYTIME_SOFT:
            self->VolEnv.DelayTime = value;
            break;
        case AL_VOLUME_ENV_ATTACKTIME_SOFT:
            self->VolEnv.AttackTime = value;
            break;
        case AL_VOLUME_ENV_HOLDTIME_SOFT:
            self->VolEnv.HoldTime = value;
            break;
        case AL_VOLUME_ENV_DECAYTIME_SOFT:
            self->VolEnv.DecayTime = value;
            break;
        case AL_VOLUME_ENV_SUSTAINVOLUME_SOFT:
            self->VolEnv.SustainAttn = value;
            break;
        case AL_VOLUME_ENV_RELEASETIME_SOFT:
            self->VolEnv.ReleaseTime = value;
            break;
        case AL_VOLUME_ENV_KEY_TO_HOLDTIME_SOFT:
            self->VolEnv.KeyToHoldTime = value;
            break;
        case AL_VOLUME_ENV_KEY_TO_DECAYTIME_SOFT:
            self->VolEnv.KeyToDecayTime = value;
            break;

        case AL_ATTENUATION_SOFT:
            if(!(value >= 0))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            self->Attenuation = value;
            break;

        case AL_TUNING_COARSE_SOFT:
            self->CoarseTuning = value;
            break;
        case AL_TUNING_FINE_SOFT:
            self->FineTuning = value;
            break;

        case AL_LOOP_MODE_SOFT:
            if(!(value == AL_NONE || value == AL_LOOP_CONTINUOUS_SOFT ||
                 value == AL_LOOP_UNTIL_RELEASE_SOFT))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            self->LoopMode = value;
            break;

        case AL_TUNING_SCALE_SOFT:
            self->TuningScale = value;
            break;

        case AL_EXCLUSIVE_CLASS_SOFT:
            self->ExclusiveClass = value;
            break;

        case AL_SAMPLE_START_SOFT:
            self->Start = value;
            break;

        case AL_SAMPLE_END_SOFT:
            self->End = value;
            break;

        case AL_SAMPLE_LOOP_START_SOFT:
            self->LoopStart = value;
            break;

        case AL_SAMPLE_LOOP_END_SOFT:
            self->LoopEnd = value;
            break;

        case AL_SAMPLE_RATE_SOFT:
            if(!(value > 0))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            self->SampleRate = value;
            break;

        case AL_BASE_KEY_SOFT:
            if(!((value >= 0 && value <= 127) || value == 255))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            self->PitchKey = value;
            break;

        case AL_KEY_CORRECTION_SOFT:
            if(!(value >= -99 && value <= 99))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            self->PitchCorrection = value;
            break;

        case AL_SAMPLE_TYPE_SOFT:
            if(!(value == AL_MONO_SOFT || value == AL_RIGHT_SOFT || value == AL_LEFT_SOFT))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            self->SampleType = value;
            break;

        case AL_FONTSOUND_LINK_SOFT:
            link = value ? LookupFontsound(context->Device, value) : NULL;
            if(value && !link)
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);

            if(link) IncrementRef(&link->ref);
            if((link=ExchangePtr((XchgPtr*)&self->Link, link)) != NULL)
                DecrementRef(&link->ref);
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}

static ALsfmodulator *ALfontsound_getModStage(ALfontsound *self, ALsizei stage)
{
    ALsfmodulator *ret = LookupModulator(self, stage);
    if(!ret)
    {
        static const ALsfmodulator moddef = {
            { { AL_ONE_SOFT, AL_UNORM_SOFT, AL_LINEAR_SOFT },
              { AL_ONE_SOFT, AL_UNORM_SOFT, AL_LINEAR_SOFT } },
            0,
            AL_LINEAR_SOFT,
            AL_NONE
        };
        ret = malloc(sizeof(ALsfmodulator[4]));
        ret[0] = moddef;
        ret[1] = moddef;
        ret[2] = moddef;
        ret[3] = moddef;
        InsertUIntMapEntry(&self->ModulatorMap, stage>>2, ret);
        ret += stage&3;
    }
    return ret;
}

void ALfontsound_setModStagei(ALfontsound *self, ALCcontext *context, ALsizei stage, ALenum param, ALint value)
{
    ALint srcidx = 0;

    if(ReadRef(&self->ref) != 0)
        SET_ERROR_AND_RETURN(context, AL_INVALID_OPERATION);
    switch(param)
    {
        case AL_SOURCE1_INPUT_SOFT:
            srcidx++;
            /* fall-through */
        case AL_SOURCE0_INPUT_SOFT:
            if(!(value == AL_ONE_SOFT || value == AL_NOTEON_VELOCITY_SOFT ||
                 value == AL_NOTEON_KEY_SOFT || value == AL_KEYPRESSURE_SOFT ||
                 value == AL_CHANNELPRESSURE_SOFT || value == AL_PITCHBEND_SOFT ||
                 value == AL_PITCHBEND_SENSITIVITY_SOFT ||
                 IsValidCtrlInput(value)))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            ALfontsound_getModStage(self, stage)->Source[srcidx].Input = value;
            break;

        case AL_SOURCE1_TYPE_SOFT:
            srcidx++;
            /* fall-through */
        case AL_SOURCE0_TYPE_SOFT:
            if(!(value == AL_UNORM_SOFT || value == AL_UNORM_REV_SOFT ||
                 value == AL_SNORM_SOFT || value == AL_SNORM_REV_SOFT))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            ALfontsound_getModStage(self, stage)->Source[srcidx].Type = value;
            break;

        case AL_SOURCE1_FORM_SOFT:
            srcidx++;
            /* fall-through */
        case AL_SOURCE0_FORM_SOFT:
            if(!(value == AL_LINEAR_SOFT || value == AL_CONCAVE_SOFT ||
                 value == AL_CONVEX_SOFT || value == AL_SWITCH_SOFT))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            ALfontsound_getModStage(self, stage)->Source[srcidx].Form = value;
            break;

        case AL_AMOUNT_SOFT:
            ALfontsound_getModStage(self, stage)->Amount = value;
            break;

        case AL_TRANSFORM_OP_SOFT:
            if(!(value == AL_LINEAR_SOFT || value == AL_ABSOLUTE_SOFT))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            ALfontsound_getModStage(self, stage)->TransformOp = value;
            break;

        case AL_DESTINATION_SOFT:
            if(!(value == AL_MOD_LFO_TO_PITCH_SOFT || value == AL_VIBRATO_LFO_TO_PITCH_SOFT ||
                 value == AL_MOD_ENV_TO_PITCH_SOFT || value == AL_FILTER_CUTOFF_SOFT ||
                 value == AL_FILTER_RESONANCE_SOFT || value == AL_MOD_LFO_TO_FILTER_CUTOFF_SOFT ||
                 value == AL_MOD_ENV_TO_FILTER_CUTOFF_SOFT || value == AL_MOD_LFO_TO_VOLUME_SOFT ||
                 value == AL_CHORUS_SEND_SOFT || value == AL_REVERB_SEND_SOFT || value == AL_PAN_SOFT ||
                 value == AL_MOD_LFO_DELAY_SOFT || value == AL_MOD_LFO_FREQUENCY_SOFT ||
                 value == AL_VIBRATO_LFO_DELAY_SOFT || value == AL_VIBRATO_LFO_FREQUENCY_SOFT ||
                 value == AL_MOD_ENV_DELAYTIME_SOFT || value == AL_MOD_ENV_ATTACKTIME_SOFT ||
                 value == AL_MOD_ENV_HOLDTIME_SOFT || value == AL_MOD_ENV_DECAYTIME_SOFT ||
                 value == AL_MOD_ENV_SUSTAINVOLUME_SOFT || value == AL_MOD_ENV_RELEASETIME_SOFT ||
                 value == AL_MOD_ENV_KEY_TO_HOLDTIME_SOFT || value == AL_MOD_ENV_KEY_TO_DECAYTIME_SOFT ||
                 value == AL_VOLUME_ENV_DELAYTIME_SOFT || value == AL_VOLUME_ENV_ATTACKTIME_SOFT ||
                 value == AL_VOLUME_ENV_HOLDTIME_SOFT || value == AL_VOLUME_ENV_DECAYTIME_SOFT ||
                 value == AL_VOLUME_ENV_SUSTAINVOLUME_SOFT || value == AL_VOLUME_ENV_RELEASETIME_SOFT ||
                 value == AL_VOLUME_ENV_KEY_TO_HOLDTIME_SOFT || value == AL_VOLUME_ENV_KEY_TO_DECAYTIME_SOFT ||
                 value == AL_ATTENUATION_SOFT || value == AL_TUNING_COARSE_SOFT ||
                 value == AL_TUNING_FINE_SOFT || value == AL_TUNING_SCALE_SOFT))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            ALfontsound_getModStage(self, stage)->Dest = value;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}

static void ALfontsound_getModStagei(ALfontsound *self, ALCcontext *context, ALsizei stage, ALenum param, ALint *values)
{
    ALsfmodulator *mod = LookupModulator(self, stage);
    ALint srcidx = 0;

    switch(param)
    {
        case AL_SOURCE1_INPUT_SOFT:
            srcidx++;
            /* fall-through */
        case AL_SOURCE0_INPUT_SOFT:
            values[0] = mod ? mod->Source[srcidx].Input : AL_ONE_SOFT;
            break;

        case AL_SOURCE1_TYPE_SOFT:
            srcidx++;
            /* fall-through */
        case AL_SOURCE0_TYPE_SOFT:
            values[0] = mod ? mod->Source[srcidx].Type : AL_UNORM_SOFT;
            break;

        case AL_SOURCE1_FORM_SOFT:
            srcidx++;
            /* fall-through */
        case AL_SOURCE0_FORM_SOFT:
            values[0] = mod ? mod->Source[srcidx].Form : AL_LINEAR_SOFT;
            break;

        case AL_AMOUNT_SOFT:
            values[0] = mod ? mod->Amount : 0;
            break;

        case AL_TRANSFORM_OP_SOFT:
            values[0] = mod ? mod->TransformOp : AL_LINEAR_SOFT;
            break;

        case AL_DESTINATION_SOFT:
            values[0] = mod ? mod->Dest : AL_NONE;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}


/* ReleaseALFontsounds
 *
 * Called to destroy any fontsounds that still exist on the device
 */
void ReleaseALFontsounds(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->FontsoundMap.size;i++)
    {
        ALfontsound *temp = device->FontsoundMap.array[i].value;
        device->FontsoundMap.array[i].value = NULL;

        ALfontsound_Destruct(temp);

        memset(temp, 0, sizeof(*temp));
        free(temp);
    }
}
