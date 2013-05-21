#ifndef _AL_AUXEFFECTSLOT_H_
#define _AL_AUXEFFECTSLOT_H_

#include "alMain.h"
#include "alEffect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALeffectState ALeffectState;
typedef struct ALeffectslot ALeffectslot;

struct ALeffectStateVtable {
    ALvoid (*const Destroy)(ALeffectState *State);
    ALboolean (*const DeviceUpdate)(ALeffectState *State, ALCdevice *Device);
    ALvoid (*const Update)(ALeffectState *State, ALCdevice *Device, const ALeffectslot *Slot);
    ALvoid (*const Process)(ALeffectState *State, ALuint SamplesToDo, const ALfloat *RESTRICT SamplesIn, ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE]);
};

struct ALeffectState {
    const struct ALeffectStateVtable *vtbl;
};

#define DEFINE_ALEFFECTSTATE_VTABLE(T)                                        \
static const struct ALeffectStateVtable T##_ALeffectState_vtable = {          \
    T##_Destroy,                                                              \
    T##_DeviceUpdate,                                                         \
    T##_Update,                                                               \
    T##_Process                                                               \
}

#define SET_VTABLE1(T1, obj)  ((obj)->vtbl = &(T1##_vtable))
#define SET_VTABLE2(T1, T2, obj) do {                                         \
    STATIC_CAST(T2, (obj))->vtbl = &(T1##_##T2##_vtable);                     \
    /*SET_VTABLE1(T1, obj);*/                                                 \
} while(0)


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

ALeffectState *NoneCreate(void);
ALeffectState *ReverbCreate(void);
ALeffectState *EchoCreate(void);
ALeffectState *ModulatorCreate(void);
ALeffectState *DedicatedCreate(void);
ALeffectState *ChorusCreate(void);
ALeffectState *FlangerCreate(void);
ALeffectState *EqualizerCreate(void);
ALeffectState *DistortionCreate(void);

#define ALeffectState_Destroy(a)        ((a)->vtbl->Destroy((a)))
#define ALeffectState_DeviceUpdate(a,b) ((a)->vtbl->DeviceUpdate((a),(b)))
#define ALeffectState_Update(a,b,c)     ((a)->vtbl->Update((a),(b),(c)))
#define ALeffectState_Process(a,b,c,d)  ((a)->vtbl->Process((a),(b),(c),(d)))

ALenum InitializeEffect(ALCdevice *Device, ALeffectslot *EffectSlot, ALeffect *effect);

#ifdef __cplusplus
}
#endif

#endif
