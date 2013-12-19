
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "alMidi.h"
#include "alThunk.h"
#include "alError.h"

#include "midi/base.h"


extern inline struct ALsoundfont *LookupSfont(ALCdevice *device, ALuint id);
extern inline struct ALsoundfont *RemoveSfont(ALCdevice *device, ALuint id);


AL_API void AL_APIENTRY alGenSoundfontsSOFT(ALsizei n, ALuint *ids)
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
        ALsoundfont *sfont = calloc(1, sizeof(ALsoundfont));
        if(!sfont)
        {
            alDeleteSoundfontsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, done);
        }
        ALsoundfont_Construct(sfont);

        err = NewThunkEntry(&sfont->id);
        if(err == AL_NO_ERROR)
            err = InsertUIntMapEntry(&device->SfontMap, sfont->id, sfont);
        if(err != AL_NO_ERROR)
        {
            FreeThunkEntry(sfont->id);
            ALsoundfont_Destruct(sfont);
            memset(sfont, 0, sizeof(ALsoundfont));
            free(sfont);

            alDeleteSoundfontsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, err, done);
        }

        ids[cur] = sfont->id;
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDeleteSoundfontsSOFT(ALsizei n, const ALuint *ids)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsoundfont *sfont;
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

        /* Check for valid soundfont ID */
        if((sfont=LookupSfont(device, ids[i])) == NULL)
            SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
        if(sfont->ref != 0)
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    for(i = 0;i < n;i++)
    {
        if((sfont=RemoveSfont(device, ids[i])) == NULL)
            continue;
        FreeThunkEntry(sfont->id);

        ALsoundfont_Destruct(sfont);

        memset(sfont, 0, sizeof(*sfont));
        free(sfont);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsSoundfontSOFT(ALuint id)
{
    ALCcontext *context;
    ALboolean ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    ret = ((!id || LookupSfont(context->Device, id)) ?
           AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(context);

    return ret;
}


/* ReleaseALSoundfonts
 *
 * Called to destroy any soundfonts that still exist on the device
 */
void ReleaseALSoundfonts(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->SfontMap.size;i++)
    {
        ALsoundfont *temp = device->SfontMap.array[i].value;
        device->SfontMap.array[i].value = NULL;

        FreeThunkEntry(temp->id);
        ALsoundfont_Destruct(temp);

        memset(temp, 0, sizeof(*temp));
        free(temp);
    }
}
