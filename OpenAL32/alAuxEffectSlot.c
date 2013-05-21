/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <math.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alThunk.h"
#include "alError.h"
#include "alSource.h"


static ALenum AddEffectSlotArray(ALCcontext *Context, ALsizei count, const ALuint *slots);
static ALvoid RemoveEffectSlotArray(ALCcontext *Context, ALeffectslot *slot);


static UIntMap EffectStateFactoryMap;
static __inline ALeffectStateFactory *getFactoryByType(ALenum type)
{
    return LookupUIntMapKey(&EffectStateFactoryMap, type);
}


AL_API ALvoid AL_APIENTRY alGenAuxiliaryEffectSlots(ALsizei n, ALuint *effectslots)
{
    ALCcontext *Context;
    ALsizei    cur = 0;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        ALenum err;

        CHECK_VALUE(Context, n >= 0);
        for(cur = 0;cur < n;cur++)
        {
            ALeffectslot *slot = al_calloc(16, sizeof(ALeffectslot));
            err = AL_OUT_OF_MEMORY;
            if(!slot || (err=InitEffectSlot(slot)) != AL_NO_ERROR)
            {
                al_free(slot);
                alDeleteAuxiliaryEffectSlots(cur, effectslots);
                al_throwerr(Context, err);
                break;
            }

            err = NewThunkEntry(&slot->id);
            if(err == AL_NO_ERROR)
                err = InsertUIntMapEntry(&Context->EffectSlotMap, slot->id, slot);
            if(err != AL_NO_ERROR)
            {
                FreeThunkEntry(slot->id);
                ALeffectStateFactory_destroy(ALeffectState_getCreator(slot->EffectState),
                                             slot->EffectState);
                al_free(slot);

                alDeleteAuxiliaryEffectSlots(cur, effectslots);
                al_throwerr(Context, err);
            }

            effectslots[cur] = slot->id;
        }
        err = AddEffectSlotArray(Context, n, effectslots);
        if(err != AL_NO_ERROR)
        {
            alDeleteAuxiliaryEffectSlots(cur, effectslots);
            al_throwerr(Context, err);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alDeleteAuxiliaryEffectSlots(ALsizei n, const ALuint *effectslots)
{
    ALCcontext *Context;
    ALeffectslot *slot;
    ALsizei i;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        CHECK_VALUE(Context, n >= 0);
        for(i = 0;i < n;i++)
        {
            if((slot=LookupEffectSlot(Context, effectslots[i])) == NULL)
                al_throwerr(Context, AL_INVALID_NAME);
            if(slot->ref != 0)
                al_throwerr(Context, AL_INVALID_OPERATION);
        }

        // All effectslots are valid
        for(i = 0;i < n;i++)
        {
            if((slot=RemoveEffectSlot(Context, effectslots[i])) == NULL)
                continue;
            FreeThunkEntry(slot->id);

            RemoveEffectSlotArray(Context, slot);
            ALeffectStateFactory_destroy(ALeffectState_getCreator(slot->EffectState),
                                         slot->EffectState);

            memset(slot, 0, sizeof(*slot));
            al_free(slot);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALboolean AL_APIENTRY alIsAuxiliaryEffectSlot(ALuint effectslot)
{
    ALCcontext *Context;
    ALboolean  result;

    Context = GetContextRef();
    if(!Context) return AL_FALSE;

    result = (LookupEffectSlot(Context, effectslot) ? AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(Context);

    return result;
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint value)
{
    ALCcontext *Context;
    ALeffectslot *Slot;
    ALeffect *effect = NULL;
    ALenum err;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        ALCdevice *device = Context->Device;
        if((Slot=LookupEffectSlot(Context, effectslot)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
        case AL_EFFECTSLOT_EFFECT:
            CHECK_VALUE(Context, value == 0 || (effect=LookupEffect(device, value)) != NULL);

            err = InitializeEffect(device, Slot, effect);
            if(err != AL_NO_ERROR)
                al_throwerr(Context, err);
            Context->UpdateSources = AL_TRUE;
            break;

        case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
            CHECK_VALUE(Context, value == AL_TRUE || value == AL_FALSE);

            Slot->AuxSendAuto = value;
            Context->UpdateSources = AL_TRUE;
            break;

        default:
            al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, const ALint *values)
{
    ALCcontext *Context;

    switch(param)
    {
        case AL_EFFECTSLOT_EFFECT:
        case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
            alAuxiliaryEffectSloti(effectslot, param, values[0]);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if(LookupEffectSlot(Context, effectslot) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
        default:
            al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat value)
{
    ALCcontext *Context;
    ALeffectslot *Slot;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Slot=LookupEffectSlot(Context, effectslot)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
        case AL_EFFECTSLOT_GAIN:
            CHECK_VALUE(Context, value >= 0.0f && value <= 1.0f);

            Slot->Gain = value;
            Slot->NeedsUpdate = AL_TRUE;
            break;

        default:
            al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, const ALfloat *values)
{
    ALCcontext *Context;

    switch(param)
    {
        case AL_EFFECTSLOT_GAIN:
            alAuxiliaryEffectSlotf(effectslot, param, values[0]);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if(LookupEffectSlot(Context, effectslot) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
        default:
            al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint *value)
{
    ALCcontext *Context;
    ALeffectslot *Slot;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Slot=LookupEffectSlot(Context, effectslot)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
        case AL_EFFECTSLOT_EFFECT:
            *value = Slot->effect.id;
            break;

        case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
            *value = Slot->AuxSendAuto;
            break;

        default:
            al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, ALint *values)
{
    ALCcontext *Context;

    switch(param)
    {
        case AL_EFFECTSLOT_EFFECT:
        case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
            alGetAuxiliaryEffectSloti(effectslot, param, values);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if(LookupEffectSlot(Context, effectslot) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
        default:
            al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat *value)
{
    ALCcontext *Context;
    ALeffectslot *Slot;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Slot=LookupEffectSlot(Context, effectslot)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
        case AL_EFFECTSLOT_GAIN:
            *value = Slot->Gain;
            break;

        default:
            al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, ALfloat *values)
{
    ALCcontext *Context;

    switch(param)
    {
        case AL_EFFECTSLOT_GAIN:
            alGetAuxiliaryEffectSlotf(effectslot, param, values);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if(LookupEffectSlot(Context, effectslot) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
        default:
            al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


typedef struct ALnoneStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALnoneStateFactory;

static ALnoneStateFactory NoneFactory;


typedef struct ALnoneState {
    DERIVE_FROM_TYPE(ALeffectState);
} ALnoneState;

static ALvoid ALnoneState_Destruct(ALnoneState *state)
{
    (void)state;
}
static ALboolean ALnoneState_DeviceUpdate(ALnoneState *state, ALCdevice *device)
{
    return AL_TRUE;
    (void)state;
    (void)device;
}
static ALvoid ALnoneState_Update(ALnoneState *state, ALCdevice *device, const ALeffectslot *slot)
{
    (void)state;
    (void)device;
    (void)slot;
}
static ALvoid ALnoneState_Process(ALnoneState *state, ALuint samplesToDo, const ALfloat *RESTRICT samplesIn, ALfloat (*RESTRICT samplesOut)[BUFFERSIZE])
{
    (void)state;
    (void)samplesToDo;
    (void)samplesIn;
    (void)samplesOut;
}
static ALeffectStateFactory *ALnoneState_getCreator(void)
{
    return STATIC_CAST(ALeffectStateFactory, &NoneFactory);
}

DEFINE_ALEFFECTSTATE_VTABLE(ALnoneState);


ALeffectState *ALnoneStateFactory_create(void)
{
    ALnoneState *state;

    state = calloc(1, sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALnoneState, ALeffectState, state);

    return STATIC_CAST(ALeffectState, state);
}

static ALvoid ALnoneStateFactory_destroy(ALeffectState *effect)
{
    ALnoneState *state = STATIC_UPCAST(ALnoneState, ALeffectState, effect);
    ALnoneState_Destruct(state);
    free(state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALnoneStateFactory);


static void init_none_factory(void)
{
    SET_VTABLE2(ALnoneStateFactory, ALeffectStateFactory, &NoneFactory);
}

ALeffectStateFactory *ALnoneStateFactory_getFactory(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_none_factory);
    return STATIC_CAST(ALeffectStateFactory, &NoneFactory);
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


static ALvoid RemoveEffectSlotArray(ALCcontext *Context, ALeffectslot *slot)
{
    ALeffectslot **slotlist, **slotlistend;

    LockContext(Context);
    slotlist = Context->ActiveEffectSlots;
    slotlistend = slotlist + Context->ActiveEffectSlotCount;
    while(slotlist != slotlistend)
    {
        if(*slotlist == slot)
        {
            *slotlist = *(--slotlistend);
            Context->ActiveEffectSlotCount--;
            break;
        }
        slotlist++;
    }
    UnlockContext(Context);
}

static ALenum AddEffectSlotArray(ALCcontext *Context, ALsizei count, const ALuint *slots)
{
    ALsizei i;

    LockContext(Context);
    if(count > Context->MaxActiveEffectSlots-Context->ActiveEffectSlotCount)
    {
        ALsizei newcount;
        void *temp = NULL;

        newcount = Context->MaxActiveEffectSlots ? (Context->MaxActiveEffectSlots<<1) : 1;
        if(newcount > Context->MaxActiveEffectSlots)
            temp = realloc(Context->ActiveEffectSlots,
                           newcount * sizeof(*Context->ActiveEffectSlots));
        if(!temp)
        {
            UnlockContext(Context);
            return AL_OUT_OF_MEMORY;
        }
        Context->ActiveEffectSlots = temp;
        Context->MaxActiveEffectSlots = newcount;
    }
    for(i = 0;i < count;i++)
    {
        ALeffectslot *slot = LookupEffectSlot(Context, slots[i]);
        assert(slot != NULL);
        Context->ActiveEffectSlots[Context->ActiveEffectSlotCount++] = slot;
    }
    UnlockContext(Context);
    return AL_NO_ERROR;
}


void InitEffectFactoryMap(void)
{
    InitUIntMap(&EffectStateFactoryMap, ~0);

    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_NULL, ALnoneStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_EAXREVERB, ALreverbStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_REVERB, ALreverbStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_CHORUS, ALchorusStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_DISTORTION, ALdistortionStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_ECHO, ALechoStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_EQUALIZER, ALequalizerStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_FLANGER, ALflangerStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_RING_MODULATOR, ALmodulatorStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_DEDICATED_DIALOGUE, ALdedicatedStateFactory_getFactory());
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT, ALdedicatedStateFactory_getFactory());
}

void DeinitEffectFactoryMap(void)
{
    ResetUIntMap(&EffectStateFactoryMap);
}


ALenum InitializeEffect(ALCdevice *Device, ALeffectslot *EffectSlot, ALeffect *effect)
{
    ALenum newtype = (effect ? effect->type : AL_EFFECT_NULL);
    ALeffectStateFactory *factory;

    if(newtype != EffectSlot->effect.type)
    {
        ALeffectState *State;
        FPUCtl oldMode;

        factory = getFactoryByType(newtype);
        if(!factory)
        {
            ERR("Failed to find factory for effect type 0x%04x\n", newtype);
            return AL_INVALID_ENUM;
        }
        State = ALeffectStateFactory_create(factory);
        if(!State)
            return AL_OUT_OF_MEMORY;

        SetMixerFPUMode(&oldMode);

        ALCdevice_Lock(Device);
        if(ALeffectState_DeviceUpdate(State, Device) == AL_FALSE)
        {
            ALCdevice_Unlock(Device);
            RestoreFPUMode(&oldMode);
            ALeffectStateFactory_destroy(ALeffectState_getCreator(State), State);
            return AL_OUT_OF_MEMORY;
        }

        State = ExchangePtr((XchgPtr*)&EffectSlot->EffectState, State);
        if(!effect)
            memset(&EffectSlot->effect, 0, sizeof(EffectSlot->effect));
        else
            memcpy(&EffectSlot->effect, effect, sizeof(*effect));

        /* FIXME: This should be done asynchronously, but since the EffectState
         * object was changed, it needs an update before its Process method can
         * be called. */
        EffectSlot->NeedsUpdate = AL_FALSE;
        ALeffectState_Update(EffectSlot->EffectState, Device, EffectSlot);
        ALCdevice_Unlock(Device);

        RestoreFPUMode(&oldMode);

        ALeffectStateFactory_destroy(ALeffectState_getCreator(State), State);
        State = NULL;
    }
    else
    {
        ALCdevice_Lock(Device);
        if(!effect)
            memset(&EffectSlot->effect, 0, sizeof(EffectSlot->effect));
        else
            memcpy(&EffectSlot->effect, effect, sizeof(*effect));
        ALCdevice_Unlock(Device);
        EffectSlot->NeedsUpdate = AL_TRUE;
    }

    return AL_NO_ERROR;
}


ALenum InitEffectSlot(ALeffectslot *slot)
{
    ALeffectStateFactory *factory;
    ALint i, c;

    factory = getFactoryByType(AL_EFFECT_NULL);
    if(!(slot->EffectState=ALeffectStateFactory_create(factory)))
        return AL_OUT_OF_MEMORY;

    slot->Gain = 1.0;
    slot->AuxSendAuto = AL_TRUE;
    slot->NeedsUpdate = AL_FALSE;
    for(c = 0;c < 1;c++)
    {
        for(i = 0;i < BUFFERSIZE;i++)
            slot->WetBuffer[c][i] = 0.0f;
        slot->ClickRemoval[c] = 0.0f;
        slot->PendingClicks[c] = 0.0f;
    }
    slot->ref = 0;

    return AL_NO_ERROR;
}

ALvoid ReleaseALAuxiliaryEffectSlots(ALCcontext *Context)
{
    ALsizei pos;
    for(pos = 0;pos < Context->EffectSlotMap.size;pos++)
    {
        ALeffectslot *temp = Context->EffectSlotMap.array[pos].value;
        Context->EffectSlotMap.array[pos].value = NULL;

        ALeffectStateFactory_destroy(ALeffectState_getCreator(temp->EffectState),
                                     temp->EffectState);

        FreeThunkEntry(temp->id);
        memset(temp, 0, sizeof(ALeffectslot));
        al_free(temp);
    }
}
