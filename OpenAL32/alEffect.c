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

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alEffect.h"
#include "alThunk.h"
#include "alError.h"

static ALeffect *g_EffectList;
static ALuint    g_EffectCount;

static void InitEffectParams(ALeffect *effect, ALenum type);


AL_API ALvoid AL_APIENTRY alGenEffects(ALsizei n, ALuint *effects)
{
    ALCcontext *Context;
    ALsizei i;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (n > 0)
    {
        // Check that enough memory has been allocted in the 'effects' array for n Effects
        if (!IsBadWritePtr((void*)effects, n * sizeof(ALuint)))
        {
            ALeffect **list = &g_EffectList;
            while(*list)
                list = &(*list)->next;

            i = 0;
            while(i < n)
            {
                *list = calloc(1, sizeof(ALeffect));
                if(!(*list))
                {
                    // We must have run out or memory
                    alDeleteEffects(i, effects);
                    alSetError(AL_OUT_OF_MEMORY);
                    break;
                }

                effects[i] = (ALuint)ALTHUNK_ADDENTRY(*list);
                (*list)->effect = effects[i];

                InitEffectParams(*list, AL_EFFECT_NULL);
                g_EffectCount++;
                i++;

                list = &(*list)->next;
            }
        }
    }

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alDeleteEffects(ALsizei n, ALuint *effects)
{
    ALCcontext *Context;
    ALeffect *ALEffect;
    ALsizei i;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (n >= 0)
    {
        // Check that all effects are valid
        for (i = 0; i < n; i++)
        {
            if (!alIsEffect(effects[i]))
            {
                alSetError(AL_INVALID_NAME);
                break;
            }
        }

        if (i == n)
        {
            // All effects are valid
            for (i = 0; i < n; i++)
            {
                // Recheck that the effect is valid, because there could be duplicated names
                if (effects[i] && alIsEffect(effects[i]))
                {
                    ALeffect **list;

                    ALEffect = ((ALeffect*)ALTHUNK_LOOKUPENTRY(effects[i]));

                    // Remove Source from list of Sources
                    list = &g_EffectList;
                    while(*list && *list != ALEffect)
                         list = &(*list)->next;

                    if(*list)
                        *list = (*list)->next;
                    ALTHUNK_REMOVEENTRY(ALEffect->effect);

                    memset(ALEffect, 0, sizeof(ALeffect));
                    free(ALEffect);

                    g_EffectCount--;
                }
            }
        }
    }
    else
        alSetError(AL_INVALID_VALUE);

    ProcessContext(Context);
}

AL_API ALboolean AL_APIENTRY alIsEffect(ALuint effect)
{
    ALCcontext *Context;
    ALeffect **list;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    list = &g_EffectList;
    while(*list && (*list)->effect != effect)
        list = &(*list)->next;

    ProcessContext(Context);

    return ((*list || !effect) ? AL_TRUE : AL_FALSE);
}

AL_API ALvoid AL_APIENTRY alEffecti(ALuint effect, ALenum param, ALint iValue)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        ALeffect *ALEffect = (ALeffect*)ALTHUNK_LOOKUPENTRY(effect);

        if(param == AL_EFFECT_TYPE)
        {
            if(iValue == AL_EFFECT_NULL ||
               iValue == AL_EFFECT_REVERB)
                InitEffectParams(ALEffect, iValue);
            else
                alSetError(AL_INVALID_VALUE);
        }
        else if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DECAY_HFLIMIT:
                if(iValue == AL_TRUE || iValue == AL_FALSE)
                    ALEffect->Reverb.DecayHFLimit = iValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_ENUM);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alEffectiv(ALuint effect, ALenum param, ALint *piValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        ALeffect *ALEffect = (ALeffect*)ALTHUNK_LOOKUPENTRY(effect);

        if(param == AL_EFFECT_TYPE)
        {
            alEffecti(effect, param, piValues[0]);
        }
        else if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DECAY_HFLIMIT:
                alEffecti(effect, param, piValues[0]);
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_ENUM);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alEffectf(ALuint effect, ALenum param, ALfloat flValue)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        ALeffect *ALEffect = (ALeffect*)ALTHUNK_LOOKUPENTRY(effect);

        if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DENSITY:
                if(flValue >= 0.0f && flValue <= 1.0f)
                    ALEffect->Reverb.Density = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_DIFFUSION:
                if(flValue >= 0.0f && flValue <= 1.0f)
                    ALEffect->Reverb.Diffusion = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_GAIN:
                if(flValue >= 0.0f && flValue <= 1.0f)
                    ALEffect->Reverb.Gain = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_GAINHF:
                if(flValue >= 0.0f && flValue <= 1.0f)
                    ALEffect->Reverb.GainHF = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_DECAY_TIME:
                if(flValue >= 0.1f && flValue <= 20.0f)
                    ALEffect->Reverb.DecayTime = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_DECAY_HFRATIO:
                if(flValue >= 0.1f && flValue <= 2.0f)
                    ALEffect->Reverb.DecayHFRatio = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_REFLECTIONS_GAIN:
                if(flValue >= 0.0f && flValue <= 3.16f)
                    ALEffect->Reverb.ReflectionsGain = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_REFLECTIONS_DELAY:
                if(flValue >= 0.0f && flValue <= 0.3f)
                    ALEffect->Reverb.ReflectionsDelay = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_LATE_REVERB_GAIN:
                if(flValue >= 0.0f && flValue <= 10.0f)
                    ALEffect->Reverb.LateReverbGain = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_LATE_REVERB_DELAY:
                if(flValue >= 0.0f && flValue <= 0.1f)
                    ALEffect->Reverb.LateReverbDelay = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_AIR_ABSORPTION_GAINHF:
                if(flValue >= 0.892f && flValue <= 1.0f)
                    ALEffect->Reverb.AirAbsorptionGainHF = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REVERB_ROOM_ROLLOFF_FACTOR:
                if(flValue >= 0.0f && flValue <= 10.0f)
                    ALEffect->Reverb.RoomRolloffFactor = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_ENUM);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alEffectfv(ALuint effect, ALenum param, ALfloat *pflValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        ALeffect *ALEffect = (ALeffect*)ALTHUNK_LOOKUPENTRY(effect);

        if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DENSITY:
            case AL_REVERB_DIFFUSION:
            case AL_REVERB_GAIN:
            case AL_REVERB_GAINHF:
            case AL_REVERB_DECAY_TIME:
            case AL_REVERB_DECAY_HFRATIO:
            case AL_REVERB_REFLECTIONS_GAIN:
            case AL_REVERB_REFLECTIONS_DELAY:
            case AL_REVERB_LATE_REVERB_GAIN:
            case AL_REVERB_LATE_REVERB_DELAY:
            case AL_REVERB_AIR_ABSORPTION_GAINHF:
            case AL_REVERB_ROOM_ROLLOFF_FACTOR:
                alEffectf(effect, param, pflValues[0]);
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_ENUM);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffecti(ALuint effect, ALenum param, ALint *piValue)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        ALeffect *ALEffect = (ALeffect*)ALTHUNK_LOOKUPENTRY(effect);

        if(param == AL_EFFECT_TYPE)
        {
            *piValue = ALEffect->type;
        }
        else if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DECAY_HFLIMIT:
                *piValue = ALEffect->Reverb.DecayHFLimit;
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_ENUM);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffectiv(ALuint effect, ALenum param, ALint *piValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        ALeffect *ALEffect = (ALeffect*)ALTHUNK_LOOKUPENTRY(effect);

        if(param == AL_EFFECT_TYPE)
        {
            alGetEffecti(effect, param, piValues);
        }
        else if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DECAY_HFLIMIT:
                alGetEffecti(effect, param, piValues);
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_ENUM);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffectf(ALuint effect, ALenum param, ALfloat *pflValue)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        ALeffect *ALEffect = (ALeffect*)ALTHUNK_LOOKUPENTRY(effect);

        if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DENSITY:
                *pflValue = ALEffect->Reverb.Density;
                break;

            case AL_REVERB_DIFFUSION:
                *pflValue = ALEffect->Reverb.Diffusion;
                break;

            case AL_REVERB_GAIN:
                *pflValue = ALEffect->Reverb.Gain;
                break;

            case AL_REVERB_GAINHF:
                *pflValue = ALEffect->Reverb.GainHF;
                break;

            case AL_REVERB_DECAY_TIME:
                *pflValue = ALEffect->Reverb.DecayTime;
                break;

            case AL_REVERB_DECAY_HFRATIO:
                *pflValue = ALEffect->Reverb.DecayHFRatio;
                break;

            case AL_REVERB_REFLECTIONS_GAIN:
                *pflValue = ALEffect->Reverb.ReflectionsGain;
                break;

            case AL_REVERB_REFLECTIONS_DELAY:
                *pflValue = ALEffect->Reverb.ReflectionsDelay;
                break;

            case AL_REVERB_LATE_REVERB_GAIN:
                *pflValue = ALEffect->Reverb.LateReverbGain;
                break;

            case AL_REVERB_LATE_REVERB_DELAY:
                *pflValue = ALEffect->Reverb.LateReverbDelay;
                break;

            case AL_REVERB_AIR_ABSORPTION_GAINHF:
                *pflValue = ALEffect->Reverb.AirAbsorptionGainHF;
                break;

            case AL_REVERB_ROOM_ROLLOFF_FACTOR:
                *pflValue = ALEffect->Reverb.RoomRolloffFactor;
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_ENUM);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffectfv(ALuint effect, ALenum param, ALfloat *pflValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        ALeffect *ALEffect = (ALeffect*)ALTHUNK_LOOKUPENTRY(effect);

        if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DENSITY:
            case AL_REVERB_DIFFUSION:
            case AL_REVERB_GAIN:
            case AL_REVERB_GAINHF:
            case AL_REVERB_DECAY_TIME:
            case AL_REVERB_DECAY_HFRATIO:
            case AL_REVERB_REFLECTIONS_GAIN:
            case AL_REVERB_REFLECTIONS_DELAY:
            case AL_REVERB_LATE_REVERB_GAIN:
            case AL_REVERB_LATE_REVERB_DELAY:
            case AL_REVERB_AIR_ABSORPTION_GAINHF:
            case AL_REVERB_ROOM_ROLLOFF_FACTOR:
                alGetEffectf(effect, param, pflValues);
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_ENUM);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}


