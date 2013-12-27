
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

AL_API void AL_APIENTRY alFontsoundiSOFT(ALuint id, ALenum param, ALint UNUSED(value))
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
        case AL_KEY_RANGE_SOFT:
            values[0] = sound->MinKey;
            values[1] = sound->MaxKey;
            break;

        case AL_VELOCITY_RANGE_SOFT:
            values[0] = sound->MinVelocity;
            values[1] = sound->MaxVelocity;
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
