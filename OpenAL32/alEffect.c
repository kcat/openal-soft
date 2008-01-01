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

#include <stdlib.h>

#include "config.h"

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
        // Check that enough memory has been allocted in the 'sources' array for n Sources
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

        switch(param)
        {
        case AL_EFFECT_TYPE:
            if(iValue == AL_EFFECT_NULL)
                InitEffectParams(ALEffect, iValue);
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

AL_API ALvoid AL_APIENTRY alEffectiv(ALuint effect, ALenum param, ALint *piValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        switch(param)
        {
        case AL_EFFECT_TYPE:
            alEffecti(effect, param, piValues[0]);
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

AL_API ALvoid AL_APIENTRY alEffectf(ALuint effect, ALenum param, ALfloat flValue)
{
    ALCcontext *Context;

    (void)flValue;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        switch(param)
        {
        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alEffectfv(ALuint effect, ALenum param, ALfloat *pflValues)
{
    ALCcontext *Context;

    (void)pflValues;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        switch(param)
        {
        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
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

        switch(param)
        {
        case AL_EFFECT_TYPE:
            *piValue = ALEffect->type;
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

AL_API ALvoid AL_APIENTRY alGetEffectiv(ALuint effect, ALenum param, ALint *piValues)
{
    ALCcontext *Context;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        switch(param)
        {
        case AL_EFFECT_TYPE:
            alGetEffecti(effect, param, piValues);
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

AL_API ALvoid AL_APIENTRY alGetEffectf(ALuint effect, ALenum param, ALfloat *pflValue)
{
    ALCcontext *Context;

    (void)pflValue;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        switch(param)
        {
        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffectfv(ALuint effect, ALenum param, ALfloat *pflValues)
{
    ALCcontext *Context;

    (void)pflValues;

    Context = alcGetCurrentContext();
    SuspendContext(Context);

    if (effect && alIsEffect(effect))
    {
        switch(param)
        {
        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
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
}