ALvoid ReleaseALEffects(ALvoid)
{
#ifdef _DEBUG
    if(g_EffectCount > 0)
        AL_PRINT("exit() %d Effect(s) NOT deleted\n", g_EffectCount);
#endif

    while(g_EffectList)
    {
        ALeffect *temp = g_EffectList;
        g_EffectList = g_EffectList->next;

        // Release effect structure
        memset(temp, 0, sizeof(ALeffect));
        free(temp);
    }
    g_EffectCount = 0;
}


static void InitEffectParams(ALeffect *effect, ALenum type)
{
    effect->type = type;
    switch(type)
    {
    case AL_EFFECT_REVERB:
        effect->Reverb.Density = 1.0f;
        effect->Reverb.Diffusion = 1.0f;
        effect->Reverb.Gain = 0.32f;
        effect->Reverb.GainHF = 0.89f;
        effect->Reverb.DecayTime = 1.49f;
        effect->Reverb.DecayHFRatio = 0.83f;
        effect->Reverb.ReflectionsGain = 0.05f;
        effect->Reverb.ReflectionsDelay = 0.007f;
        effect->Reverb.LateReverbGain = 1.26f;
        effect->Reverb.LateReverbDelay = 0.011f;
        effect->Reverb.AirAbsorptionGainHF = 0.994f;
        effect->Reverb.RoomRolloffFactor = 0.0f;
        effect->Reverb.DecayHFLimit = AL_TRUE;
        break;
    }
}
