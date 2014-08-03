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


extern inline struct ALeffectslot *LookupEffectSlot(ALCcontext *context, ALuint id);
extern inline struct ALeffectslot *RemoveEffectSlot(ALCcontext *context, ALuint id);

static ALenum AddEffectSlotArray(ALCcontext *Context, ALeffectslot **start, ALsizei count);
static void RemoveEffectSlotArray(ALCcontext *Context, const ALeffectslot *slot);


static UIntMap EffectStateFactoryMap;
static inline ALeffectStateFactory *getFactoryByType(ALenum type)
{
    ALeffectStateFactory* (*getFactory)(void) = LookupUIntMapKey(&EffectStateFactoryMap, type);
    if(getFactory != NULL)
        return getFactory();
    return NULL;
}


AL_API ALvoid AL_APIENTRY alGenAuxiliaryEffectSlots(ALsizei n, ALuint *effectslots)
{
    ALCcontext *context;
    VECTOR(ALeffectslot*) slotvec;
    ALsizei cur;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    VECTOR_INIT(slotvec);

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(!VECTOR_RESERVE(slotvec, n))
        SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, done);

    for(cur = 0;cur < n;cur++)
    {
        ALeffectslot *slot = al_calloc(16, sizeof(ALeffectslot));
        err = AL_OUT_OF_MEMORY;
        if(!slot || (err=InitEffectSlot(slot)) != AL_NO_ERROR)
        {
            al_free(slot);
            alDeleteAuxiliaryEffectSlots(cur, effectslots);
            SET_ERROR_AND_GOTO(context, err, done);
        }

        err = NewThunkEntry(&slot->id);
        if(err == AL_NO_ERROR)
            err = InsertUIntMapEntry(&context->EffectSlotMap, slot->id, slot);
        if(err != AL_NO_ERROR)
        {
            FreeThunkEntry(slot->id);
            DELETE_OBJ(slot->EffectState);
            al_free(slot);

            alDeleteAuxiliaryEffectSlots(cur, effectslots);
            SET_ERROR_AND_GOTO(context, err, done);
        }

        VECTOR_PUSH_BACK(slotvec, slot);

        effectslots[cur] = slot->id;
    }
    err = AddEffectSlotArray(context, VECTOR_ITER_BEGIN(slotvec), n);
    if(err != AL_NO_ERROR)
    {
        alDeleteAuxiliaryEffectSlots(cur, effectslots);
        SET_ERROR_AND_GOTO(context, err, done);
    }

