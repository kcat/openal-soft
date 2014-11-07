#ifndef _AL_AUXEFFECTSLOT_H_
#define _AL_AUXEFFECTSLOT_H_

#include "alMain.h"
#include "alEffect.h"

#include "align.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ALeffectStateVtable;
struct ALeffectslot;

typedef struct ALeffectState {
    const struct ALeffectStateVtable *vtbl;
} ALeffectState;

struct ALeffectStateVtable {
    void (*const Destruct)(ALeffectState *state);

    ALboolean (*const deviceUpdate)(ALeffectState *state, ALCdevice *device);
    void (*const update)(ALeffectState *state, ALCdevice *device, const struct ALeffectslot *slot);
    void (*const process)(ALeffectState *state, ALuint samplesToDo, const ALfloat *restrict samplesIn, ALfloat (*restrict samplesOut)[BUFFERSIZE], ALuint numChannels);

    void (*const Delete)(void *ptr);
};

#define DEFINE_ALEFFECTSTATE_VTABLE(T)                                        \
DECLARE_THUNK(T, ALeffectState, void, Destruct)                               \
DECLARE_THUNK1(T, ALeffectState, ALboolean, deviceUpdate, ALCdevice*)         \
DECLARE_THUNK2(T, ALeffectState, void, update, ALCdevice*, const ALeffectslot*) \
DECLARE_THUNK4(T, ALeffectState, void, process, ALuint, const ALfloat*restrict, ALfloatBUFFERSIZE*restrict, ALuint) \
static void T##_ALeffectState_Delete(void *ptr)                               \
{ return T##_Delete(STATIC_UPCAST(T, ALeffectState, (ALeffectState*)ptr)); }  \
                                                                              \
static const struct ALeffectStateVtable T##_ALeffectState_vtable = {          \
    T##_ALeffectState_Destruct,                                               \
                                                                              \
    T##_ALeffectState_deviceUpdate,                                           \
    T##_ALeffectState_update,                                                 \
    T##_ALeffectState_process,                                                \
                                                                              \
    T##_ALeffectState_Delete,                                                 \
}


struct ALeffectStateFactoryVtable;

typedef struct ALeffectStateFactory {
    const struct ALeffectStateFactoryVtable *vtbl;
} ALeffectStateFactory;

struct ALeffectStateFactoryVtable {
    ALeffectState *(*const create)(ALeffectStateFactory *factory);
};

#define DEFINE_ALEFFECTSTATEFACTORY_VTABLE(T)                                 \
DECLARE_THUNK(T, ALeffectStateFactory, ALeffectState*, create)                \
                                                                              \
static const struct ALeffectStateFactoryVtable T##_ALeffectStateFactory_vtable = { \
    T##_ALeffectStateFactory_create,                                          \
}


typedef struct ALeffectslot {
    ALenum EffectType;
    ALeffectProps EffectProps;

    volatile ALfloat   Gain;
    volatile ALboolean AuxSendAuto;

    ATOMIC(ALenum) NeedsUpdate;
    ALeffectState *EffectState;

    alignas(16) ALfloat WetBuffer[1][BUFFERSIZE];

    RefCount ref;

    /* Self ID */
    ALuint id;
} ALeffectslot;

inline struct ALeffectslot *LookupEffectSlot(ALCcontext *context, ALuint id)
{ return (struct ALeffectslot*)LookupUIntMapKey(&context->EffectSlotMap, id); }
inline struct ALeffectslot *RemoveEffectSlot(ALCcontext *context, ALuint id)
{ return (struct ALeffectslot*)RemoveUIntMapKey(&context->EffectSlotMap, id); }

ALenum InitEffectSlot(ALeffectslot *slot);
ALvoid ReleaseALAuxiliaryEffectSlots(ALCcontext *Context);


ALeffectStateFactory *ALnullStateFactory_getFactory(void);
ALeffectStateFactory *ALreverbStateFactory_getFactory(void);
ALeffectStateFactory *ALautowahStateFactory_getFactory(void);
ALeffectStateFactory *ALchorusStateFactory_getFactory(void);
ALeffectStateFactory *ALcompressorStateFactory_getFactory(void);
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
