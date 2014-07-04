
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "alMidi.h"
#include "alError.h"
#include "alThunk.h"

#include "midi/base.h"


extern inline struct ALsfpreset *LookupPreset(ALCdevice *device, ALuint id);
extern inline struct ALsfpreset *RemovePreset(ALCdevice *device, ALuint id);

static void ALsfpreset_Construct(ALsfpreset *self);
static void ALsfpreset_Destruct(ALsfpreset *self);


AL_API void AL_APIENTRY alGenPresetsSOFT(ALsizei n, ALuint *ids)
{
    ALCcontext *context;
    ALsizei cur = 0;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    for(cur = 0;cur < n;cur++)
    {
        ALsfpreset *preset = NewPreset(context);
        if(!preset)
        {
            alDeletePresetsSOFT(cur, ids);
            break;
        }

        ids[cur] = preset->id;
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDeletePresetsSOFT(ALsizei n, const ALuint *ids)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsfpreset *preset;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    for(i = 0;i < n;i++)
    {
        /* Check for valid ID */
        if((preset=LookupPreset(device, ids[i])) == NULL)
            SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
        if(ReadRef(&preset->ref) != 0)
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    for(i = 0;i < n;i++)
    {
        if((preset=LookupPreset(device, ids[i])) != NULL)
            DeletePreset(device, preset);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsPresetSOFT(ALuint id)
{
    ALCcontext *context;
    ALboolean ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    ret = LookupPreset(context->Device, id) ? AL_TRUE : AL_FALSE;

    ALCcontext_DecRef(context);

    return ret;
}

AL_API void AL_APIENTRY alPresetiSOFT(ALuint id, ALenum param, ALint value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsfpreset *preset;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((preset=LookupPreset(device, id)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(ReadRef(&preset->ref) != 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    switch(param)
    {
        case AL_MIDI_PRESET_SOFT:
            if(!(value >= 0 && value <= 127))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            preset->Preset = value;
            break;

        case AL_MIDI_BANK_SOFT:
            if(!(value >= 0 && value <= 128))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            preset->Bank = value;
            break;

        default:
            SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alPresetivSOFT(ALuint id, ALenum param, const ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsfpreset *preset;

    switch(param)
    {
        case AL_MIDI_PRESET_SOFT:
        case AL_MIDI_BANK_SOFT:
            alPresetiSOFT(id, param, values[0]);
            return;
    }

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((preset=LookupPreset(device, id)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(ReadRef(&preset->ref) != 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    switch(param)
    {
        default:
            SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alGetPresetivSOFT(ALuint id, ALenum param, ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsfpreset *preset;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((preset=LookupPreset(device, id)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
        case AL_MIDI_PRESET_SOFT:
            values[0] = preset->Preset;
            break;

        case AL_MIDI_BANK_SOFT:
            values[0] = preset->Bank;
            break;

        case AL_FONTSOUNDS_SIZE_SOFT:
            values[0] = preset->NumSounds;
            break;

        case AL_FONTSOUNDS_SOFT:
            for(i = 0;i < preset->NumSounds;i++)
                values[i] = preset->Sounds[i]->id;
            break;

        default:
            SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alPresetFontsoundsSOFT(ALuint id, ALsizei count, const ALuint *fsids)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsfpreset *preset;
    ALfontsound **sounds;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(!(preset=LookupPreset(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(count < 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    if(ReadRef(&preset->ref) != 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);

    if(count == 0)
        sounds = NULL;
    else
    {
        sounds = calloc(count, sizeof(sounds[0]));
        if(!sounds)
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, done);

        for(i = 0;i < count;i++)
        {
            if(!(sounds[i]=LookupFontsound(device, fsids[i])))
            {
                free(sounds);
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            }
        }
    }

    for(i = 0;i < count;i++)
        IncrementRef(&sounds[i]->ref);

    sounds = ExchangePtr((XchgPtr*)&preset->Sounds, sounds);
    count = ExchangeInt(&preset->NumSounds, count);

    for(i = 0;i < count;i++)
        DecrementRef(&sounds[i]->ref);
    free(sounds);

done:
    ALCcontext_DecRef(context);
}


ALsfpreset *NewPreset(ALCcontext *context)
{
    ALCdevice *device = context->Device;
    ALsfpreset *preset;
    ALenum err;

    preset = calloc(1, sizeof(*preset));
    if(!preset)
        SET_ERROR_AND_RETURN_VALUE(context, AL_OUT_OF_MEMORY, NULL);
    ALsfpreset_Construct(preset);

    err = NewThunkEntry(&preset->id);
    if(err == AL_NO_ERROR)
        err = InsertUIntMapEntry(&device->PresetMap, preset->id, preset);
    if(err != AL_NO_ERROR)
    {
        ALsfpreset_Destruct(preset);
        memset(preset, 0, sizeof(*preset));
        free(preset);

        SET_ERROR_AND_RETURN_VALUE(context, err, NULL);
    }

    return preset;
}

void DeletePreset(ALCdevice *device, ALsfpreset *preset)
{
    RemovePreset(device, preset->id);

    ALsfpreset_Destruct(preset);
    memset(preset, 0, sizeof(*preset));
    free(preset);
}


static void ALsfpreset_Construct(ALsfpreset *self)
{
    InitRef(&self->ref, 0);

    self->Preset = 0;
    self->Bank = 0;

    self->Sounds = NULL;
    self->NumSounds = 0;

    self->id = 0;
}

static void ALsfpreset_Destruct(ALsfpreset *self)
{
    ALsizei i;

    FreeThunkEntry(self->id);
    self->id = 0;

    for(i = 0;i < self->NumSounds;i++)
        DecrementRef(&self->Sounds[i]->ref);
    free(self->Sounds);
    self->Sounds = NULL;
    self->NumSounds = 0;
}


/* ReleaseALPresets
 *
 * Called to destroy any presets that still exist on the device
 */
void ReleaseALPresets(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->PresetMap.size;i++)
    {
        ALsfpreset *temp = device->PresetMap.array[i].value;
        device->PresetMap.array[i].value = NULL;

        ALsfpreset_Destruct(temp);

        memset(temp, 0, sizeof(*temp));
        free(temp);
    }
}
