#include "config.h"

#include <stdlib.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alError.h"


typedef struct ALnullStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALnullStateFactory;

static ALnullStateFactory NullFactory;


typedef struct ALnullState {
    DERIVE_FROM_TYPE(ALeffectState);
} ALnullState;

static ALvoid ALnullState_Destruct(ALnullState *state)
{
    (void)state;
}
static ALboolean ALnullState_DeviceUpdate(ALnullState *state, ALCdevice *device)
{
    return AL_TRUE;
    (void)state;
    (void)device;
}
static ALvoid ALnullState_Update(ALnullState *state, ALCdevice *device, const ALeffectslot *slot)
{
    (void)state;
    (void)device;
    (void)slot;
}
static ALvoid ALnullState_Process(ALnullState *state, ALuint samplesToDo, const ALfloat *restrict samplesIn, ALfloat (*restrict samplesOut)[BUFFERSIZE])
{
    (void)state;
    (void)samplesToDo;
    (void)samplesIn;
    (void)samplesOut;
}
static ALeffectStateFactory *ALnullState_getCreator(void)
{
    return STATIC_CAST(ALeffectStateFactory, &NullFactory);
}

DEFINE_ALEFFECTSTATE_VTABLE(ALnullState);


ALeffectState *ALnullStateFactory_create(void)
{
    ALnullState *state;

    state = calloc(1, sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALnullState, ALeffectState, state);

    return STATIC_CAST(ALeffectState, state);
}

static ALvoid ALnullStateFactory_destroy(ALeffectState *effect)
{
    ALnullState *state = STATIC_UPCAST(ALnullState, ALeffectState, effect);
    ALnullState_Destruct(state);
    free(state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALnullStateFactory);


static void init_none_factory(void)
{
    SET_VTABLE2(ALnullStateFactory, ALeffectStateFactory, &NullFactory);
}

ALeffectStateFactory *ALnullStateFactory_getFactory(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_none_factory);
    return STATIC_CAST(ALeffectStateFactory, &NullFactory);
}


void null_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{ (void)effect;(void)param;(void)val; alSetError(context, AL_INVALID_ENUM); }
void null_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{ (void)effect;(void)param;(void)vals; alSetError(context, AL_INVALID_ENUM); }
void null_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{ (void)effect;(void)param;(void)val; alSetError(context, AL_INVALID_ENUM); }
void null_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{ (void)effect;(void)param;(void)vals; alSetError(context, AL_INVALID_ENUM); }

void null_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{ (void)effect;(void)param;(void)val; alSetError(context, AL_INVALID_ENUM); }
void null_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{ (void)effect;(void)param;(void)vals; alSetError(context, AL_INVALID_ENUM); }
void null_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{ (void)effect;(void)param;(void)val; alSetError(context, AL_INVALID_ENUM); }
void null_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{ (void)effect;(void)param;(void)vals; alSetError(context, AL_INVALID_ENUM); }
