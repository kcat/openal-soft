/**
 * OpenAL cross platform audio library
 * Copyright (C) 2009 by Chris Robinson.
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

#include <math.h>
#include <stdlib.h>

#include "alMain.h"
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"


typedef struct ALmodulatorState {
    // Must be first in all effects!
    ALeffectState state;
} ALmodulatorState;

static ALvoid ModulatorDestroy(ALeffectState *effect)
{
    ALmodulatorState *state = (ALmodulatorState*)effect;
    free(state);
}

static ALboolean ModulatorDeviceUpdate(ALeffectState *effect, ALCdevice *Device)
{
    return AL_TRUE;
    (void)Device;
    (void)effect;
}

static ALvoid ModulatorUpdate(ALeffectState *effect, ALCcontext *Context, const ALeffect *Effect)
{
    (void)effect;
    (void)Context;
    (void)Effect;
}

static ALvoid ModulatorProcess(ALeffectState *effect, const ALeffectslot *Slot, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[OUTPUTCHANNELS])
{
    ALmodulatorState *state = (ALmodulatorState*)effect;
    const ALfloat gain = Slot->Gain;
    ALfloat samp;
    ALuint i;
    (void)state;

    for(i = 0;i < SamplesToDo;i++)
    {
        samp = SamplesIn[i];

        // Apply slot gain
        samp *= gain;

        SamplesOut[i][FRONT_LEFT]  += samp;
        SamplesOut[i][FRONT_RIGHT] += samp;
        SamplesOut[i][SIDE_LEFT]   += samp;
        SamplesOut[i][SIDE_RIGHT]  += samp;
        SamplesOut[i][BACK_LEFT]   += samp;
        SamplesOut[i][BACK_RIGHT]  += samp;
    }
}

ALeffectState *ModulatorCreate(void)
{
    ALmodulatorState *state;

    state = malloc(sizeof(*state));
    if(!state)
        return NULL;

    state->state.Destroy = ModulatorDestroy;
    state->state.DeviceUpdate = ModulatorDeviceUpdate;
    state->state.Update = ModulatorUpdate;
    state->state.Process = ModulatorProcess;

    return &state->state;
}
