#include "config.h"

#include <stdlib.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alError.h"


typedef struct ALnullState {
    DERIVE_FROM_TYPE(ALeffectState);
} ALnullState;


/* This destructs (not free!) the effect state. It's called only when the
 * effect slot is no longer used.
 */
static ALvoid ALnullState_Destruct(ALnullState* UNUSED(state))
{
}

/* This updates the device-dependant effect state. This is called on
 * initialization and any time the device parameters (eg. playback frequency,
 * format) have been changed.
 */
static ALboolean ALnullState_deviceUpdate(ALnullState* UNUSED(state), ALCdevice* UNUSED(device))
{
    return AL_TRUE;
}

/* This updates the effect state. This is called any time the effect is
 * (re)loaded into a slot.
 */
static ALvoid ALnullState_update(ALnullState* UNUSED(state), ALCdevice* UNUSED(device), const ALeffectslot* UNUSED(slot))
{
}

/* This processes the effect state, for the given number of samples from the
 * input to the output buffer. The result should be added to the output buffer,
 * not replace it.
 */
static ALvoid ALnullState_process(ALnullState* UNUSED(state), ALuint UNUSED(samplesToDo), const ALfloat *restrict UNUSED(samplesIn), ALfloatBUFFERSIZE*restrict UNUSED(samplesOut), ALuint UNUSED(NumChannels))
{
}

/* This allocates memory to store the object, before it gets constructed.
 * DECLARE_DEFAULT_ALLOCATORS can be used to declate a default method.
 */
static void *ALnullState_New(size_t size)
{
    return malloc(size);
}

/* This frees the memory used by the object, after it has been destructed.
 * DECLARE_DEFAULT_ALLOCATORS can be used to declate a default method.
 */
static void ALnullState_Delete(void *ptr)
{
    free(ptr);
}

/* Define the forwards and the ALeffectState vtable for this type. */
DEFINE_ALEFFECTSTATE_VTABLE(ALnullState);


typedef struct ALnullStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALnullStateFactory;

/* Creates ALeffectState objects of the appropriate type. */
ALeffectState *ALnullStateFactory_create(ALnullStateFactory *UNUSED(factory))
{
    ALnullState *state;

    state = ALnullState_New(sizeof(*state));
    if(!state) return NULL;
    /* Set vtables for inherited types. */
    SET_VTABLE2(ALnullState, ALeffectState, state);

    return STATIC_CAST(ALeffectState, state);
}

/* Define the ALeffectStateFactory vtable for this type. */
DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALnullStateFactory);

ALeffectStateFactory *ALnullStateFactory_getFactory(void)
{
    static ALnullStateFactory NullFactory = { { GET_VTABLE2(ALnullStateFactory, ALeffectStateFactory) } };

    return STATIC_CAST(ALeffectStateFactory, &NullFactory);
}


void ALnull_setParami(ALeffect* UNUSED(effect), ALCcontext *context, ALenum param, ALint UNUSED(val))
{
    switch(param)
    {
        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALnull_setParamiv(ALeffect* UNUSED(effect), ALCcontext *context, ALenum param, const ALint* UNUSED(vals))
{
    switch(param)
    {
        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALnull_setParamf(ALeffect* UNUSED(effect), ALCcontext *context, ALenum param, ALfloat UNUSED(val))
{
    switch(param)
    {
        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALnull_setParamfv(ALeffect* UNUSED(effect), ALCcontext *context, ALenum param, const ALfloat* UNUSED(vals))
{
    switch(param)
    {
        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}

void ALnull_getParami(const ALeffect* UNUSED(effect), ALCcontext *context, ALenum param, ALint* UNUSED(val))
{
    switch(param)
    {
        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALnull_getParamiv(const ALeffect* UNUSED(effect), ALCcontext *context, ALenum param, ALint* UNUSED(vals))
{
    switch(param)
    {
        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALnull_getParamf(const ALeffect* UNUSED(effect), ALCcontext *context, ALenum param, ALfloat* UNUSED(val))
{
    switch(param)
    {
        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALnull_getParamfv(const ALeffect* UNUSED(effect), ALCcontext *context, ALenum param, ALfloat* UNUSED(vals))
{
    switch(param)
    {
        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}

DEFINE_ALEFFECT_VTABLE(ALnull);
