
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "alMidi.h"
#include "alError.h"
#include "alThunk.h"

#include "midi/base.h"


extern inline struct ALsfinstrument *LookupInstrument(ALCdevice *device, ALuint id);
extern inline struct ALsfinstrument *RemoveInstrument(ALCdevice *device, ALuint id);


AL_API void AL_APIENTRY alGenInstrumentsSOFT(ALsizei n, ALuint *ids)
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
        ALsfinstrument *inst = calloc(1, sizeof(ALsfinstrument));
        if(!inst)
        {
            alDeleteInstrumentsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, done);
        }
        ALsfinstrument_Construct(inst);

        err = NewThunkEntry(&inst->id);
        if(err == AL_NO_ERROR)
            err = InsertUIntMapEntry(&device->InstrumentMap, inst->id, inst);
        if(err != AL_NO_ERROR)
        {
            FreeThunkEntry(inst->id);
            ALsfinstrument_Destruct(inst);
            memset(inst, 0, sizeof(*inst));
            free(inst);

            alDeleteInstrumentsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, err, done);
        }

        ids[cur] = inst->id;
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDeleteInstrumentsSOFT(ALsizei n, const ALuint *ids)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsfinstrument *inst;
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
        if((inst=LookupInstrument(device, ids[i])) == NULL)
            SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
        if(inst->ref != 0)
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    for(i = 0;i < n;i++)
    {
        if((inst=RemoveInstrument(device, ids[i])) == NULL)
            continue;
        FreeThunkEntry(inst->id);

        ALsfinstrument_Destruct(inst);

        memset(inst, 0, sizeof(*inst));
        free(inst);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsInstrumentSOFT(ALuint id)
{
    ALCcontext *context;
    ALboolean ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    ret = ((!id || LookupInstrument(context->Device, id)) ?
           AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(context);

    return ret;
}


/* ReleaseALInstruments
 *
 * Called to destroy any instruments that still exist on the device
 */
void ReleaseALInstruments(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->InstrumentMap.size;i++)
    {
        ALsfinstrument *temp = device->InstrumentMap.array[i].value;
        device->InstrumentMap.array[i].value = NULL;

        FreeThunkEntry(temp->id);
        ALsfinstrument_Destruct(temp);

        memset(temp, 0, sizeof(*temp));
        free(temp);
    }
}
