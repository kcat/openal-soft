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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "effect.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx-presets.h"
#include "AL/efx.h"

#include "alcmain.h"
#include "alcontext.h"
#include "alexcpt.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "alstring.h"
#include "effects/base.h"
#include "logging.h"
#include "opthelpers.h"
#include "vector.h"


const EffectList gEffectList[15]{
    { "eaxreverb",  EAXREVERB_EFFECT,  AL_EFFECT_EAXREVERB },
    { "reverb",     REVERB_EFFECT,     AL_EFFECT_REVERB },
    { "autowah",    AUTOWAH_EFFECT,    AL_EFFECT_AUTOWAH },
    { "chorus",     CHORUS_EFFECT,     AL_EFFECT_CHORUS },
    { "compressor", COMPRESSOR_EFFECT, AL_EFFECT_COMPRESSOR },
    { "distortion", DISTORTION_EFFECT, AL_EFFECT_DISTORTION },
    { "echo",       ECHO_EFFECT,       AL_EFFECT_ECHO },
    { "equalizer",  EQUALIZER_EFFECT,  AL_EFFECT_EQUALIZER },
    { "flanger",    FLANGER_EFFECT,    AL_EFFECT_FLANGER },
    { "fshifter",   FSHIFTER_EFFECT,   AL_EFFECT_FREQUENCY_SHIFTER },
    { "modulator",  MODULATOR_EFFECT,  AL_EFFECT_RING_MODULATOR },
    { "pshifter",   PSHIFTER_EFFECT,   AL_EFFECT_PITCH_SHIFTER },
    { "vmorpher",   VMORPHER_EFFECT,   AL_EFFECT_VOCAL_MORPHER },
    { "dedicated",  DEDICATED_EFFECT,  AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT },
    { "dedicated",  DEDICATED_EFFECT,  AL_EFFECT_DEDICATED_DIALOGUE },
};

ALboolean DisabledEffects[MAX_EFFECTS];

