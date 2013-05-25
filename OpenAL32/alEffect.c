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
#include <float.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alEffect.h"
#include "alThunk.h"
#include "alError.h"


ALboolean DisabledEffects[MAX_EFFECTS];


static void InitEffectParams(ALeffect *effect, ALenum type);


AL_API ALvoid AL_APIENTRY alGenEffects(ALsizei n, ALuint *effects)
{
    ALCcontext *Context;
    ALsizei    cur = 0;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        ALCdevice *device = Context->Device;
        ALenum err;

        CHECK_VALUE(Context, n >= 0);
        for(cur = 0;cur < n;cur++)
        {
            ALeffect *effect = calloc(1, sizeof(ALeffect));
            err = AL_OUT_OF_MEMORY;
            if(!effect || (err=InitEffect(effect)) != AL_NO_ERROR)
            {
                free(effect);
                alDeleteEffects(cur, effects);
                al_throwerr(Context, err);
            }

            err = NewThunkEntry(&effect->id);
            if(err == AL_NO_ERROR)
                err = InsertUIntMapEntry(&device->EffectMap, effect->id, effect);
            if(err != AL_NO_ERROR)
            {
                FreeThunkEntry(effect->id);
                memset(effect, 0, sizeof(ALeffect));
                free(effect);

                alDeleteEffects(cur, effects);
                al_throwerr(Context, err);
            }

            effects[cur] = effect->id;
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alDeleteEffects(ALsizei n, const ALuint *effects)
{
    ALCcontext *Context;
    ALeffect *Effect;
    ALsizei i;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        ALCdevice *device = Context->Device;
        CHECK_VALUE(Context, n >= 0);
        for(i = 0;i < n;i++)
        {
            if(effects[i] && LookupEffect(device, effects[i]) == NULL)
                al_throwerr(Context, AL_INVALID_NAME);
        }

        for(i = 0;i < n;i++)
        {
            if((Effect=RemoveEffect(device, effects[i])) == NULL)
                continue;
            FreeThunkEntry(Effect->id);

            memset(Effect, 0, sizeof(*Effect));
            free(Effect);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALboolean AL_APIENTRY alIsEffect(ALuint effect)
{
    ALCcontext *Context;
    ALboolean  result;

    Context = GetContextRef();
    if(!Context) return AL_FALSE;

    result = ((!effect || LookupEffect(Context->Device, effect)) ?
              AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(Context);

    return result;
}

AL_API ALvoid AL_APIENTRY alEffecti(ALuint effect, ALenum param, ALint value)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetContextRef();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device, effect)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else
    {
        if(param == AL_EFFECT_TYPE)
        {
            ALboolean isOk = (value == AL_EFFECT_NULL);
            ALint i;
            for(i = 0;!isOk && EffectList[i].val;i++)
            {
                if(value == EffectList[i].val &&
                   !DisabledEffects[EffectList[i].type])
                    isOk = AL_TRUE;
            }

            if(isOk)
                InitEffectParams(ALEffect, value);
            else
                alSetError(Context, AL_INVALID_VALUE);
        }
        else
        {
            /* Call the appropriate handler */
            ALeffect_SetParami(ALEffect, Context, param, value);
        }
    }

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alEffectiv(ALuint effect, ALenum param, const ALint *values)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    switch(param)
    {
        case AL_EFFECT_TYPE:
            alEffecti(effect, param, values[0]);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device, effect)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else
    {
        /* Call the appropriate handler */
        ALeffect_SetParamiv(ALEffect, Context, param, values);
    }

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alEffectf(ALuint effect, ALenum param, ALfloat value)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetContextRef();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device, effect)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else
    {
        /* Call the appropriate handler */
        ALeffect_SetParamf(ALEffect, Context, param, value);
    }

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alEffectfv(ALuint effect, ALenum param, const ALfloat *values)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetContextRef();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device, effect)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else
    {
        /* Call the appropriate handler */
        ALeffect_SetParamfv(ALEffect, Context, param, values);
    }

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffecti(ALuint effect, ALenum param, ALint *value)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetContextRef();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device, effect)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else
    {
        if(param == AL_EFFECT_TYPE)
            *value = ALEffect->type;
        else
        {
            /* Call the appropriate handler */
            ALeffect_GetParami(ALEffect, Context, param, value);
        }
    }

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffectiv(ALuint effect, ALenum param, ALint *values)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    switch(param)
    {
        case AL_EFFECT_TYPE:
            alGetEffecti(effect, param, values);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device, effect)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else
    {
        /* Call the appropriate handler */
        ALeffect_GetParamiv(ALEffect, Context, param, values);
    }

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffectf(ALuint effect, ALenum param, ALfloat *value)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetContextRef();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device, effect)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else
    {
        /* Call the appropriate handler */
        ALeffect_GetParamf(ALEffect, Context, param, value);
    }

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffectfv(ALuint effect, ALenum param, ALfloat *values)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetContextRef();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device, effect)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else
    {
        /* Call the appropriate handler */
        ALeffect_GetParamfv(ALEffect, Context, param, values);
    }

    ALCcontext_DecRef(Context);
}


