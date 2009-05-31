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


static ALvoid InitializeEffect(ALCcontext *Context, ALeffectslot *ALEffectSlot, ALeffect *effect);


ALvoid AL_APIENTRY alGenAuxiliaryEffectSlots(ALsizei n, ALuint *effectslots)
{
    ALCcontext *Context;
    ALsizei i, j;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (n > 0)
    {
        if(Context->AuxiliaryEffectSlotCount+n <= Context->AuxiliaryEffectSlotMax)
        {
            // Check that enough memory has been allocted in the 'effectslots' array for n Effect Slots
            if (!IsBadWritePtr((void*)effectslots, n * sizeof(ALuint)))
            {
                ALeffectslot **list = &Context->AuxiliaryEffectSlot;
                while(*list)
                    list = &(*list)->next;

                i = 0;
                while(i < n)
                {
                    *list = calloc(1, sizeof(ALeffectslot));
                    if(!(*list))
                    {
                        // We must have run out or memory
                        alDeleteAuxiliaryEffectSlots(i, effectslots);
                        alSetError(AL_OUT_OF_MEMORY);
                        break;
                    }

                    (*list)->Gain = 1.0;
                    (*list)->AuxSendAuto = AL_TRUE;
                    for(j = 0;j < BUFFERSIZE;j++)
                        (*list)->WetBuffer[j] = 0.0f;
                    (*list)->refcount = 0;

                    effectslots[i] = (ALuint)ALTHUNK_ADDENTRY(*list);
                    (*list)->effectslot = effectslots[i];

                    Context->AuxiliaryEffectSlotCount++;
                    i++;

                    list = &(*list)->next;
                }
            }
        }
        else
            alSetError(AL_INVALID_OPERATION);
    }

    ProcessContext(Context);
}

ALvoid AL_APIENTRY alDeleteAuxiliaryEffectSlots(ALsizei n, ALuint *effectslots)
{
    ALCcontext *Context;
    ALeffectslot *ALAuxiliaryEffectSlot;
    ALsizei i;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (n >= 0)
    {
        // Check that all effectslots are valid
        for (i = 0; i < n; i++)
        {
            if (!alIsAuxiliaryEffectSlot(effectslots[i]))
            {
                alSetError(AL_INVALID_NAME);
                break;
            }
            else
            {
                ALAuxiliaryEffectSlot = (ALeffectslot*)ALTHUNK_LOOKUPENTRY(effectslots[i]);
                if(ALAuxiliaryEffectSlot->refcount > 0)
                {
                    alSetError(AL_INVALID_NAME);
                    break;
                }
            }
        }

        if (i == n)
        {
            // All effectslots are valid
            for (i = 0; i < n; i++)
            {
                // Recheck that the effectslot is valid, because there could be duplicated names
                if (alIsAuxiliaryEffectSlot(effectslots[i]))
                {
                    ALeffectslot **list;

                    ALAuxiliaryEffectSlot = ((ALeffectslot*)ALTHUNK_LOOKUPENTRY(effectslots[i]));

                    // Remove Source from list of Sources
                    list = &Context->AuxiliaryEffectSlot;
                    while(*list && *list != ALAuxiliaryEffectSlot)
                         list = &(*list)->next;

                    if(*list)
                        *list = (*list)->next;
                    ALTHUNK_REMOVEENTRY(ALAuxiliaryEffectSlot->effectslot);

                    if(ALAuxiliaryEffectSlot->EffectState)
                        ALEffect_Destroy(ALAuxiliaryEffectSlot->EffectState);

                    memset(ALAuxiliaryEffectSlot, 0, sizeof(ALeffectslot));
                    free(ALAuxiliaryEffectSlot);

                    Context->AuxiliaryEffectSlotCount--;
                }
            }
        }
    }
    else
        alSetError(AL_INVALID_VALUE);

    ProcessContext(Context);
}

ALboolean AL_APIENTRY alIsAuxiliaryEffectSlot(ALuint effectslot)
{
    ALCcontext *Context;
    ALeffectslot **list;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return AL_FALSE;
    }
    SuspendContext(Context);

    list = &Context->AuxiliaryEffectSlot;
    while(*list && (*list)->effectslot != effectslot)
        list = &(*list)->next;

    ProcessContext(Context);

    return (*list ? AL_TRUE : AL_FALSE);
}