namespace {

constexpr struct FactoryItem {
    ALenum Type;
    EffectStateFactory* (&GetFactory)(void);
} FactoryList[] = {
    { AL_EFFECT_NULL, NullStateFactory_getFactory },
    { AL_EFFECT_EAXREVERB, ReverbStateFactory_getFactory },
    { AL_EFFECT_REVERB, StdReverbStateFactory_getFactory },
    { AL_EFFECT_AUTOWAH, AutowahStateFactory_getFactory },
    { AL_EFFECT_CHORUS, ChorusStateFactory_getFactory },
    { AL_EFFECT_COMPRESSOR, CompressorStateFactory_getFactory },
    { AL_EFFECT_DISTORTION, DistortionStateFactory_getFactory },
    { AL_EFFECT_ECHO, EchoStateFactory_getFactory },
    { AL_EFFECT_EQUALIZER, EqualizerStateFactory_getFactory },
    { AL_EFFECT_FLANGER, FlangerStateFactory_getFactory },
    { AL_EFFECT_FREQUENCY_SHIFTER, FshifterStateFactory_getFactory },
    { AL_EFFECT_RING_MODULATOR, ModulatorStateFactory_getFactory },
    { AL_EFFECT_PITCH_SHIFTER, PshifterStateFactory_getFactory},
    { AL_EFFECT_VOCAL_MORPHER, VmorpherStateFactory_getFactory},
    { AL_EFFECT_DEDICATED_DIALOGUE, DedicatedStateFactory_getFactory },
    { AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT, DedicatedStateFactory_getFactory }
};


template<typename... T>
void ALeffect_setParami(ALeffect *effect, T&& ...args)
{ effect->vtab->setParami(&effect->Props, std::forward<T>(args)...); }
template<typename... T>
void ALeffect_setParamiv(ALeffect *effect, T&& ...args)
{ effect->vtab->setParamiv(&effect->Props, std::forward<T>(args)...); }
template<typename... T>
void ALeffect_setParamf(ALeffect *effect, T&& ...args)
{ effect->vtab->setParamf(&effect->Props, std::forward<T>(args)...); }
template<typename... T>
void ALeffect_setParamfv(ALeffect *effect, T&& ...args)
{ effect->vtab->setParamfv(&effect->Props, std::forward<T>(args)...); }

template<typename... T>
void ALeffect_getParami(const ALeffect *effect, T&& ...args)
{ effect->vtab->getParami(&effect->Props, std::forward<T>(args)...); }
template<typename... T>
void ALeffect_getParamiv(const ALeffect *effect, T&& ...args)
{ effect->vtab->getParamiv(&effect->Props, std::forward<T>(args)...); }
template<typename... T>
void ALeffect_getParamf(const ALeffect *effect, T&& ...args)
{ effect->vtab->getParamf(&effect->Props, std::forward<T>(args)...); }
template<typename... T>
void ALeffect_getParamfv(const ALeffect *effect, T&& ...args)
{ effect->vtab->getParamfv(&effect->Props, std::forward<T>(args)...); }


void InitEffectParams(ALeffect *effect, ALenum type)
{
    EffectStateFactory *factory = getFactoryByType(type);
    if(factory)
    {
        effect->Props = factory->getDefaultProps();
        effect->vtab = factory->getEffectVtable();
    }
    else
    {
        effect->Props = EffectProps{};
        effect->vtab = nullptr;
    }
    effect->type = type;
}

bool EnsureEffects(ALCdevice *device, size_t needed)
{
    size_t count{std::accumulate(device->EffectList.cbegin(), device->EffectList.cend(), size_t{0},
        [](size_t cur, const EffectSubList &sublist) noexcept -> size_t
        { return cur + static_cast<ALuint>(POPCNT64(sublist.FreeMask)); }
    )};

    while(needed > count)
    {
        if UNLIKELY(device->EffectList.size() >= 1<<25)
            return false;

        device->EffectList.emplace_back();
        auto sublist = device->EffectList.end() - 1;
        sublist->FreeMask = ~0_u64;
        sublist->Effects = static_cast<ALeffect*>(al_calloc(alignof(ALeffect), sizeof(ALeffect)*64));
        if UNLIKELY(!sublist->Effects)
        {
            device->EffectList.pop_back();
            return false;
        }
        count += 64;
    }
    return true;
}

ALeffect *AllocEffect(ALCdevice *device)
{
    auto sublist = std::find_if(device->EffectList.begin(), device->EffectList.end(),
        [](const EffectSubList &entry) noexcept -> bool
        { return entry.FreeMask != 0; }
    );
    auto lidx = static_cast<ALuint>(std::distance(device->EffectList.begin(), sublist));
    auto slidx = static_cast<ALuint>(CTZ64(sublist->FreeMask));

    ALeffect *effect{::new (sublist->Effects + slidx) ALeffect{}};
    InitEffectParams(effect, AL_EFFECT_NULL);

    /* Add 1 to avoid effect ID 0. */
    effect->id = ((lidx<<6) | slidx) + 1;

    sublist->FreeMask &= ~(1_u64 << slidx);

    return effect;
}

void FreeEffect(ALCdevice *device, ALeffect *effect)
{
    const ALuint id{effect->id - 1};
    const size_t lidx{id >> 6};
    const ALuint slidx{id & 0x3f};

    al::destroy_at(effect);

    device->EffectList[lidx].FreeMask |= 1_u64 << slidx;
}

inline ALeffect *LookupEffect(ALCdevice *device, ALuint id)
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if UNLIKELY(lidx >= device->EffectList.size())
        return nullptr;
    EffectSubList &sublist = device->EffectList[lidx];
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.Effects + slidx;
}

} // namespace

AL_API ALvoid AL_APIENTRY alGenEffects(ALsizei n, ALuint *effects)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Generating %d effects", n);
    if UNLIKELY(n <= 0) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};
    if(!EnsureEffects(device, static_cast<ALuint>(n)))
    {
        context->setError(AL_OUT_OF_MEMORY, "Failed to allocate %d effect%s", n, (n==1)?"":"s");
        return;
    }

    if LIKELY(n == 1)
    {
        /* Special handling for the easy and normal case. */
        ALeffect *effect{AllocEffect(device)};
        effects[0] = effect->id;
    }
    else
    {
        /* Store the allocated buffer IDs in a separate local list, to avoid
         * modifying the user storage in case of failure.
         */
        al::vector<ALuint> ids;
        ids.reserve(static_cast<ALuint>(n));
        do {
            ALeffect *effect{AllocEffect(device)};
            ids.emplace_back(effect->id);
        } while(--n);
        std::copy(ids.cbegin(), ids.cend(), effects);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alDeleteEffects(ALsizei n, const ALuint *effects)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Deleting %d effects", n);
    if UNLIKELY(n <= 0) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};

    /* First try to find any effects that are invalid. */
    auto validate_effect = [device](const ALuint eid) -> bool
    { return !eid || LookupEffect(device, eid) != nullptr; };

    const ALuint *effects_end = effects + n;
    auto inveffect = std::find_if_not(effects, effects_end, validate_effect);
    if UNLIKELY(inveffect != effects_end)
    {
        context->setError(AL_INVALID_NAME, "Invalid effect ID %u", *inveffect);
        return;
    }

    /* All good. Delete non-0 effect IDs. */
    auto delete_effect = [device](ALuint eid) -> void
    {
        ALeffect *effect{eid ? LookupEffect(device, eid) : nullptr};
        if(effect) FreeEffect(device, effect);
    };
    std::for_each(effects, effects_end, delete_effect);
}
END_API_FUNC