done:
    VECTOR_DEINIT(slotvec);

    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDeleteAuxiliaryEffectSlots(ALsizei n, const ALuint *effectslots)
{
    ALCcontext *context;
    ALeffectslot *slot;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    for(i = 0;i < n;i++)
    {
        if((slot=LookupEffectSlot(context, effectslots[i])) == NULL)
            SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
        if(ReadRef(&slot->ref) != 0)
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    // All effectslots are valid
    for(i = 0;i < n;i++)
    {
        if((slot=RemoveEffectSlot(context, effectslots[i])) == NULL)
            continue;
        FreeThunkEntry(slot->id);

        RemoveEffectSlotArray(context, slot);
        DELETE_OBJ(slot->EffectState);

        memset(slot, 0, sizeof(*slot));
        al_free(slot);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsAuxiliaryEffectSlot(ALuint effectslot)
{
    ALCcontext *context;
    ALboolean  ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    ret = (LookupEffectSlot(context, effectslot) ? AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(context);

    return ret;
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALeffectslot *slot;
    ALeffect *effect = NULL;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((slot=LookupEffectSlot(context, effectslot)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
        effect = (value ? LookupEffect(device, value) : NULL);
        if(!(value == 0 || effect != NULL))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

        err = InitializeEffect(device, slot, effect);
        if(err != AL_NO_ERROR)
            SET_ERROR_AND_GOTO(context, err, done);
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);
        break;

    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        if(!(value == AL_TRUE || value == AL_FALSE))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

        slot->AuxSendAuto = value;
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, const ALint *values)
{
    ALCcontext *context;

    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        alAuxiliaryEffectSloti(effectslot, param, values[0]);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    if(LookupEffectSlot(context, effectslot) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat value)
{
    ALCcontext *context;
    ALeffectslot *slot;

    context = GetContextRef();
    if(!context) return;

    if((slot=LookupEffectSlot(context, effectslot)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        if(!(value >= 0.0f && value <= 1.0f))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

        slot->Gain = value;
        ATOMIC_STORE(&slot->NeedsUpdate, AL_TRUE);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, const ALfloat *values)
{
    ALCcontext *context;

    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        alAuxiliaryEffectSlotf(effectslot, param, values[0]);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    if(LookupEffectSlot(context, effectslot) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint *value)
{
    ALCcontext *context;
    ALeffectslot *slot;

    context = GetContextRef();
    if(!context) return;

    if((slot=LookupEffectSlot(context, effectslot)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        *value = slot->AuxSendAuto;
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, ALint *values)
{
    ALCcontext *context;

    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        alGetAuxiliaryEffectSloti(effectslot, param, values);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    if(LookupEffectSlot(context, effectslot) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat *value)
{
    ALCcontext *context;
    ALeffectslot *slot;

    context = GetContextRef();
    if(!context) return;

    if((slot=LookupEffectSlot(context, effectslot)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        *value = slot->Gain;
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, ALfloat *values)
{
    ALCcontext *context;

    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        alGetAuxiliaryEffectSlotf(effectslot, param, values);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    if(LookupEffectSlot(context, effectslot) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


static ALenum AddEffectSlotArray(ALCcontext *context, ALeffectslot **start, ALsizei count)
{
    ALenum err = AL_NO_ERROR;

    LockContext(context);
    if(!VECTOR_INSERT(context->ActiveAuxSlots, VECTOR_ITER_END(context->ActiveAuxSlots), start, start+count))
        err = AL_OUT_OF_MEMORY;
    UnlockContext(context);

    return err;
}

static void RemoveEffectSlotArray(ALCcontext *context, const ALeffectslot *slot)
{
    ALeffectslot **iter;

    LockContext(context);
#define MATCH_SLOT(_i)  (slot == *(_i))
    VECTOR_FIND_IF(iter, ALeffectslot*, context->ActiveAuxSlots, MATCH_SLOT);
    if(iter != VECTOR_ITER_END(context->ActiveAuxSlots))
    {
        *iter = VECTOR_BACK(context->ActiveAuxSlots);
        VECTOR_POP_BACK(context->ActiveAuxSlots);
    }
#undef MATCH_SLOT
    UnlockContext(context);
}


void InitEffectFactoryMap(void)
{
    InitUIntMap(&EffectStateFactoryMap, ~0);

    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_NULL, ALnullStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_EAXREVERB, ALreverbStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_REVERB, ALreverbStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_AUTOWAH, ALautowahStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_CHORUS, ALchorusStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_COMPRESSOR, ALcompressorStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_DISTORTION, ALdistortionStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_ECHO, ALechoStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_EQUALIZER, ALequalizerStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_FLANGER, ALflangerStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_RING_MODULATOR, ALmodulatorStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_DEDICATED_DIALOGUE, ALdedicatedStateFactory_getFactory);
    InsertUIntMapEntry(&EffectStateFactoryMap, AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT, ALdedicatedStateFactory_getFactory);
}

void DeinitEffectFactoryMap(void)
{
    ResetUIntMap(&EffectStateFactoryMap);
}


ALenum InitializeEffect(ALCdevice *Device, ALeffectslot *EffectSlot, ALeffect *effect)
{
    ALenum newtype = (effect ? effect->type : AL_EFFECT_NULL);
    ALeffectStateFactory *factory;

    if(newtype != EffectSlot->EffectType)
    {
        ALeffectState *State;
        FPUCtl oldMode;

        factory = getFactoryByType(newtype);
        if(!factory)
        {
            ERR("Failed to find factory for effect type 0x%04x\n", newtype);
            return AL_INVALID_ENUM;
        }
        State = V0(factory,create)();
        if(!State)
            return AL_OUT_OF_MEMORY;

        SetMixerFPUMode(&oldMode);

        ALCdevice_Lock(Device);
        if(V(State,deviceUpdate)(Device) == AL_FALSE)
        {
            ALCdevice_Unlock(Device);
            RestoreFPUMode(&oldMode);
            DELETE_OBJ(State);
            return AL_OUT_OF_MEMORY;
        }

        State = ExchangePtr((XchgPtr*)&EffectSlot->EffectState, State);
        if(!effect)
        {
            memset(&EffectSlot->EffectProps, 0, sizeof(EffectSlot->EffectProps));
            EffectSlot->EffectType = AL_EFFECT_NULL;
        }
        else
        {
            memcpy(&EffectSlot->EffectProps, &effect->Props, sizeof(effect->Props));
            EffectSlot->EffectType = effect->type;
        }

        /* FIXME: This should be done asynchronously, but since the EffectState
         * object was changed, it needs an update before its Process method can
         * be called. */
        ATOMIC_STORE(&EffectSlot->NeedsUpdate, AL_FALSE);
        V(EffectSlot->EffectState,update)(Device, EffectSlot);
        ALCdevice_Unlock(Device);

        RestoreFPUMode(&oldMode);

        DELETE_OBJ(State);
        State = NULL;
    }
    else
    {
        if(effect)
        {
            ALCdevice_Lock(Device);
            memcpy(&EffectSlot->EffectProps, &effect->Props, sizeof(effect->Props));
            ALCdevice_Unlock(Device);
            ATOMIC_STORE(&EffectSlot->NeedsUpdate, AL_TRUE);
        }
    }

    return AL_NO_ERROR;
}


ALenum InitEffectSlot(ALeffectslot *slot)
{
    ALeffectStateFactory *factory;
    ALuint i, c;

    slot->EffectType = AL_EFFECT_NULL;

    factory = getFactoryByType(AL_EFFECT_NULL);
    if(!(slot->EffectState=V0(factory,create)()))
        return AL_OUT_OF_MEMORY;

    slot->Gain = 1.0;
    slot->AuxSendAuto = AL_TRUE;
    ATOMIC_INIT(&slot->NeedsUpdate, AL_FALSE);
    for(c = 0;c < 1;c++)
    {
        for(i = 0;i < BUFFERSIZE;i++)
            slot->WetBuffer[c][i] = 0.0f;
    }
    InitRef(&slot->ref, 0);

    return AL_NO_ERROR;
}

ALvoid ReleaseALAuxiliaryEffectSlots(ALCcontext *Context)
{
    ALsizei pos;
    for(pos = 0;pos < Context->EffectSlotMap.size;pos++)
    {
        ALeffectslot *temp = Context->EffectSlotMap.array[pos].value;
        Context->EffectSlotMap.array[pos].value = NULL;

        DELETE_OBJ(temp->EffectState);

        FreeThunkEntry(temp->id);
        memset(temp, 0, sizeof(ALeffectslot));
        al_free(temp);
    }
}