ALenum InitEffect(ALeffect *effect)
{
    InitEffectParams(effect, AL_EFFECT_NULL);
    return AL_NO_ERROR;
}

ALvoid ReleaseALEffects(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->EffectMap.size;i++)
    {
        ALeffect *temp = device->EffectMap.array[i].value;
        device->EffectMap.array[i].value = NULL;

        // Release effect structure
        FreeThunkEntry(temp->id);
        memset(temp, 0, sizeof(ALeffect));
        free(temp);
    }
}


static void InitEffectParams(ALeffect *effect, ALenum type)
{
    switch(type)
    {
    case AL_EFFECT_EAXREVERB:
        effect->Reverb.Density   = AL_EAXREVERB_DEFAULT_DENSITY;
        effect->Reverb.Diffusion = AL_EAXREVERB_DEFAULT_DIFFUSION;
        effect->Reverb.Gain   = AL_EAXREVERB_DEFAULT_GAIN;
        effect->Reverb.GainHF = AL_EAXREVERB_DEFAULT_GAINHF;
        effect->Reverb.GainLF = AL_EAXREVERB_DEFAULT_GAINLF;
        effect->Reverb.DecayTime    = AL_EAXREVERB_DEFAULT_DECAY_TIME;
        effect->Reverb.DecayHFRatio = AL_EAXREVERB_DEFAULT_DECAY_HFRATIO;
        effect->Reverb.DecayLFRatio = AL_EAXREVERB_DEFAULT_DECAY_LFRATIO;
        effect->Reverb.ReflectionsGain   = AL_EAXREVERB_DEFAULT_REFLECTIONS_GAIN;
        effect->Reverb.ReflectionsDelay  = AL_EAXREVERB_DEFAULT_REFLECTIONS_DELAY;
        effect->Reverb.ReflectionsPan[0] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
        effect->Reverb.ReflectionsPan[1] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
        effect->Reverb.ReflectionsPan[2] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
        effect->Reverb.LateReverbGain   = AL_EAXREVERB_DEFAULT_LATE_REVERB_GAIN;
        effect->Reverb.LateReverbDelay  = AL_EAXREVERB_DEFAULT_LATE_REVERB_DELAY;
        effect->Reverb.LateReverbPan[0] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
        effect->Reverb.LateReverbPan[1] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
        effect->Reverb.LateReverbPan[2] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
        effect->Reverb.EchoTime  = AL_EAXREVERB_DEFAULT_ECHO_TIME;
        effect->Reverb.EchoDepth = AL_EAXREVERB_DEFAULT_ECHO_DEPTH;
        effect->Reverb.ModulationTime  = AL_EAXREVERB_DEFAULT_MODULATION_TIME;
        effect->Reverb.ModulationDepth = AL_EAXREVERB_DEFAULT_MODULATION_DEPTH;
        effect->Reverb.AirAbsorptionGainHF = AL_EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF;
        effect->Reverb.HFReference = AL_EAXREVERB_DEFAULT_HFREFERENCE;
        effect->Reverb.LFReference = AL_EAXREVERB_DEFAULT_LFREFERENCE;
        effect->Reverb.RoomRolloffFactor = AL_EAXREVERB_DEFAULT_ROOM_ROLLOFF_FACTOR;
        effect->Reverb.DecayHFLimit = AL_EAXREVERB_DEFAULT_DECAY_HFLIMIT;
        effect->vtbl = &ALeaxreverb_vtable;
        break;
    case AL_EFFECT_REVERB:
        effect->Reverb.Density   = AL_REVERB_DEFAULT_DENSITY;
        effect->Reverb.Diffusion = AL_REVERB_DEFAULT_DIFFUSION;
        effect->Reverb.Gain   = AL_REVERB_DEFAULT_GAIN;
        effect->Reverb.GainHF = AL_REVERB_DEFAULT_GAINHF;
        effect->Reverb.DecayTime    = AL_REVERB_DEFAULT_DECAY_TIME;
        effect->Reverb.DecayHFRatio = AL_REVERB_DEFAULT_DECAY_HFRATIO;
        effect->Reverb.ReflectionsGain   = AL_REVERB_DEFAULT_REFLECTIONS_GAIN;
        effect->Reverb.ReflectionsDelay  = AL_REVERB_DEFAULT_REFLECTIONS_DELAY;
        effect->Reverb.LateReverbGain   = AL_REVERB_DEFAULT_LATE_REVERB_GAIN;
        effect->Reverb.LateReverbDelay  = AL_REVERB_DEFAULT_LATE_REVERB_DELAY;
        effect->Reverb.AirAbsorptionGainHF = AL_REVERB_DEFAULT_AIR_ABSORPTION_GAINHF;
        effect->Reverb.RoomRolloffFactor = AL_REVERB_DEFAULT_ROOM_ROLLOFF_FACTOR;
        effect->Reverb.DecayHFLimit = AL_REVERB_DEFAULT_DECAY_HFLIMIT;
        effect->vtbl = &ALreverb_vtable;
        break;
    case AL_EFFECT_CHORUS:
        effect->Chorus.Waveform = AL_CHORUS_DEFAULT_WAVEFORM;
        effect->Chorus.Phase = AL_CHORUS_DEFAULT_PHASE;
        effect->Chorus.Rate = AL_CHORUS_DEFAULT_RATE;
        effect->Chorus.Depth = AL_CHORUS_DEFAULT_DEPTH;
        effect->Chorus.Feedback = AL_CHORUS_DEFAULT_FEEDBACK;
        effect->Chorus.Delay = AL_CHORUS_DEFAULT_DELAY;
        effect->vtbl = &ALchorus_vtable;
        break;
    case AL_EFFECT_DISTORTION:
        effect->Distortion.Edge = AL_DISTORTION_DEFAULT_EDGE;
        effect->Distortion.Gain = AL_DISTORTION_DEFAULT_GAIN;
        effect->Distortion.LowpassCutoff = AL_DISTORTION_DEFAULT_LOWPASS_CUTOFF;
        effect->Distortion.EQCenter = AL_DISTORTION_DEFAULT_EQCENTER;
        effect->Distortion.EQBandwidth = AL_DISTORTION_DEFAULT_EQBANDWIDTH;
        effect->vtbl = &ALdistortion_vtable;
        break;
    case AL_EFFECT_ECHO:
        effect->Echo.Delay    = AL_ECHO_DEFAULT_DELAY;
        effect->Echo.LRDelay  = AL_ECHO_DEFAULT_LRDELAY;
        effect->Echo.Damping  = AL_ECHO_DEFAULT_DAMPING;
        effect->Echo.Feedback = AL_ECHO_DEFAULT_FEEDBACK;
        effect->Echo.Spread   = AL_ECHO_DEFAULT_SPREAD;
        effect->vtbl = &ALecho_vtable;
        break;
    case AL_EFFECT_EQUALIZER:
        effect->Equalizer.LowCutoff = AL_EQUALIZER_DEFAULT_LOW_CUTOFF;
        effect->Equalizer.LowGain = AL_EQUALIZER_DEFAULT_LOW_GAIN;
        effect->Equalizer.Mid1Center = AL_EQUALIZER_DEFAULT_MID1_CENTER;
        effect->Equalizer.Mid1Gain = AL_EQUALIZER_DEFAULT_MID1_GAIN;
        effect->Equalizer.Mid1Width = AL_EQUALIZER_DEFAULT_MID1_WIDTH;
        effect->Equalizer.Mid2Center = AL_EQUALIZER_DEFAULT_MID2_CENTER;
        effect->Equalizer.Mid2Gain = AL_EQUALIZER_DEFAULT_MID2_GAIN;
        effect->Equalizer.Mid2Width = AL_EQUALIZER_DEFAULT_MID2_WIDTH;
        effect->Equalizer.HighCutoff = AL_EQUALIZER_DEFAULT_HIGH_CUTOFF;
        effect->Equalizer.HighGain = AL_EQUALIZER_DEFAULT_HIGH_GAIN;
        effect->vtbl = &ALequalizer_vtable;
        break;
    case AL_EFFECT_FLANGER:
        effect->Flanger.Waveform = AL_FLANGER_DEFAULT_WAVEFORM;
        effect->Flanger.Phase = AL_FLANGER_DEFAULT_PHASE;
        effect->Flanger.Rate = AL_FLANGER_DEFAULT_RATE;
        effect->Flanger.Depth = AL_FLANGER_DEFAULT_DEPTH;
        effect->Flanger.Feedback = AL_FLANGER_DEFAULT_FEEDBACK;
        effect->Flanger.Delay = AL_FLANGER_DEFAULT_DELAY;
        effect->vtbl = &ALflanger_vtable;
        break;
    case AL_EFFECT_RING_MODULATOR:
        effect->Modulator.Frequency      = AL_RING_MODULATOR_DEFAULT_FREQUENCY;
        effect->Modulator.HighPassCutoff = AL_RING_MODULATOR_DEFAULT_HIGHPASS_CUTOFF;
        effect->Modulator.Waveform       = AL_RING_MODULATOR_DEFAULT_WAVEFORM;
        effect->vtbl = &ALmodulator_vtable;
        break;
    case AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT:
    case AL_EFFECT_DEDICATED_DIALOGUE:
        effect->Dedicated.Gain = 1.0f;
        effect->vtbl = &ALdedicated_vtable;
        break;
    default:
        effect->vtbl = &ALnull_vtable;
        break;
    }
    effect->type = type;
}


