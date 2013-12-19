
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


AL_API void AL_APIENTRY alGenPresetsSOFT(ALsizei n, ALuint *ids)
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
        ALsfpreset *preset = calloc(1, sizeof(ALsfpreset));
        if(!preset)
        {
            alDeleteSoundfontsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, done);
        }
        ALsfpreset_Construct(preset);

        err = NewThunkEntry(&preset->id);
        if(err == AL_NO_ERROR)
            err = InsertUIntMapEntry(&device->PresetMap, preset->id, preset);
        if(err != AL_NO_ERROR)
        {
            FreeThunkEntry(preset->id);
            ALsfpreset_Destruct(preset);
            memset(preset, 0, sizeof(*preset));
            free(preset);

            alDeleteSoundfontsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, err, done);
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
        if(!ids[i])
            continue;

        /* Check for valid ID */
        if((preset=LookupPreset(device, ids[i])) == NULL)
            SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
        if(preset->ref != 0)
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    for(i = 0;i < n;i++)
    {
        if((preset=RemovePreset(device, ids[i])) == NULL)
            continue;
        FreeThunkEntry(preset->id);

        ALsfpreset_Destruct(preset);

        memset(preset, 0, sizeof(*preset));
        free(preset);
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

    ret = ((!id || LookupPreset(context->Device, id)) ?
           AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(context);

    return ret;
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

        FreeThunkEntry(temp->id);
        ALsfpreset_Destruct(temp);

        memset(temp, 0, sizeof(*temp));
        free(temp);
    }
}
