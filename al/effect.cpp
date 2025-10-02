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
#include <bit>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "AL/al.h"
#include "AL/alext.h"
#include "AL/efx-presets.h"
#include "AL/efx.h"

#include "al/effects/effects.h"
#include "alc/context.h"
#include "alc/device.h"
#include "alc/inprogext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "alstring.h"
#include "core/except.h"
#include "core/logging.h"
#include "direct_defs.h"
#include "gsl/gsl"
#include "opthelpers.h"

using uint = unsigned int;


constinit const std::array<EffectList,16> gEffectList{{
    { "eaxreverb",   EAXREVERB_EFFECT,   AL_EFFECT_EAXREVERB },
    { "reverb",      REVERB_EFFECT,      AL_EFFECT_REVERB },
    { "autowah",     AUTOWAH_EFFECT,     AL_EFFECT_AUTOWAH },
    { "chorus",      CHORUS_EFFECT,      AL_EFFECT_CHORUS },
    { "compressor",  COMPRESSOR_EFFECT,  AL_EFFECT_COMPRESSOR },
    { "distortion",  DISTORTION_EFFECT,  AL_EFFECT_DISTORTION },
    { "echo",        ECHO_EFFECT,        AL_EFFECT_ECHO },
    { "equalizer",   EQUALIZER_EFFECT,   AL_EFFECT_EQUALIZER },
    { "flanger",     FLANGER_EFFECT,     AL_EFFECT_FLANGER },
    { "fshifter",    FSHIFTER_EFFECT,    AL_EFFECT_FREQUENCY_SHIFTER },
    { "modulator",   MODULATOR_EFFECT,   AL_EFFECT_RING_MODULATOR },
    { "pshifter",    PSHIFTER_EFFECT,    AL_EFFECT_PITCH_SHIFTER },
    { "vmorpher",    VMORPHER_EFFECT,    AL_EFFECT_VOCAL_MORPHER },
    { "dedicated",   DEDICATED_EFFECT,   AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT },
    { "dedicated",   DEDICATED_EFFECT,   AL_EFFECT_DEDICATED_DIALOGUE },
    { "convolution", CONVOLUTION_EFFECT, AL_EFFECT_CONVOLUTION_SOFT },
}};


