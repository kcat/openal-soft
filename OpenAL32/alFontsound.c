
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "alMidi.h"
#include "alError.h"
#include "alThunk.h"

#include "midi/base.h"


extern inline struct ALfontsound *LookupFontsound(ALCdevice *device, ALuint id);
extern inline struct ALfontsound *RemoveFontsound(ALCdevice *device, ALuint id);


AL_API void AL_APIENTRY alGenFontsoundsSOFT(ALsizei n, ALuint *ids)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsizei cur = 0;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    for(cur = 0;cur < n;cur++)
    {
        ALfontsound *inst = calloc(1, sizeof(ALfontsound));
        if(!inst)
        {
            alDeleteFontsoundsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, done);
        }
        ALfontsound_Construct(inst);

        err = NewThunkEntry(&inst->id);
        if(err == AL_NO_ERROR)
            err = InsertUIntMapEntry(&device->FontsoundMap, inst->id, inst);
        if(err != AL_NO_ERROR)
        {
            ALfontsound_Destruct(inst);
            memset(inst, 0, sizeof(*inst));
            free(inst);

            alDeleteFontsoundsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, err, done);
        }

        ids[cur] = inst->id;
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
        if(inst->ref != 0)
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    for(i = 0;i < n;i++)
    {
        if((inst=RemoveFontsound(device, ids[i])) == NULL)
            continue;

        ALfontsound_Destruct(inst);

        memset(inst, 0, sizeof(*inst));
        free(inst);
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
    ALfontsound *link;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(!(sound=LookupFontsound(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(sound->ref != 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    switch(param)
    {
        case AL_MOD_LFO_TO_PITCH_SOFT:
            sound->ModLfoToPitch = value;
            break;

        case AL_VIBRATO_LFO_TO_PITCH_SOFT:
            sound->VibratoLfoToPitch = value;
            break;

        case AL_MOD_ENV_TO_PITCH_SOFT:
            sound->ModEnvToPitch = value;
            break;

        case AL_FILTER_CUTOFF_SOFT:
            sound->FilterCutoff = value;
            break;

        case AL_FILTER_RESONANCE_SOFT:
            if(!(value >= 0))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            sound->FilterQ = value;
            break;

        case AL_MOD_LFO_TO_FILTER_CUTOFF_SOFT:
            sound->ModLfoToFilterCutoff = value;
            break;

        case AL_MOD_ENV_TO_FILTER_CUTOFF_SOFT:
            sound->ModEnvToFilterCutoff = value;
            break;

        case AL_MOD_ENV_DELAYTIME_SOFT:
            sound->ModEnv.DelayTime = value;
            break;
        case AL_MOD_ENV_ATTACKTIME_SOFT:
            sound->ModEnv.AttackTime = value;
            break;
        case AL_MOD_ENV_HOLDTIME_SOFT:
            sound->ModEnv.HoldTime = value;
            break;
        case AL_MOD_ENV_DECAYTIME_SOFT:
            sound->ModEnv.DecayTime = value;
            break;
        case AL_MOD_ENV_SUSTAINVOLUME_SOFT:
            sound->ModEnv.SustainVol = value;
            break;
        case AL_MOD_ENV_RELEASETIME_SOFT:
            sound->ModEnv.ReleaseTime = value;
            break;
        case AL_MOD_ENV_KEY_TO_HOLDTIME_SOFT:
            sound->ModEnv.KeyToHoldTime = value;
            break;
        case AL_MOD_ENV_KEY_TO_DECAYTIME_SOFT:
            sound->ModEnv.KeyToDecayTime = value;
            break;

        case AL_VOLUME_ENV_DELAYTIME_SOFT:
            sound->VolEnv.DelayTime = value;
            break;
        case AL_VOLUME_ENV_ATTACKTIME_SOFT:
            sound->VolEnv.AttackTime = value;
            break;
        case AL_VOLUME_ENV_HOLDTIME_SOFT:
            sound->VolEnv.HoldTime = value;
            break;
        case AL_VOLUME_ENV_DECAYTIME_SOFT:
            sound->VolEnv.DecayTime = value;
            break;
        case AL_VOLUME_ENV_SUSTAINVOLUME_SOFT:
            sound->VolEnv.SustainVol = value;
            break;
        case AL_VOLUME_ENV_RELEASETIME_SOFT:
            sound->VolEnv.ReleaseTime = value;
            break;
        case AL_VOLUME_ENV_KEY_TO_HOLDTIME_SOFT:
            sound->VolEnv.KeyToHoldTime = value;
            break;
        case AL_VOLUME_ENV_KEY_TO_DECAYTIME_SOFT:
            sound->VolEnv.KeyToDecayTime = value;
            break;

        case AL_SAMPLE_START_SOFT:
            sound->Start = value;
            break;

        case AL_SAMPLE_END_SOFT:
            sound->End = value;
            break;

        case AL_SAMPLE_LOOP_START_SOFT:
            sound->LoopStart = value;
            break;

        case AL_SAMPLE_LOOP_END_SOFT:
            sound->LoopEnd = value;
            break;

        case AL_SAMPLE_RATE_SOFT:
            if(!(value > 0))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            sound->SampleRate = value;
            break;

        case AL_LOOP_MODE_SOFT:
            if(!(value == AL_NONE || value == AL_LOOP_CONTINUOUS_SOFT ||
                 value == AL_LOOP_UNTIL_RELEASE_SOFT))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            sound->LoopMode = value;
            break;

        case AL_BASE_KEY_SOFT:
            if(!((value >= 0 && value <= 127) || value == 255))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            sound->PitchKey = value;
            break;

        case AL_KEY_CORRECTION_SOFT:
            if(!(value > -100 && value < 100))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            sound->PitchCorrection = value;
            break;

        case AL_SAMPLE_TYPE_SOFT:
            if(!(value >= 1 && value <= 4))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            sound->SampleType = value;
            break;

        case AL_FONTSOUND_LINK_SOFT:
            if(!value)
                link = NULL;
            else
            {
                link = LookupFontsound(device, value);
                if(!link)
                    SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            }
            if(link) IncrementRef(&link->ref);
            link = ExchangePtr((XchgPtr*)&sound->Link, link);
            if(link) DecrementRef(&link->ref);
            break;

        default:
            SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

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
    if(sound->ref != 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    switch(param)
    {
        case AL_KEY_RANGE_SOFT:
            if(!(value1 >= 0 && value1 <= 127 && value2 >= 0 && value2 <= 127))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            sound->MinKey = value1;
            sound->MaxKey = value2;
            break;

        case AL_VELOCITY_RANGE_SOFT:
            if(!(value1 >= 0 && value1 <= 127 && value2 >= 0 && value2 <= 127))
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
        case AL_SAMPLE_START_SOFT:
        case AL_SAMPLE_END_SOFT:
        case AL_SAMPLE_LOOP_START_SOFT:
        case AL_SAMPLE_LOOP_END_SOFT:
        case AL_SAMPLE_RATE_SOFT:
        case AL_LOOP_MODE_SOFT:
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
    if(sound->ref != 0)
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
            values[0] = sound->ModEnv.SustainVol;
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
            values[0] = sound->VolEnv.SustainVol;
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

        case AL_LOOP_MODE_SOFT:
            values[0] = sound->LoopMode;
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