ALvoid AL_APIENTRY alAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint iValue)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (alIsAuxiliaryEffectSlot(effectslot))
    {
        ALeffectslot *ALEffectSlot = (ALeffectslot*)ALTHUNK_LOOKUPENTRY(effectslot);

        switch(param)
        {
        case AL_EFFECTSLOT_EFFECT:
            if(alIsEffect(iValue))
            {
                ALeffect *effect = (ALeffect*)ALTHUNK_LOOKUPENTRY(iValue);
                InitializeEffect(Context, ALEffectSlot, effect);
            }
            else
                alSetError(AL_INVALID_VALUE);
            break;

        case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
            if(iValue == AL_TRUE || iValue == AL_FALSE)
                ALEffectSlot->AuxSendAuto = iValue;
            else
                alSetError(AL_INVALID_VALUE);
            break;

        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

ALvoid AL_APIENTRY alAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, ALint *piValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (alIsAuxiliaryEffectSlot(effectslot))
    {
        switch(param)
        {
        case AL_EFFECTSLOT_EFFECT:
        case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
            alAuxiliaryEffectSloti(effectslot, param, piValues[0]);
            break;

        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

ALvoid AL_APIENTRY alAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat flValue)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (alIsAuxiliaryEffectSlot(effectslot))
    {
        ALeffectslot *ALEffectSlot = (ALeffectslot*)ALTHUNK_LOOKUPENTRY(effectslot);

        switch(param)
        {
        case AL_EFFECTSLOT_GAIN:
            if(flValue >= 0.0f && flValue <= 1.0f)
                ALEffectSlot->Gain = flValue;
            else
                alSetError(AL_INVALID_VALUE);
            break;

        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

ALvoid AL_APIENTRY alAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, ALfloat *pflValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (alIsAuxiliaryEffectSlot(effectslot))
    {
        switch(param)
        {
        case AL_EFFECTSLOT_GAIN:
            alAuxiliaryEffectSlotf(effectslot, param, pflValues[0]);
            break;

        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

ALvoid AL_APIENTRY alGetAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint *piValue)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (alIsAuxiliaryEffectSlot(effectslot))
    {
        ALeffectslot *ALEffectSlot = (ALeffectslot*)ALTHUNK_LOOKUPENTRY(effectslot);

        switch(param)
        {
        case AL_EFFECTSLOT_EFFECT:
            *piValue = ALEffectSlot->effect.effect;
            break;

        case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
            *piValue = ALEffectSlot->AuxSendAuto;
            break;

        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, ALint *piValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (alIsAuxiliaryEffectSlot(effectslot))
    {
        switch(param)
        {
        case AL_EFFECTSLOT_EFFECT:
        case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
            alGetAuxiliaryEffectSloti(effectslot, param, piValues);
            break;

        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat *pflValue)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (alIsAuxiliaryEffectSlot(effectslot))
    {
        ALeffectslot *ALEffectSlot = (ALeffectslot*)ALTHUNK_LOOKUPENTRY(effectslot);

        switch(param)
        {
        case AL_EFFECTSLOT_GAIN:
            *pflValue = ALEffectSlot->Gain;
            break;

        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, ALfloat *pflValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    if(!Context)
    {
        alSetError(AL_INVALID_OPERATION);
        return;
    }
    SuspendContext(Context);

    if (alIsAuxiliaryEffectSlot(effectslot))
    {
        switch(param)
        {
        case AL_EFFECTSLOT_GAIN:
            alGetAuxiliaryEffectSlotf(effectslot, param, pflValues);
            break;

        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}


static ALvoid InitializeEffect(ALCcontext *Context, ALeffectslot *ALEffectSlot, ALeffect *effect)
{
    if((!effect) || (effect->type != ALEffectSlot->effect.type))
    {
        ALeffectState *NewState = NULL;
        if(effect)
        {
            if(effect->type == AL_EFFECT_EAXREVERB)
                NewState = EAXVerbCreate(Context);
            else if(effect->type == AL_EFFECT_REVERB)
                NewState = VerbCreate(Context);
            else if(effect->type == AL_EFFECT_ECHO)
                NewState = EchoCreate(Context);
            /* No new state? An error occured.. */
            if(!NewState)
                return;
        }
        if(ALEffectSlot->EffectState)
            ALEffect_Destroy(ALEffectSlot->EffectState);
        ALEffectSlot->EffectState = NewState;
    }
    if(!effect)
    {
        memset(&ALEffectSlot->effect, 0, sizeof(ALEffectSlot->effect));
        return;
    }
    memcpy(&ALEffectSlot->effect, effect, sizeof(*effect));
    ALEffect_Update(ALEffectSlot->EffectState, Context, effect);
}


ALvoid ReleaseALAuxiliaryEffectSlots(ALCcontext *Context)
{
#ifdef _DEBUG
    if(Context->AuxiliaryEffectSlotCount > 0)
        AL_PRINT("alcDestroyContext(): deleting %d AuxiliaryEffectSlot(s)\n", Context->AuxiliaryEffectSlotCount);
#endif

    while(Context->AuxiliaryEffectSlot)
    {
        ALeffectslot *temp = Context->AuxiliaryEffectSlot;
        Context->AuxiliaryEffectSlot = Context->AuxiliaryEffectSlot->next;

        // Release effectslot structure
        if(temp->EffectState)
            ALEffect_Destroy(temp->EffectState);
        ALTHUNK_REMOVEENTRY(temp->effectslot);

        memset(temp, 0, sizeof(ALeffectslot));
        free(temp);
    }
    Context->AuxiliaryEffectSlotCount = 0;
}