AL_API ALboolean AL_APIENTRY alIsEffect(ALuint effect)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if LIKELY(context)
    {
        ALCdevice *device{context->mDevice.get()};
        std::lock_guard<std::mutex> _{device->EffectLock};
        if(!effect || LookupEffect(device, effect))
            return AL_TRUE;
    }
    return AL_FALSE;
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alEffecti(ALuint effect, ALenum param, ALint value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};

    ALeffect *aleffect{LookupEffect(device, effect)};
    if UNLIKELY(!aleffect)
        context->setError(AL_INVALID_NAME, "Invalid effect ID %u", effect);
    else
    {
        if(param == AL_EFFECT_TYPE)
        {
            ALboolean isOk{value == AL_EFFECT_NULL};
            if(!isOk)
            {
                for(const EffectList &effectitem : gEffectList)
                {
                    if(value == effectitem.val && !DisabledEffects[effectitem.type])
                    {
                        isOk = AL_TRUE;
                        break;
                    }
                }
            }

            if(isOk)
                InitEffectParams(aleffect, value);
            else
                context->setError(AL_INVALID_VALUE, "Effect type 0x%04x not supported", value);
        }
        else
        {
            /* Call the appropriate handler */
            ALeffect_setParami(aleffect, context.get(), param, value);
        }
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alEffectiv(ALuint effect, ALenum param, const ALint *values)
START_API_FUNC
{
    switch(param)
    {
        case AL_EFFECT_TYPE:
            alEffecti(effect, param, values[0]);
            return;
    }

    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};

    ALeffect *aleffect{LookupEffect(device, effect)};
    if UNLIKELY(!aleffect)
        context->setError(AL_INVALID_NAME, "Invalid effect ID %u", effect);
    else
    {
        /* Call the appropriate handler */
        ALeffect_setParamiv(aleffect, context.get(), param, values);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alEffectf(ALuint effect, ALenum param, ALfloat value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};

    ALeffect *aleffect{LookupEffect(device, effect)};
    if UNLIKELY(!aleffect)
        context->setError(AL_INVALID_NAME, "Invalid effect ID %u", effect);
    else
    {
        /* Call the appropriate handler */
        ALeffect_setParamf(aleffect, context.get(), param, value);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alEffectfv(ALuint effect, ALenum param, const ALfloat *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};

    ALeffect *aleffect{LookupEffect(device, effect)};
    if UNLIKELY(!aleffect)
        context->setError(AL_INVALID_NAME, "Invalid effect ID %u", effect);
    else
    {
        /* Call the appropriate handler */
        ALeffect_setParamfv(aleffect, context.get(), param, values);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alGetEffecti(ALuint effect, ALenum param, ALint *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};

    const ALeffect *aleffect{LookupEffect(device, effect)};
    if UNLIKELY(!aleffect)
        context->setError(AL_INVALID_NAME, "Invalid effect ID %u", effect);
    else
    {
        if(param == AL_EFFECT_TYPE)
            *value = aleffect->type;
        else
        {
            /* Call the appropriate handler */
            ALeffect_getParami(aleffect, context.get(), param, value);
        }
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alGetEffectiv(ALuint effect, ALenum param, ALint *values)
START_API_FUNC
{
    switch(param)
    {
        case AL_EFFECT_TYPE:
            alGetEffecti(effect, param, values);
            return;
    }

    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};

    const ALeffect *aleffect{LookupEffect(device, effect)};
    if UNLIKELY(!aleffect)
        context->setError(AL_INVALID_NAME, "Invalid effect ID %u", effect);
    else
    {
        /* Call the appropriate handler */
        ALeffect_getParamiv(aleffect, context.get(), param, values);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alGetEffectf(ALuint effect, ALenum param, ALfloat *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};

    const ALeffect *aleffect{LookupEffect(device, effect)};
    if UNLIKELY(!aleffect)
        context->setError(AL_INVALID_NAME, "Invalid effect ID %u", effect);
    else
    {
        /* Call the appropriate handler */
        ALeffect_getParamf(aleffect, context.get(), param, value);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alGetEffectfv(ALuint effect, ALenum param, ALfloat *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->EffectLock};

    const ALeffect *aleffect{LookupEffect(device, effect)};
    if UNLIKELY(!aleffect)
        context->setError(AL_INVALID_NAME, "Invalid effect ID %u", effect);
    else
    {
        /* Call the appropriate handler */
        ALeffect_getParamfv(aleffect, context.get(), param, values);
    }
}
END_API_FUNC


void InitEffect(ALeffect *effect)
{
    InitEffectParams(effect, AL_EFFECT_NULL);
}

EffectSubList::~EffectSubList()
{
    uint64_t usemask{~FreeMask};
    while(usemask)
    {
        ALsizei idx = CTZ64(usemask);
        al::destroy_at(Effects+idx);
        usemask &= ~(1_u64 << idx);
    }
    FreeMask = ~usemask;
    al_free(Effects);
    Effects = nullptr;
}


EffectStateFactory *getFactoryByType(ALenum type)
{
    auto iter = std::find_if(std::begin(FactoryList), std::end(FactoryList),
        [type](const FactoryItem &item) noexcept -> bool
        { return item.Type == type; }
    );
    return (iter != std::end(FactoryList)) ? iter->GetFactory() : nullptr;
}


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

void LoadReverbPreset(const char *name, ALeffect *effect)
{
    if(al::strcasecmp(name, "NONE") == 0)
    {
        InitEffectParams(effect, AL_EFFECT_NULL);
        TRACE("Loading reverb '%s'\n", "NONE");
        return;
    }

    if(!DisabledEffects[EAXREVERB_EFFECT])
        InitEffectParams(effect, AL_EFFECT_EAXREVERB);
    else if(!DisabledEffects[REVERB_EFFECT])
        InitEffectParams(effect, AL_EFFECT_REVERB);
    else
        InitEffectParams(effect, AL_EFFECT_NULL);
    for(const auto &reverbitem : reverblist)
    {
        const EFXEAXREVERBPROPERTIES *props;

        if(al::strcasecmp(name, reverbitem.name) != 0)
            continue;

        TRACE("Loading reverb '%s'\n", reverbitem.name);
        props = &reverbitem.props;
        effect->Props.Reverb.Density   = props->flDensity;
        effect->Props.Reverb.Diffusion = props->flDiffusion;
        effect->Props.Reverb.Gain   = props->flGain;
        effect->Props.Reverb.GainHF = props->flGainHF;
        effect->Props.Reverb.GainLF = props->flGainLF;
        effect->Props.Reverb.DecayTime    = props->flDecayTime;
        effect->Props.Reverb.DecayHFRatio = props->flDecayHFRatio;
        effect->Props.Reverb.DecayLFRatio = props->flDecayLFRatio;
        effect->Props.Reverb.ReflectionsGain   = props->flReflectionsGain;
        effect->Props.Reverb.ReflectionsDelay  = props->flReflectionsDelay;
        effect->Props.Reverb.ReflectionsPan[0] = props->flReflectionsPan[0];
        effect->Props.Reverb.ReflectionsPan[1] = props->flReflectionsPan[1];
        effect->Props.Reverb.ReflectionsPan[2] = props->flReflectionsPan[2];
        effect->Props.Reverb.LateReverbGain   = props->flLateReverbGain;
        effect->Props.Reverb.LateReverbDelay  = props->flLateReverbDelay;
        effect->Props.Reverb.LateReverbPan[0] = props->flLateReverbPan[0];
        effect->Props.Reverb.LateReverbPan[1] = props->flLateReverbPan[1];
        effect->Props.Reverb.LateReverbPan[2] = props->flLateReverbPan[2];
        effect->Props.Reverb.EchoTime  = props->flEchoTime;
        effect->Props.Reverb.EchoDepth = props->flEchoDepth;
        effect->Props.Reverb.ModulationTime  = props->flModulationTime;
        effect->Props.Reverb.ModulationDepth = props->flModulationDepth;
        effect->Props.Reverb.AirAbsorptionGainHF = props->flAirAbsorptionGainHF;
        effect->Props.Reverb.HFReference = props->flHFReference;
        effect->Props.Reverb.LFReference = props->flLFReference;
        effect->Props.Reverb.RoomRolloffFactor = props->flRoomRolloffFactor;
        effect->Props.Reverb.DecayHFLimit = props->iDecayHFLimit ? AL_TRUE : AL_FALSE;
        return;
    }

    WARN("Reverb preset '%s' not found\n", name);
}