#include "AL/efx-presets.h"

#define DECL(x) { #x, EFX_REVERB_PRESET_##x }
static const struct {
    const char name[32];
    EFXEAXREVERBPROPERTIES props;
} reverblist[] = {
    DECL(GENERIC),
    DECL(PADDEDCELL),
    DECL(ROOM),
    DECL(BATHROOM),
    DECL(LIVINGROOM),
    DECL(STONEROOM),
    DECL(AUDITORIUM),
    DECL(CONCERTHALL),
    DECL(CAVE),
    DECL(ARENA),
    DECL(HANGAR),
    DECL(CARPETEDHALLWAY),
    DECL(HALLWAY),
    DECL(STONECORRIDOR),
    DECL(ALLEY),
    DECL(FOREST),
    DECL(CITY),
    DECL(MOUNTAINS),
    DECL(QUARRY),
    DECL(PLAIN),
    DECL(PARKINGLOT),
    DECL(SEWERPIPE),
    DECL(UNDERWATER),
    DECL(DRUGGED),
    DECL(DIZZY),
    DECL(PSYCHOTIC),

    DECL(CASTLE_SMALLROOM),
    DECL(CASTLE_SHORTPASSAGE),
    DECL(CASTLE_MEDIUMROOM),
    DECL(CASTLE_LARGEROOM),
    DECL(CASTLE_LONGPASSAGE),
    DECL(CASTLE_HALL),
    DECL(CASTLE_CUPBOARD),
    DECL(CASTLE_COURTYARD),
    DECL(CASTLE_ALCOVE),

    DECL(FACTORY_SMALLROOM),
    DECL(FACTORY_SHORTPASSAGE),
    DECL(FACTORY_MEDIUMROOM),
    DECL(FACTORY_LARGEROOM),
    DECL(FACTORY_LONGPASSAGE),
    DECL(FACTORY_HALL),
    DECL(FACTORY_CUPBOARD),
    DECL(FACTORY_COURTYARD),
    DECL(FACTORY_ALCOVE),

    DECL(ICEPALACE_SMALLROOM),
    DECL(ICEPALACE_SHORTPASSAGE),
    DECL(ICEPALACE_MEDIUMROOM),
    DECL(ICEPALACE_LARGEROOM),
    DECL(ICEPALACE_LONGPASSAGE),
    DECL(ICEPALACE_HALL),
    DECL(ICEPALACE_CUPBOARD),
    DECL(ICEPALACE_COURTYARD),
    DECL(ICEPALACE_ALCOVE),

    DECL(SPACESTATION_SMALLROOM),
    DECL(SPACESTATION_SHORTPASSAGE),
    DECL(SPACESTATION_MEDIUMROOM),
    DECL(SPACESTATION_LARGEROOM),
    DECL(SPACESTATION_LONGPASSAGE),
    DECL(SPACESTATION_HALL),
    DECL(SPACESTATION_CUPBOARD),
    DECL(SPACESTATION_ALCOVE),

    DECL(WOODEN_SMALLROOM),
    DECL(WOODEN_SHORTPASSAGE),
    DECL(WOODEN_MEDIUMROOM),
    DECL(WOODEN_LARGEROOM),
    DECL(WOODEN_LONGPASSAGE),
    DECL(WOODEN_HALL),
    DECL(WOODEN_CUPBOARD),
    DECL(WOODEN_COURTYARD),
    DECL(WOODEN_ALCOVE),

    DECL(SPORT_EMPTYSTADIUM),
    DECL(SPORT_SQUASHCOURT),
    DECL(SPORT_SMALLSWIMMINGPOOL),
    DECL(SPORT_LARGESWIMMINGPOOL),
    DECL(SPORT_GYMNASIUM),
    DECL(SPORT_FULLSTADIUM),
    DECL(SPORT_STADIUMTANNOY),

    DECL(PREFAB_WORKSHOP),
    DECL(PREFAB_SCHOOLROOM),
    DECL(PREFAB_PRACTISEROOM),
    DECL(PREFAB_OUTHOUSE),
    DECL(PREFAB_CARAVAN),

    DECL(DOME_TOMB),
    DECL(PIPE_SMALL),
    DECL(DOME_SAINTPAULS),
    DECL(PIPE_LONGTHIN),
    DECL(PIPE_LARGE),
    DECL(PIPE_RESONANT),

    DECL(OUTDOORS_BACKYARD),
    DECL(OUTDOORS_ROLLINGPLAINS),
    DECL(OUTDOORS_DEEPCANYON),
    DECL(OUTDOORS_CREEK),
    DECL(OUTDOORS_VALLEY),

    DECL(MOOD_HEAVEN),
    DECL(MOOD_HELL),
    DECL(MOOD_MEMORY),

    DECL(DRIVING_COMMENTATOR),
    DECL(DRIVING_PITGARAGE),
    DECL(DRIVING_INCAR_RACER),
    DECL(DRIVING_INCAR_SPORTS),
    DECL(DRIVING_INCAR_LUXURY),
    DECL(DRIVING_FULLGRANDSTAND),
    DECL(DRIVING_EMPTYGRANDSTAND),
    DECL(DRIVING_TUNNEL),

    DECL(CITY_STREETS),
    DECL(CITY_SUBWAY),
    DECL(CITY_MUSEUM),
    DECL(CITY_LIBRARY),
    DECL(CITY_UNDERPASS),
    DECL(CITY_ABANDONED),

    DECL(DUSTYROOM),
    DECL(CHAPEL),
    DECL(SMALLWATERROOM),
};
#undef DECL