namespace {

using namespace std::string_view_literals;

using SubListAllocator = al::allocator<std::array<ALeffect,64>>;

constexpr auto GetDefaultProps(ALenum type) noexcept -> const EffectProps&
{
    switch(type)
    {
    case AL_EFFECT_NULL: return NullEffectProps;
    case AL_EFFECT_EAXREVERB: return ReverbEffectProps;
    case AL_EFFECT_REVERB: return StdReverbEffectProps;
    case AL_EFFECT_AUTOWAH: return AutowahEffectProps;
    case AL_EFFECT_CHORUS: return ChorusEffectProps;
    case AL_EFFECT_COMPRESSOR: return CompressorEffectProps;
    case AL_EFFECT_DISTORTION: return DistortionEffectProps;
    case AL_EFFECT_ECHO: return EchoEffectProps;
    case AL_EFFECT_EQUALIZER: return EqualizerEffectProps;
    case AL_EFFECT_FLANGER: return FlangerEffectProps;
    case AL_EFFECT_FREQUENCY_SHIFTER: return FshifterEffectProps;
    case AL_EFFECT_RING_MODULATOR: return ModulatorEffectProps;
    case AL_EFFECT_PITCH_SHIFTER: return PshifterEffectProps;
    case AL_EFFECT_VOCAL_MORPHER: return VmorpherEffectProps;
    case AL_EFFECT_DEDICATED_DIALOGUE: return DedicatedDialogEffectProps;
    case AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT: return DedicatedLfeEffectProps;
    case AL_EFFECT_CONVOLUTION_SOFT: return ConvolutionEffectProps;
    }
    return NullEffectProps;
}

void InitEffectParams(ALeffect *effect, ALenum type) noexcept
{
    switch(type)
    {
    case AL_EFFECT_NULL: effect->PropsVariant.emplace<NullEffectHandler>(); break;
    case AL_EFFECT_EAXREVERB: effect->PropsVariant.emplace<ReverbEffectHandler>(); break;
    case AL_EFFECT_REVERB: effect->PropsVariant.emplace<StdReverbEffectHandler>(); break;
    case AL_EFFECT_AUTOWAH: effect->PropsVariant.emplace<AutowahEffectHandler>(); break;
    case AL_EFFECT_CHORUS: effect->PropsVariant.emplace<ChorusEffectHandler>(); break;
    case AL_EFFECT_COMPRESSOR: effect->PropsVariant.emplace<CompressorEffectHandler>(); break;
    case AL_EFFECT_DISTORTION: effect->PropsVariant.emplace<DistortionEffectHandler>(); break;
    case AL_EFFECT_ECHO: effect->PropsVariant.emplace<EchoEffectHandler>(); break;
    case AL_EFFECT_EQUALIZER: effect->PropsVariant.emplace<EqualizerEffectHandler>(); break;
    case AL_EFFECT_FLANGER: effect->PropsVariant.emplace<ChorusEffectHandler>(); break;
    case AL_EFFECT_FREQUENCY_SHIFTER: effect->PropsVariant.emplace<FshifterEffectHandler>(); break;
    case AL_EFFECT_RING_MODULATOR: effect->PropsVariant.emplace<ModulatorEffectHandler>(); break;
    case AL_EFFECT_PITCH_SHIFTER: effect->PropsVariant.emplace<PshifterEffectHandler>(); break;
    case AL_EFFECT_VOCAL_MORPHER: effect->PropsVariant.emplace<VmorpherEffectHandler>(); break;
    case AL_EFFECT_DEDICATED_DIALOGUE:
        effect->PropsVariant.emplace<DedicatedDialogEffectHandler>();
        break;
    case AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT:
        effect->PropsVariant.emplace<DedicatedLfeEffectHandler>();
        break;
    case AL_EFFECT_CONVOLUTION_SOFT:
        effect->PropsVariant.emplace<ConvolutionEffectHandler>();
        break;
    }
    effect->Props = GetDefaultProps(type);
    effect->type = type;
}

[[nodiscard]]
auto EnsureEffects(gsl::not_null<al::Device*> device, size_t needed) noexcept -> bool
try {
    auto count = std::accumulate(device->EffectList.cbegin(), device->EffectList.cend(), 0_uz,
        [](size_t cur, const EffectSubList &sublist) noexcept -> size_t
        { return cur + gsl::narrow_cast<uint>(std::popcount(sublist.FreeMask)); });

    while(needed > count)
    {
        if(device->EffectList.size() >= 1<<25) [[unlikely]]
            return false;

        auto sublist = EffectSubList{};
        sublist.FreeMask = ~0_u64;
        sublist.Effects = SubListAllocator{}.allocate(1);
        device->EffectList.emplace_back(std::move(sublist));
        count += std::tuple_size_v<SubListAllocator::value_type>;
    }
    return true;
}
catch(...) {
    return false;
}

[[nodiscard]]
auto AllocEffect(gsl::not_null<al::Device*> device) noexcept -> gsl::not_null<ALeffect*>
{
    auto sublist = std::ranges::find_if(device->EffectList, &EffectSubList::FreeMask);
    auto lidx = gsl::narrow_cast<uint>(std::distance(device->EffectList.begin(), sublist));
    auto slidx = gsl::narrow_cast<uint>(std::countr_zero(sublist->FreeMask));
    ASSUME(slidx < 64);

    auto effect = gsl::make_not_null(std::construct_at(
        std::to_address(std::next(sublist->Effects->begin(), slidx))));
    InitEffectParams(effect, AL_EFFECT_NULL);

    /* Add 1 to avoid effect ID 0. */
    effect->id = ((lidx<<6) | slidx) + 1;

    sublist->FreeMask &= ~(1_u64 << slidx);

    return effect;
}

void FreeEffect(gsl::not_null<al::Device*> device, gsl::not_null<ALeffect*> effect)
{
    device->mEffectNames.erase(effect->id);

    const auto id = effect->id - 1;
    const auto lidx = id >> 6;
    const auto slidx = id & 0x3f;

    std::destroy_at(std::to_address(effect));

    device->EffectList[lidx].FreeMask |= 1_u64 << slidx;
}

[[nodiscard]]
inline auto LookupEffect(std::nothrow_t, gsl::not_null<al::Device*> device, ALuint id) noexcept
    -> ALeffect*
{
    const auto lidx = (id-1) >> 6;
    const auto slidx = (id-1) & 0x3f;

    if(lidx >= device->EffectList.size()) [[unlikely]]
        return nullptr;
    auto &sublist = device->EffectList[lidx];
    if(sublist.FreeMask & (1_u64 << slidx)) [[unlikely]]
        return nullptr;
    return std::to_address(std::next(sublist.Effects->begin(), slidx));
}

[[nodiscard]]
auto LookupEffect(gsl::not_null<al::Context*> context, ALuint id) -> gsl::not_null<ALeffect*>
{
    if(auto *effect = LookupEffect(std::nothrow, al::get_not_null(context->mALDevice), id))
        [[likely]] return gsl::make_not_null(effect);
    context->throw_error(AL_INVALID_NAME, "Invalid effect ID {}", id);
}


void alGenEffects(gsl::not_null<al::Context*> context, ALsizei n, ALuint *effects) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Generating {} effects", n);
    if(n <= 0) [[unlikely]] return;

    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    const auto eids = std::views::counted(effects, n);
    if(!EnsureEffects(device, eids.size()))
        context->throw_error(AL_OUT_OF_MEMORY, "Failed to allocate {} effect{}", n,
            (n==1) ? "" : "s");

