#ifndef _AL_AUXEFFECTSLOT_H_
#define _AL_AUXEFFECTSLOT_H_

#include "alMain.h"
#include "alEffect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALeffectStateFactory ALeffectStateFactory;

typedef struct ALeffectState ALeffectState;
typedef struct ALeffectslot ALeffectslot;

struct ALeffectStateVtable {
    ALvoid (*const Destruct)(ALeffectState *state);
    ALboolean (*const DeviceUpdate)(ALeffectState *state, ALCdevice *device);
    ALvoid (*const Update)(ALeffectState *state, ALCdevice *device, const ALeffectslot *slot);
    ALvoid (*const Process)(ALeffectState *state, ALuint samplesToDo, const ALfloat *restrict samplesIn, ALfloat (*restrict samplesOut)[BUFFERSIZE]);
    ALeffectStateFactory *(*const getCreator)(void);
};

struct ALeffectState {
    const struct ALeffectStateVtable *vtbl;
};

#define ALeffectState_Destruct(a)       ((a)->vtbl->Destruct((a)))
#define ALeffectState_DeviceUpdate(a,b) ((a)->vtbl->DeviceUpdate((a),(b)))
#define ALeffectState_Update(a,b,c)     ((a)->vtbl->Update((a),(b),(c)))
#define ALeffectState_Process(a,b,c,d)  ((a)->vtbl->Process((a),(b),(c),(d)))
#define ALeffectState_getCreator(a)     ((a)->vtbl->getCreator())

#define DEFINE_ALEFFECTSTATE_VTABLE(T)                                        \
static ALvoid T##_ALeffectState_Destruct(ALeffectState *state)                \
{ T##_Destruct(STATIC_UPCAST(T, ALeffectState, state)); }                     \
static ALboolean T##_ALeffectState_DeviceUpdate(ALeffectState *state, ALCdevice *device) \
{ return T##_DeviceUpdate(STATIC_UPCAST(T, ALeffectState, state), device); }             \
static ALvoid T##_ALeffectState_Update(ALeffectState *state, ALCdevice *device, const ALeffectslot *slot) \
{ T##_Update(STATIC_UPCAST(T, ALeffectState, state), device, slot); }                                     \
static ALvoid T##_ALeffectState_Process(ALeffectState *state, ALuint samplesToDo, const ALfloat *restrict samplesIn, ALfloat (*restrict samplesOut)[BUFFERSIZE]) \
{ T##_Process(STATIC_UPCAST(T, ALeffectState, state), samplesToDo, samplesIn, samplesOut); }                                                                     \
static ALeffectStateFactory* T##_ALeffectState_getCreator(void)               \
{ return T##_getCreator(); }                                                  \
                                                                              \
static const struct ALeffectStateVtable T##_ALeffectState_vtable = {          \
    T##_ALeffectState_Destruct,                                               \
    T##_ALeffectState_DeviceUpdate,                                           \
    T##_ALeffectState_Update,                                                 \
    T##_ALeffectState_Process,                                                \
    T##_ALeffectState_getCreator,                                             \
}


struct ALeffectStateFactoryVtable {
    ALeffectState *(*const create)(void);
    ALvoid (*const destroy)(ALeffectState *state);
};

struct ALeffectStateFactory {
    const struct ALeffectStateFactoryVtable *vtbl;
};

#define ALeffectStateFactory_create(p)    ((p)->vtbl->create())
#define ALeffectStateFactory_destroy(p,a) ((p)->vtbl->destroy((a)))

#define DEFINE_ALEFFECTSTATEFACTORY_VTABLE(T)                                 \
static ALeffectState* T##_ALeffectStateFactory_create(void)                   \
{ return T##_create(); }                                                      \
static ALvoid T##_ALeffectStateFactory_destroy(ALeffectState *state)          \
{ T##_destroy(state); }                                                       \
                                                                              \
static const struct ALeffectStateFactoryVtable T##_ALeffectStateFactory_vtable = { \
    T##_ALeffectStateFactory_create,                                          \
    T##_ALeffectStateFactory_destroy                                          \
}


struct ALeffectslot
{
    ALeffect effect;

    volatile ALfloat   Gain;
    volatile ALboolean AuxSendAuto;

    volatile ALenum NeedsUpdate;
    ALeffectState *EffectState;

    ALIGN(16) ALfloat WetBuffer[1][BUFFERSIZE];

    ALfloat ClickRemoval[1];
    ALfloat PendingClicks[1];

    RefCount ref;

    /* Self ID */
    ALuint id;
};


ALenum InitEffectSlot(ALeffectslot *slot);
ALvoid ReleaseALAuxiliaryEffectSlots(ALCcontext *Context);


ALeffectStateFactory *ALreverbStateFactory_getFactory(void);
ALeffectStateFactory *ALchorusStateFactory_getFactory(void);
ALeffectStateFactory *ALdistortionStateFactory_getFactory(void);
ALeffectStateFactory *ALechoStateFactory_getFactory(void);
ALeffectStateFactory *ALequalizerStateFactory_getFactory(void);
ALeffectStateFactory *ALflangerStateFactory_getFactory(void);
ALeffectStateFactory *ALmodulatorStateFactory_getFactory(void);

ALeffectStateFactory *ALdedicatedStateFactory_getFactory(void);


ALenum InitializeEffect(ALCdevice *Device, ALeffectslot *EffectSlot, ALeffect *effect);

void InitEffectFactoryMap(void);
void DeinitEffectFactoryMap(void);

#ifdef __cplusplus
}
#endif

#endif