ALvoid LoadReverbPreset(const char *name, ALeffect *effect)
{
    size_t i;

    if(strcasecmp(name, "NONE") == 0)
    {
        InitEffectParams(effect, AL_EFFECT_NULL);
        TRACE("Loading reverb '%s'\n", "NONE");
        return;
    }

    if(!DisabledEffects[EAXREVERB])
        InitEffectParams(effect, AL_EFFECT_EAXREVERB);
    else if(!DisabledEffects[REVERB])
        InitEffectParams(effect, AL_EFFECT_REVERB);
    else
        InitEffectParams(effect, AL_EFFECT_NULL);
    for(i = 0;i < COUNTOF(reverblist);i++)
    {
        const EFXEAXREVERBPROPERTIES *props;

        if(strcasecmp(name, reverblist[i].name) != 0)
            continue;

        TRACE("Loading reverb '%s'\n", reverblist[i].name);
        props = &reverblist[i].props;
        effect->Reverb.Density   = props->flDensity;
        effect->Reverb.Diffusion = props->flDiffusion;
        effect->Reverb.Gain   = props->flGain;
        effect->Reverb.GainHF = props->flGainHF;
        effect->Reverb.GainLF = props->flGainLF;
        effect->Reverb.DecayTime    = props->flDecayTime;
        effect->Reverb.DecayHFRatio = props->flDecayHFRatio;
        effect->Reverb.DecayLFRatio = props->flDecayLFRatio;
        effect->Reverb.ReflectionsGain   = props->flReflectionsGain;
        effect->Reverb.ReflectionsDelay  = props->flReflectionsDelay;
        effect->Reverb.ReflectionsPan[0] = props->flReflectionsPan[0];
        effect->Reverb.ReflectionsPan[1] = props->flReflectionsPan[1];
        effect->Reverb.ReflectionsPan[2] = props->flReflectionsPan[2];
        effect->Reverb.LateReverbGain   = props->flLateReverbGain;
        effect->Reverb.LateReverbDelay  = props->flLateReverbDelay;
        effect->Reverb.LateReverbPan[0] = props->flLateReverbPan[0];
        effect->Reverb.LateReverbPan[1] = props->flLateReverbPan[1];
        effect->Reverb.LateReverbPan[2] = props->flLateReverbPan[2];
        effect->Reverb.EchoTime  = props->flEchoTime;
        effect->Reverb.EchoDepth = props->flEchoDepth;
        effect->Reverb.ModulationTime  = props->flModulationTime;
        effect->Reverb.ModulationDepth = props->flModulationDepth;
        effect->Reverb.AirAbsorptionGainHF = props->flAirAbsorptionGainHF;
        effect->Reverb.HFReference = props->flHFReference;
        effect->Reverb.LFReference = props->flLFReference;
        effect->Reverb.RoomRolloffFactor = props->flRoomRolloffFactor;
        effect->Reverb.DecayHFLimit = props->iDecayHFLimit;
        return;
    }

    WARN("Reverb preset '%s' not found\n", name);
}