    std::ranges::generate(eids, [device]{ return AllocEffect(device)->id; });
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alDeleteEffects(gsl::not_null<al::Context*> context, ALsizei n, const ALuint *effects)
    noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Deleting {} effects", n);
    if(n <= 0) [[unlikely]] return;

    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    /* First try to find any effects that are invalid. */
    const auto eids = std::views::counted(effects, n);
    std::ranges::for_each(eids, [context](const ALuint eid)
    { if(eid != 0) std::ignore = LookupEffect(context, eid); });

    /* All good. Delete non-0 effect IDs. */
    std::ranges::for_each(eids, [device](ALuint eid)
    {
        if(auto *effect = LookupEffect(std::nothrow, device, eid))
            FreeEffect(device, gsl::make_not_null(effect));
    });
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

auto alIsEffect(gsl::not_null<al::Context*> context, ALuint effect) noexcept -> ALboolean
{
    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};
    if(effect == 0 || LookupEffect(std::nothrow, device, effect) != nullptr)
        return AL_TRUE;
    return AL_FALSE;
}


void alEffecti(gsl::not_null<al::Context*> context, ALuint effect, ALenum param, ALint value)
    noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    auto const aleffect = LookupEffect(context, effect);
    switch(param)
    {
    case AL_EFFECT_TYPE:
        if(value != AL_EFFECT_NULL)
        {
            auto check_effect = [value](const EffectList &item) -> bool
            { return value == item.val && !DisabledEffects.test(item.type); };
            if(!std::ranges::any_of(gEffectList, check_effect))
                context->throw_error(AL_INVALID_VALUE, "Effect type {:#04x} not supported",
                    as_unsigned(value));
        }

        InitEffectParams(aleffect, value);
        return;
    }

    /* Call the appropriate handler */
    std::visit([context,aleffect,param,value]<typename T>(T &arg)
    {
        using PropType = T::prop_type;
        return arg.SetParami(context, std::get<PropType>(aleffect->Props), param, value);
    }, aleffect->PropsVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alEffectiv(gsl::not_null<al::Context*> context, ALuint effect, ALenum param,
    const ALint *values) noexcept
try {
    switch(param)
    {
    case AL_EFFECT_TYPE:
        alEffecti(context, effect, param, *values);
        return;
    }

    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    auto const aleffect = LookupEffect(context, effect);

    /* Call the appropriate handler */
    std::visit([context,aleffect,param,values]<typename T>(T &arg)
    {
        using PropType = T::prop_type;
        return arg.SetParamiv(context, std::get<PropType>(aleffect->Props), param, values);
    }, aleffect->PropsVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alEffectf(gsl::not_null<al::Context*> context, ALuint effect, ALenum param, ALfloat value)
    noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    auto const aleffect = LookupEffect(context, effect);

    /* Call the appropriate handler */
    std::visit([context,aleffect,param,value]<typename T>(T &arg)
    {
        using PropType = T::prop_type;
        return arg.SetParamf(context, std::get<PropType>(aleffect->Props), param, value);
    }, aleffect->PropsVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alEffectfv(gsl::not_null<al::Context*> context, ALuint effect, ALenum param,
    const ALfloat *values) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    auto const aleffect = LookupEffect(context, effect);

    /* Call the appropriate handler */
    std::visit([context,aleffect,param,values]<typename T>(T &arg)
    {
        using PropType = T::prop_type;
        return arg.SetParamfv(context, std::get<PropType>(aleffect->Props), param, values);
    }, aleffect->PropsVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetEffecti(gsl::not_null<al::Context*> context, ALuint effect, ALenum param, ALint *value)
    noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    auto const aleffect = LookupEffect(context, effect);
    switch(param)
    {
    case AL_EFFECT_TYPE: *value = aleffect->type; return;
    }

    /* Call the appropriate handler */
    std::visit([context,aleffect,param,value]<typename T>(T &arg)
    {
        using PropType = T::prop_type;
        return arg.GetParami(context, std::get<PropType>(aleffect->Props), param, value);
    }, aleffect->PropsVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetEffectiv(gsl::not_null<al::Context*> context, ALuint effect, ALenum param, ALint *values)
    noexcept
try {
    switch(param)
    {
    case AL_EFFECT_TYPE:
        alGetEffecti(context, effect, param, values);
        return;
    }

    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    auto const aleffect = LookupEffect(context, effect);

    /* Call the appropriate handler */
    std::visit([context,aleffect,param,values]<typename T>(T &arg)
    {
        using PropType = T::prop_type;
        return arg.GetParamiv(context, std::get<PropType>(aleffect->Props), param, values);
    }, aleffect->PropsVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetEffectf(gsl::not_null<al::Context*> context, ALuint effect, ALenum param, ALfloat *value)
    noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    auto const aleffect = LookupEffect(context, effect);

    /* Call the appropriate handler */
    std::visit([context,aleffect,param,value]<typename T>(T &arg)
    {
        using PropType = T::prop_type;
        return arg.GetParamf(context, std::get<PropType>(aleffect->Props), param, value);
    }, aleffect->PropsVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetEffectfv(gsl::not_null<al::Context*> context, ALuint effect, ALenum param,
    ALfloat *values) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    auto const aleffect = LookupEffect(context, effect);

    /* Call the appropriate handler */
    std::visit([context,aleffect,param,values]<typename T>(T &arg)
    {
        using PropType = T::prop_type;
        return arg.GetParamfv(context, std::get<PropType>(aleffect->Props), param, values);
    }, aleffect->PropsVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

} // namespace

AL_API DECL_FUNC2(void, alGenEffects, ALsizei,n, ALuint*,effects)
AL_API DECL_FUNC2(void, alDeleteEffects, ALsizei,n, const ALuint*,effects)
AL_API DECL_FUNC1(ALboolean, alIsEffect, ALuint,effect)

AL_API DECL_FUNC3(void, alEffecti, ALuint,effect, ALenum,param, ALint,value)
AL_API DECL_FUNC3(void, alEffectiv, ALuint,effect, ALenum,param, const ALint*,values)
AL_API DECL_FUNC3(void, alEffectf, ALuint,effect, ALenum,param, ALfloat,value)
AL_API DECL_FUNC3(void, alEffectfv, ALuint,effect, ALenum,param, const ALfloat*,values)
AL_API DECL_FUNC3(void, alGetEffecti, ALuint,effect, ALenum,param, ALint*,value)
AL_API DECL_FUNC3(void, alGetEffectiv, ALuint,effect, ALenum,param, ALint*,values)
AL_API DECL_FUNC3(void, alGetEffectf, ALuint,effect, ALenum,param, ALfloat*,value)
AL_API DECL_FUNC3(void, alGetEffectfv, ALuint,effect, ALenum,param, ALfloat*,values)


void InitEffect(ALeffect *effect)
{
    InitEffectParams(effect, AL_EFFECT_NULL);
}

void ALeffect::SetName(gsl::not_null<al::Context*> context, ALuint id, std::string_view name)
{
    auto const device = al::get_not_null(context->mALDevice);
    auto effectlock = std::lock_guard{device->EffectLock};

    std::ignore = LookupEffect(context, id);

    device->mEffectNames.insert_or_assign(id, name);
}


EffectSubList::~EffectSubList()
{
    if(!Effects)
        return;

    auto usemask = ~FreeMask;
    while(usemask)
    {
        const auto idx = std::countr_zero(usemask);
        std::destroy_at(std::to_address(std::next(Effects->begin(), idx)));
        usemask &= ~(1_u64 << idx);
    }
    FreeMask = ~usemask;
    SubListAllocator{}.deallocate(Effects, 1);
    Effects = nullptr;
}


struct EffectPreset {
    std::string_view name;
    EFXEAXREVERBPROPERTIES props;
};
#define DECL(x) EffectPreset{#x##sv, EFX_REVERB_PRESET_##x}
static constexpr auto reverblist = std::array{
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

void LoadReverbPreset(const std::string_view name, ALeffect *effect)
{
    if(al::case_compare(name, "NONE"sv) == 0)
    {
        InitEffectParams(effect, AL_EFFECT_NULL);
        TRACE("Loading reverb '{}'", "NONE");
        return;
    }

    if(!DisabledEffects.test(EAXREVERB_EFFECT))
        InitEffectParams(effect, AL_EFFECT_EAXREVERB);
    else if(!DisabledEffects.test(REVERB_EFFECT))
        InitEffectParams(effect, AL_EFFECT_REVERB);
    else
    {
        TRACE("Reverb disabled, ignoring preset '{}'", name);
        InitEffectParams(effect, AL_EFFECT_NULL);
        return;
    }

    const auto preset = std::ranges::find_if(reverblist, [name](const EffectPreset &item) -> bool
    { return al::case_compare(name, item.name) == 0; });
    if(preset == reverblist.end())
    {
        WARN("Reverb preset '{}' not found", name);
        return;
    }

    TRACE("Loading reverb '{}'", preset->name);
    const auto &props = preset->props;
    auto &dst = std::get<ReverbProps>(effect->Props);
    dst.Density   = props.flDensity;
    dst.Diffusion = props.flDiffusion;
    dst.Gain   = props.flGain;
    dst.GainHF = props.flGainHF;
    dst.GainLF = props.flGainLF;
    dst.DecayTime    = props.flDecayTime;
    dst.DecayHFRatio = props.flDecayHFRatio;
    dst.DecayLFRatio = props.flDecayLFRatio;
    dst.ReflectionsGain   = props.flReflectionsGain;
    dst.ReflectionsDelay  = props.flReflectionsDelay;
    dst.ReflectionsPan[0] = props.flReflectionsPan[0];
    dst.ReflectionsPan[1] = props.flReflectionsPan[1];
    dst.ReflectionsPan[2] = props.flReflectionsPan[2];
    dst.LateReverbGain   = props.flLateReverbGain;
    dst.LateReverbDelay  = props.flLateReverbDelay;
    dst.LateReverbPan[0] = props.flLateReverbPan[0];
    dst.LateReverbPan[1] = props.flLateReverbPan[1];
    dst.LateReverbPan[2] = props.flLateReverbPan[2];
    dst.EchoTime  = props.flEchoTime;
    dst.EchoDepth = props.flEchoDepth;
    dst.ModulationTime  = props.flModulationTime;
    dst.ModulationDepth = props.flModulationDepth;
    dst.AirAbsorptionGainHF = props.flAirAbsorptionGainHF;
    dst.HFReference = props.flHFReference;
    dst.LFReference = props.flLFReference;
    dst.RoomRolloffFactor = props.flRoomRolloffFactor;
    dst.DecayHFLimit = props.iDecayHFLimit ? AL_TRUE : AL_FALSE;
}

bool IsValidEffectType(ALenum type) noexcept
{
    if(type == AL_EFFECT_NULL)
        return true;

    return std::ranges::any_of(gEffectList, [type](const EffectList &item) noexcept -> bool
    { return type == item.val && !DisabledEffects.test(item.type); });
}
