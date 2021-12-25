/*

EAX OpenAL Extension

Copyright (c) 2020-2021 Boris I. Bendovsky (bibendovsky@hotmail.com) and Contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

*/


#include "eax_eaxx_eax_reverb_effect.h"

#include <cmath>

#include <array>
#include <tuple>

#include "AL/efx.h"

#include "eax_algorithm.h"
#include "eax_exception.h"

#include "eax_al_object.h"
#include "eax_unit_converters.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxEaxReverbEffectDirtyFlags& lhs,
	const EaxxEaxReverbEffectDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxEaxReverbEffectDirtyFlags::Value&>(lhs) ==
			reinterpret_cast<const EaxxEaxReverbEffectDirtyFlags::Value&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEaxReverbEffectException :
	public Exception
{
public:
	explicit EaxxEaxReverbEffectException(
		const char* message)
		:
		Exception{"EAXX_EAX_REVERB_EFFECT", message}
	{
	}
}; // EaxxEaxReverbEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxEaxReverbEffect::EaxxEaxReverbEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_EAXREVERB)}
{
	if (al_effect_slot == 0)
	{
		throw EaxxEaxReverbEffectException{"Null EFX effect slot object."};
	}

	set_eax_defaults();
	set_efx_defaults();
}


// ----------------------------------------------------------------------
// Effect

void EaxxEaxReverbEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxEaxReverbEffect::dispatch(
	const EaxxEaxCall& eax_call)
{
	if (eax_call.is_get())
	{
		get(eax_call);
	}
	else
	{
		set(eax_call);
	}
}

// Effect
// ----------------------------------------------------------------------

void EaxxEaxReverbEffect::set_eax_defaults()
{
	eax_ = ::EAXREVERB_PRESETS[::EAX_ENVIRONMENT_GENERIC];

	eax_d_ = eax_;
}

void EaxxEaxReverbEffect::set_efx_density()
{
	const auto eax_environment_size = eax_.flEnvironmentSize;

	const auto efx_density = clamp(
		(eax_environment_size * eax_environment_size * eax_environment_size) / 16.0F,
		AL_EAXREVERB_MIN_DENSITY,
		AL_EAXREVERB_MAX_DENSITY
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_DENSITY, efx_density);
}

void EaxxEaxReverbEffect::set_efx_diffusion()
{
	const auto efx_environment_diffusion = clamp(
		eax_.flEnvironmentDiffusion,
		AL_EAXREVERB_MIN_DIFFUSION,
		AL_EAXREVERB_MAX_DIFFUSION
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_DIFFUSION, efx_environment_diffusion);
}

void EaxxEaxReverbEffect::set_efx_gain()
{
	const auto efx_room = clamp(
		level_mb_to_gain(eax_.lRoom),
		AL_EAXREVERB_MIN_GAIN,
		AL_EAXREVERB_MAX_GAIN
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_GAIN, efx_room);
}

void EaxxEaxReverbEffect::set_efx_gain_hf()
{
	const auto efx_room_hf = clamp(
		level_mb_to_gain(eax_.lRoomHF),
		AL_EAXREVERB_MIN_GAINHF,
		AL_EAXREVERB_MAX_GAINHF
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_GAINHF, efx_room_hf);
}

void EaxxEaxReverbEffect::set_efx_gain_lf()
{
	const auto efx_room_lf = clamp(
		level_mb_to_gain(eax_.lRoomLF),
		AL_EAXREVERB_MIN_GAINLF,
		AL_EAXREVERB_MAX_GAINLF
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_GAINLF, efx_room_lf);
}

void EaxxEaxReverbEffect::set_efx_decay_time()
{
	const auto efx_decay_time = clamp(
		eax_.flDecayTime,
		AL_EAXREVERB_MIN_DECAY_TIME,
		AL_EAXREVERB_MAX_DECAY_TIME
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_DECAY_TIME, efx_decay_time);
}

void EaxxEaxReverbEffect::set_efx_decay_hf_ratio()
{
	const auto efx_decay_hf_ratio = clamp(
		eax_.flDecayHFRatio,
		AL_EAXREVERB_MIN_DECAY_HFRATIO,
		AL_EAXREVERB_MAX_DECAY_HFRATIO
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_DECAY_HFRATIO, efx_decay_hf_ratio);
}

void EaxxEaxReverbEffect::set_efx_decay_lf_ratio()
{
	const auto efx_decay_lf_ratio = clamp(
		eax_.flDecayLFRatio,
		AL_EAXREVERB_MIN_DECAY_LFRATIO,
		AL_EAXREVERB_MAX_DECAY_LFRATIO
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_DECAY_LFRATIO, efx_decay_lf_ratio);
}

void EaxxEaxReverbEffect::set_efx_reflections_gain()
{
	const auto efx_reflections = clamp(
		level_mb_to_gain(eax_.lReflections),
		AL_EAXREVERB_MIN_REFLECTIONS_GAIN,
		AL_EAXREVERB_MAX_REFLECTIONS_GAIN
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_REFLECTIONS_GAIN, efx_reflections);
}

void EaxxEaxReverbEffect::set_efx_reflections_delay()
{
	const auto efx_reflections_delay = clamp(
		eax_.flReflectionsDelay,
		AL_EAXREVERB_MIN_REFLECTIONS_DELAY,
		AL_EAXREVERB_MAX_REFLECTIONS_DELAY
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_REFLECTIONS_DELAY, efx_reflections_delay);
}

void EaxxEaxReverbEffect::set_efx_reflections_pan()
{
	::alEffectfv(
		efx_effect_object_.get(),
		AL_EAXREVERB_REFLECTIONS_PAN,
		&eax_.vReflectionsPan.x
	);
}

void EaxxEaxReverbEffect::set_efx_late_reverb_gain()
{
	const auto efx_reverb = clamp(
		level_mb_to_gain(eax_.lReverb),
		AL_EAXREVERB_MIN_LATE_REVERB_GAIN,
		AL_EAXREVERB_MAX_LATE_REVERB_GAIN
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_LATE_REVERB_GAIN, efx_reverb);
}

void EaxxEaxReverbEffect::set_efx_late_reverb_delay()
{
	const auto efx_reverb_delay = clamp(
		eax_.flReverbDelay,
		AL_EAXREVERB_MIN_LATE_REVERB_DELAY,
		AL_EAXREVERB_MAX_LATE_REVERB_DELAY);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_LATE_REVERB_DELAY, efx_reverb_delay);
}

void EaxxEaxReverbEffect::set_efx_late_reverb_pan()
{
	::alEffectfv(
		efx_effect_object_.get(),
		AL_EAXREVERB_LATE_REVERB_PAN,
		&eax_.vReverbPan.x
	);
}

void EaxxEaxReverbEffect::set_efx_echo_time()
{
	const auto efx_echo_time = clamp(
		eax_.flEchoTime,
		AL_EAXREVERB_MIN_ECHO_TIME,
		AL_EAXREVERB_MAX_ECHO_TIME
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_ECHO_TIME, efx_echo_time);
}

void EaxxEaxReverbEffect::set_efx_echo_depth()
{
	const auto efx_echo_depth = clamp(
		eax_.flEchoDepth,
		AL_EAXREVERB_MIN_ECHO_DEPTH,
		AL_EAXREVERB_MAX_ECHO_DEPTH
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_ECHO_DEPTH, efx_echo_depth);
}

void EaxxEaxReverbEffect::set_efx_modulation_time()
{
	const auto efx_modulation_time = clamp(
		eax_.flModulationTime,
		AL_EAXREVERB_MIN_MODULATION_TIME,
		AL_EAXREVERB_MAX_MODULATION_TIME
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_MODULATION_TIME, efx_modulation_time);
}

void EaxxEaxReverbEffect::set_efx_modulation_depth()
{
	const auto efx_modulation_depth = clamp(
		eax_.flModulationDepth,
		AL_EAXREVERB_MIN_MODULATION_DEPTH,
		AL_EAXREVERB_MAX_MODULATION_DEPTH
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_MODULATION_DEPTH, efx_modulation_depth);
}

void EaxxEaxReverbEffect::set_efx_air_absorption_gain_hf()
{
	const auto efx_air_absorption_hf = clamp(
		level_mb_to_gain(eax_.flAirAbsorptionHF),
		AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF,
		AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_AIR_ABSORPTION_GAINHF, efx_air_absorption_hf);
}

void EaxxEaxReverbEffect::set_efx_hf_reference()
{
	const auto efx_hf_reference = clamp(
		eax_.flHFReference,
		AL_EAXREVERB_MIN_HFREFERENCE,
		AL_EAXREVERB_MAX_HFREFERENCE
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_HFREFERENCE, efx_hf_reference);
}

void EaxxEaxReverbEffect::set_efx_lf_reference()
{
	const auto efx_lf_reference = clamp(
		eax_.flLFReference,
		AL_EAXREVERB_MIN_LFREFERENCE,
		AL_EAXREVERB_MAX_LFREFERENCE
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_LFREFERENCE, efx_lf_reference);
}

void EaxxEaxReverbEffect::set_efx_room_rolloff_factor()
{
	const auto efx_room_rolloff_factor = clamp(
		eax_.flRoomRolloffFactor,
		AL_EAXREVERB_MIN_ROOM_ROLLOFF_FACTOR,
		AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR
	);

	::alEffectf(efx_effect_object_.get(), AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, efx_room_rolloff_factor);
}

void EaxxEaxReverbEffect::set_efx_flags()
{
	::alEffecti(
		efx_effect_object_.get(),
		AL_EAXREVERB_DECAY_HFLIMIT,
		(eax_.ulFlags & ::EAXREVERBFLAGS_DECAYHFLIMIT) != 0
	);
}

void EaxxEaxReverbEffect::set_efx_defaults()
{
	set_efx_density();
	set_efx_diffusion();
	set_efx_gain();
	set_efx_gain_hf();
	set_efx_gain_lf();
	set_efx_decay_time();
	set_efx_decay_hf_ratio();
	set_efx_decay_lf_ratio();
	set_efx_reflections_gain();
	set_efx_reflections_delay();
	set_efx_reflections_pan();
	set_efx_late_reverb_gain();
	set_efx_late_reverb_delay();
	set_efx_late_reverb_pan();
	set_efx_echo_time();
	set_efx_echo_depth();
	set_efx_modulation_time();
	set_efx_modulation_depth();
	set_efx_air_absorption_gain_hf();
	set_efx_hf_reference();
	set_efx_lf_reference();
	set_efx_room_rolloff_factor();
	set_efx_flags();
}

void EaxxEaxReverbEffect::get_all(
	const EaxxEaxCall& eax_call) const
{
	if (eax_call.get_version() == 2)
	{
		auto& eax_reverb = eax_call.get_value<EaxxEaxReverbEffectException, ::EAX20LISTENERPROPERTIES>();
		eax_reverb.lRoom = eax_.lRoom;
		eax_reverb.lRoomHF = eax_.lRoomHF;
		eax_reverb.flRoomRolloffFactor = eax_.flRoomRolloffFactor;
		eax_reverb.flDecayTime = eax_.flDecayTime;
		eax_reverb.flDecayHFRatio = eax_.flDecayHFRatio;
		eax_reverb.lReflections = eax_.lReflections;
		eax_reverb.flReflectionsDelay = eax_.flReflectionsDelay;
		eax_reverb.lReverb = eax_.lReverb;
		eax_reverb.flReverbDelay = eax_.flReverbDelay;
		eax_reverb.dwEnvironment = eax_.ulEnvironment;
		eax_reverb.flEnvironmentSize = eax_.flEnvironmentSize;
		eax_reverb.flEnvironmentDiffusion = eax_.flEnvironmentDiffusion;
		eax_reverb.flAirAbsorptionHF = eax_.flAirAbsorptionHF;
		eax_reverb.dwFlags = eax_.ulFlags;
	}
	else
	{
		eax_call.set_value<EaxxEaxReverbEffectException>(eax_);
	}
}

void EaxxEaxReverbEffect::get(
	const EaxxEaxCall& eax_call) const
{
	switch (eax_call.get_property_id())
	{
		case ::EAXREVERB_NONE:
			break;

		case ::EAXREVERB_ALLPARAMETERS:
			get_all(eax_call);
			break;

		case ::EAXREVERB_ENVIRONMENT:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.ulEnvironment);
			break;

		case ::EAXREVERB_ENVIRONMENTSIZE:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flEnvironmentSize);
			break;

		case ::EAXREVERB_ENVIRONMENTDIFFUSION:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flEnvironmentDiffusion);
			break;

		case ::EAXREVERB_ROOM:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.lRoom);
			break;

		case ::EAXREVERB_ROOMHF:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.lRoomHF);
			break;

		case ::EAXREVERB_ROOMLF:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.lRoomLF);
			break;

		case ::EAXREVERB_DECAYTIME:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flDecayTime);
			break;

		case ::EAXREVERB_DECAYHFRATIO:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flDecayHFRatio);
			break;

		case ::EAXREVERB_DECAYLFRATIO:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flDecayLFRatio);
			break;

		case ::EAXREVERB_REFLECTIONS:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.lReflections);
			break;

		case ::EAXREVERB_REFLECTIONSDELAY:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flReflectionsDelay);
			break;

		case ::EAXREVERB_REFLECTIONSPAN:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.vReflectionsPan);
			break;

		case ::EAXREVERB_REVERB:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.lReverb);
			break;

		case ::EAXREVERB_REVERBDELAY:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flReverbDelay);
			break;

		case ::EAXREVERB_REVERBPAN:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.vReverbPan);
			break;

		case ::EAXREVERB_ECHOTIME:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flEchoTime);
			break;

		case ::EAXREVERB_ECHODEPTH:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flEchoDepth);
			break;

		case ::EAXREVERB_MODULATIONTIME:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flModulationTime);
			break;

		case ::EAXREVERB_MODULATIONDEPTH:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flModulationDepth);
			break;

		case ::EAXREVERB_AIRABSORPTIONHF:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flAirAbsorptionHF);
			break;

		case ::EAXREVERB_HFREFERENCE:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flHFReference);
			break;

		case ::EAXREVERB_LFREFERENCE:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flLFReference);
			break;

		case ::EAXREVERB_ROOMROLLOFFFACTOR:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.flRoomRolloffFactor);
			break;

		case ::EAXREVERB_FLAGS:
			eax_call.set_value<EaxxEaxReverbEffectException>(eax_.ulFlags);
			break;

		default:
			throw EaxxEaxReverbEffectException{"Unsupported property id."};
	}
}

void EaxxEaxReverbEffect::validate_environment(
	unsigned long ulEnvironment,
	int version,
	bool is_standalone)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Environment",
		ulEnvironment,
		::EAXREVERB_MINENVIRONMENT,
		(version == 2 || is_standalone) ? ::EAX20REVERB_MAXENVIRONMENT : ::EAX30REVERB_MAXENVIRONMENT
	);
}

void EaxxEaxReverbEffect::validate_environment_size(
	float flEnvironmentSize)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Environment Size",
		flEnvironmentSize,
		::EAXREVERB_MINENVIRONMENTSIZE,
		::EAXREVERB_MAXENVIRONMENTSIZE
	);
}

void EaxxEaxReverbEffect::validate_environment_diffusion(
	float flEnvironmentDiffusion)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Environment Diffusion",
		flEnvironmentDiffusion,
		::EAXREVERB_MINENVIRONMENTDIFFUSION,
		::EAXREVERB_MAXENVIRONMENTDIFFUSION
	);
}

void EaxxEaxReverbEffect::validate_room(
	long lRoom)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Room",
		lRoom,
		::EAXREVERB_MINROOM,
		::EAXREVERB_MAXROOM
	);
}

void EaxxEaxReverbEffect::validate_room_hf(
	long lRoomHF)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Room HF",
		lRoomHF,
		::EAXREVERB_MINROOMHF,
		::EAXREVERB_MAXROOMHF
	);
}

void EaxxEaxReverbEffect::validate_room_lf(
	long lRoomLF)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Room LF",
		lRoomLF,
		::EAXREVERB_MINROOMLF,
		::EAXREVERB_MAXROOMLF
	);
}

void EaxxEaxReverbEffect::validate_decay_time(
	float flDecayTime)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Decay Time",
		flDecayTime,
		::EAXREVERB_MINDECAYTIME,
		::EAXREVERB_MAXDECAYTIME
	);
}

void EaxxEaxReverbEffect::validate_decay_hf_ratio(
	float flDecayHFRatio)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Decay HF Ratio",
		flDecayHFRatio,
		::EAXREVERB_MINDECAYHFRATIO,
		::EAXREVERB_MAXDECAYHFRATIO
	);
}

void EaxxEaxReverbEffect::validate_decay_lf_ratio(
	float flDecayLFRatio)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Decay LF Ratio",
		flDecayLFRatio,
		::EAXREVERB_MINDECAYLFRATIO,
		::EAXREVERB_MAXDECAYLFRATIO
	);
}

void EaxxEaxReverbEffect::validate_reflections(
	long lReflections)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Reflections",
		lReflections,
		::EAXREVERB_MINREFLECTIONS,
		::EAXREVERB_MAXREFLECTIONS
	);
}

void EaxxEaxReverbEffect::validate_reflections_delay(
	float flReflectionsDelay)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Reflections Delay",
		flReflectionsDelay,
		::EAXREVERB_MINREFLECTIONSDELAY,
		::EAXREVERB_MAXREFLECTIONSDELAY
	);
}

void EaxxEaxReverbEffect::validate_reflections_pan(
	const ::EAXVECTOR& vReflectionsPan)
{
	std::ignore = vReflectionsPan;
}

void EaxxEaxReverbEffect::validate_reverb(
	long lReverb)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Reverb",
		lReverb,
		::EAXREVERB_MINREVERB,
		::EAXREVERB_MAXREVERB
	);
}

void EaxxEaxReverbEffect::validate_reverb_delay(
	float flReverbDelay)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Reverb Delay",
		flReverbDelay,
		::EAXREVERB_MINREVERBDELAY,
		::EAXREVERB_MAXREVERBDELAY
	);
}

void EaxxEaxReverbEffect::validate_reverb_pan(
	const ::EAXVECTOR& vReverbPan)
{
	std::ignore = vReverbPan;
}

void EaxxEaxReverbEffect::validate_echo_time(
	float flEchoTime)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Echo Time",
		flEchoTime,
		::EAXREVERB_MINECHOTIME,
		::EAXREVERB_MAXECHOTIME
	);
}

void EaxxEaxReverbEffect::validate_echo_depth(
	float flEchoDepth)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Echo Depth",
		flEchoDepth,
		::EAXREVERB_MINECHODEPTH,
		::EAXREVERB_MAXECHODEPTH
	);
}

void EaxxEaxReverbEffect::validate_modulation_time(
	float flModulationTime)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Modulation Time",
		flModulationTime,
		::EAXREVERB_MINMODULATIONTIME,
		::EAXREVERB_MAXMODULATIONTIME
	);
}

void EaxxEaxReverbEffect::validate_modulation_depth(
	float flModulationDepth)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Modulation Depth",
		flModulationDepth,
		::EAXREVERB_MINMODULATIONDEPTH,
		::EAXREVERB_MAXMODULATIONDEPTH
	);
}

void EaxxEaxReverbEffect::validate_air_absorbtion_hf(
	float air_absorbtion_hf)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Air Absorbtion HF",
		air_absorbtion_hf,
		::EAXREVERB_MINAIRABSORPTIONHF,
		::EAXREVERB_MAXAIRABSORPTIONHF
	);
}

void EaxxEaxReverbEffect::validate_hf_reference(
	float flHFReference)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"HF Reference",
		flHFReference,
		::EAXREVERB_MINHFREFERENCE,
		::EAXREVERB_MAXHFREFERENCE
	);
}

void EaxxEaxReverbEffect::validate_lf_reference(
	float flLFReference)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"LF Reference",
		flLFReference,
		::EAXREVERB_MINLFREFERENCE,
		::EAXREVERB_MAXLFREFERENCE
	);
}

void EaxxEaxReverbEffect::validate_room_rolloff_factor(
	float flRoomRolloffFactor)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Room Rolloff Factor",
		flRoomRolloffFactor,
		::EAXREVERB_MINROOMROLLOFFFACTOR,
		::EAXREVERB_MAXROOMROLLOFFFACTOR
	);
}

void EaxxEaxReverbEffect::validate_flags(
	unsigned long ulFlags)
{
	eaxx_validate_range<EaxxEaxReverbEffectException>(
		"Flags",
		ulFlags,
		0UL,
		~::EAXREVERBFLAGS_RESERVED
	);
}

void EaxxEaxReverbEffect::validate_all(
	const ::EAX20LISTENERPROPERTIES& listener,
	int version)
{
	validate_room(listener.lRoom);
	validate_room_hf(listener.lRoomHF);
	validate_room_rolloff_factor(listener.flRoomRolloffFactor);
	validate_decay_time(listener.flDecayTime);
	validate_decay_hf_ratio(listener.flDecayHFRatio);
	validate_reflections(listener.lReflections);
	validate_reflections_delay(listener.flReflectionsDelay);
	validate_reverb(listener.lReverb);
	validate_reverb_delay(listener.flReverbDelay);
	validate_environment(listener.dwEnvironment, version, false);
	validate_environment_size(listener.flEnvironmentSize);
	validate_environment_diffusion(listener.flEnvironmentDiffusion);
	validate_air_absorbtion_hf(listener.flAirAbsorptionHF);
	validate_flags(listener.dwFlags);
}

void EaxxEaxReverbEffect::validate_all(
	const ::EAXREVERBPROPERTIES& lReverb,
	int version)
{
	validate_environment(lReverb.ulEnvironment, version, false);
	validate_environment_size(lReverb.flEnvironmentSize);
	validate_environment_diffusion(lReverb.flEnvironmentDiffusion);
	validate_room(lReverb.lRoom);
	validate_room_hf(lReverb.lRoomHF);
	validate_room_lf(lReverb.lRoomLF);
	validate_decay_time(lReverb.flDecayTime);
	validate_decay_hf_ratio(lReverb.flDecayHFRatio);
	validate_decay_lf_ratio(lReverb.flDecayLFRatio);
	validate_reflections(lReverb.lReflections);
	validate_reflections_delay(lReverb.flReflectionsDelay);
	validate_reverb(lReverb.lReverb);
	validate_reverb_delay(lReverb.flReverbDelay);
	validate_echo_time(lReverb.flEchoTime);
	validate_echo_depth(lReverb.flEchoDepth);
	validate_modulation_time(lReverb.flModulationTime);
	validate_modulation_depth(lReverb.flModulationDepth);
	validate_air_absorbtion_hf(lReverb.flAirAbsorptionHF);
	validate_hf_reference(lReverb.flHFReference);
	validate_lf_reference(lReverb.flLFReference);
	validate_room_rolloff_factor(lReverb.flRoomRolloffFactor);
	validate_flags(lReverb.ulFlags);
}

void EaxxEaxReverbEffect::defer_environment(
	unsigned long ulEnvironment)
{
	eax_d_.ulEnvironment = ulEnvironment;
	eax_dirty_flags_.ulEnvironment = (eax_.ulEnvironment != eax_d_.ulEnvironment);
}

void EaxxEaxReverbEffect::defer_environment_size(
	float flEnvironmentSize)
{
	eax_d_.flEnvironmentSize = flEnvironmentSize;
	eax_dirty_flags_.flEnvironmentSize = (eax_.flEnvironmentSize != eax_d_.flEnvironmentSize);
}

void EaxxEaxReverbEffect::defer_environment_diffusion(
	float flEnvironmentDiffusion)
{
	eax_d_.flEnvironmentDiffusion = flEnvironmentDiffusion;
	eax_dirty_flags_.flEnvironmentDiffusion = (eax_.flEnvironmentDiffusion != eax_d_.flEnvironmentDiffusion);
}

void EaxxEaxReverbEffect::defer_room(
	long lRoom)
{
	eax_d_.lRoom = lRoom;
	eax_dirty_flags_.lRoom = (eax_.lRoom != eax_d_.lRoom);
}

void EaxxEaxReverbEffect::defer_room_hf(
	long lRoomHF)
{
	eax_d_.lRoomHF = lRoomHF;
	eax_dirty_flags_.lRoomHF = (eax_.lRoomHF != eax_d_.lRoomHF);
}

void EaxxEaxReverbEffect::defer_room_lf(
	long lRoomLF)
{
	eax_d_.lRoomLF = lRoomLF;
	eax_dirty_flags_.lRoomLF = (eax_.lRoomLF != eax_d_.lRoomLF);
}

void EaxxEaxReverbEffect::defer_decay_time(
	float flDecayTime)
{
	eax_d_.flDecayTime = flDecayTime;
	eax_dirty_flags_.flDecayTime = (eax_.flDecayTime != eax_d_.flDecayTime);
}

void EaxxEaxReverbEffect::defer_decay_hf_ratio(
	float flDecayHFRatio)
{
	eax_d_.flDecayHFRatio = flDecayHFRatio;
	eax_dirty_flags_.flDecayHFRatio = (eax_.flDecayHFRatio != eax_d_.flDecayHFRatio);
}

void EaxxEaxReverbEffect::defer_decay_lf_ratio(
	float flDecayLFRatio)
{
	eax_d_.flDecayLFRatio = flDecayLFRatio;
	eax_dirty_flags_.flDecayLFRatio = (eax_.flDecayLFRatio != eax_d_.flDecayLFRatio);
}

void EaxxEaxReverbEffect::defer_reflections(
	long lReflections)
{
	eax_d_.lReflections = lReflections;
	eax_dirty_flags_.lReflections = (eax_.lReflections != eax_d_.lReflections);
}

void EaxxEaxReverbEffect::defer_reflections_delay(
	float flReflectionsDelay)
{
	eax_d_.flReflectionsDelay = flReflectionsDelay;
	eax_dirty_flags_.flReflectionsDelay = (eax_.flReflectionsDelay != eax_d_.flReflectionsDelay);
}

void EaxxEaxReverbEffect::defer_reflections_pan(
	const ::EAXVECTOR& vReflectionsPan)
{
	eax_d_.vReflectionsPan = vReflectionsPan;
	eax_dirty_flags_.vReflectionsPan = (eax_.vReflectionsPan != eax_d_.vReflectionsPan);
}

void EaxxEaxReverbEffect::defer_reverb(
	long lReverb)
{
	eax_d_.lReverb = lReverb;
	eax_dirty_flags_.lReverb = (eax_.lReverb != eax_d_.lReverb);
}

void EaxxEaxReverbEffect::defer_reverb_delay(
	float flReverbDelay)
{
	eax_d_.flReverbDelay = flReverbDelay;
	eax_dirty_flags_.flReverbDelay = (eax_.flReverbDelay != eax_d_.flReverbDelay);
}

void EaxxEaxReverbEffect::defer_reverb_pan(
	const ::EAXVECTOR& vReverbPan)
{
	eax_d_.vReverbPan = vReverbPan;
	eax_dirty_flags_.vReverbPan = (eax_.vReverbPan != eax_d_.vReverbPan);
}

void EaxxEaxReverbEffect::defer_echo_time(
	float flEchoTime)
{
	eax_d_.flEchoTime = flEchoTime;
	eax_dirty_flags_.flEchoTime = (eax_.flEchoTime != eax_d_.flEchoTime);
}

void EaxxEaxReverbEffect::defer_echo_depth(
	float flEchoDepth)
{
	eax_d_.flEchoDepth = flEchoDepth;
	eax_dirty_flags_.flEchoDepth = (eax_.flEchoDepth != eax_d_.flEchoDepth);
}

void EaxxEaxReverbEffect::defer_modulation_time(
	float flModulationTime)
{
	eax_d_.flModulationTime = flModulationTime;
	eax_dirty_flags_.flModulationTime = (eax_.flModulationTime != eax_d_.flModulationTime);
}

void EaxxEaxReverbEffect::defer_modulation_depth(
	float flModulationDepth)
{
	eax_d_.flModulationDepth = flModulationDepth;
	eax_dirty_flags_.flModulationDepth = (eax_.flModulationDepth != eax_d_.flModulationDepth);
}

void EaxxEaxReverbEffect::defer_air_absorbtion_hf(
	float flAirAbsorptionHF)
{
	eax_d_.flAirAbsorptionHF = flAirAbsorptionHF;
	eax_dirty_flags_.flAirAbsorptionHF = (eax_.flAirAbsorptionHF != eax_d_.flAirAbsorptionHF);
}

void EaxxEaxReverbEffect::defer_hf_reference(
	float flHFReference)
{
	eax_d_.flHFReference = flHFReference;
	eax_dirty_flags_.flHFReference = (eax_.flHFReference != eax_d_.flHFReference);
}

void EaxxEaxReverbEffect::defer_lf_reference(
	float flLFReference)
{
	eax_d_.flLFReference = flLFReference;
	eax_dirty_flags_.flLFReference = (eax_.flLFReference != eax_d_.flLFReference);
}

void EaxxEaxReverbEffect::defer_room_rolloff_factor(
	float flRoomRolloffFactor)
{
	eax_d_.flRoomRolloffFactor = flRoomRolloffFactor;
	eax_dirty_flags_.flRoomRolloffFactor = (eax_.flRoomRolloffFactor != eax_d_.flRoomRolloffFactor);
}

void EaxxEaxReverbEffect::defer_flags(
	unsigned long ulFlags)
{
	eax_d_.ulFlags = ulFlags;
	eax_dirty_flags_.ulFlags = (eax_.ulFlags != eax_d_.ulFlags);
}

void EaxxEaxReverbEffect::defer_all(
	const ::EAX20LISTENERPROPERTIES& listener)
{
	defer_room(listener.lRoom);
	defer_room_hf(listener.lRoomHF);
	defer_room_rolloff_factor(listener.flRoomRolloffFactor);
	defer_decay_time(listener.flDecayTime);
	defer_decay_hf_ratio(listener.flDecayHFRatio);
	defer_reflections(listener.lReflections);
	defer_reflections_delay(listener.flReflectionsDelay);
	defer_reverb(listener.lReverb);
	defer_reverb_delay(listener.flReverbDelay);
	defer_environment(listener.dwEnvironment);
	defer_environment_size(listener.flEnvironmentSize);
	defer_environment_diffusion(listener.flEnvironmentDiffusion);
	defer_air_absorbtion_hf(listener.flAirAbsorptionHF);
	defer_flags(listener.dwFlags);
}

void EaxxEaxReverbEffect::defer_all(
	const ::EAXREVERBPROPERTIES& lReverb)
{
	defer_environment(lReverb.ulEnvironment);
	defer_environment_size(lReverb.flEnvironmentSize);
	defer_environment_diffusion(lReverb.flEnvironmentDiffusion);
	defer_room(lReverb.lRoom);
	defer_room_hf(lReverb.lRoomHF);
	defer_room_lf(lReverb.lRoomLF);
	defer_decay_time(lReverb.flDecayTime);
	defer_decay_hf_ratio(lReverb.flDecayHFRatio);
	defer_decay_lf_ratio(lReverb.flDecayLFRatio);
	defer_reflections(lReverb.lReflections);
	defer_reflections_delay(lReverb.flReflectionsDelay);
	defer_reflections_pan(lReverb.vReflectionsPan);
	defer_reverb(lReverb.lReverb);
	defer_reverb_delay(lReverb.flReverbDelay);
	defer_reverb_pan(lReverb.vReverbPan);
	defer_echo_time(lReverb.flEchoTime);
	defer_echo_depth(lReverb.flEchoDepth);
	defer_modulation_time(lReverb.flModulationTime);
	defer_modulation_depth(lReverb.flModulationDepth);
	defer_air_absorbtion_hf(lReverb.flAirAbsorptionHF);
	defer_hf_reference(lReverb.flHFReference);
	defer_lf_reference(lReverb.flLFReference);
	defer_room_rolloff_factor(lReverb.flRoomRolloffFactor);
	defer_flags(lReverb.ulFlags);
}

void EaxxEaxReverbEffect::defer_environment(
	const EaxxEaxCall& eax_call)
{
	const auto& ulEnvironment =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::ulEnvironment)>();

	validate_environment(ulEnvironment, eax_call.get_version(), true);

	if (eax_d_.ulEnvironment == ulEnvironment)
	{
		return;
	}

	const auto& reverb_preset = ::EAXREVERB_PRESETS[ulEnvironment];

	defer_all(reverb_preset);
}

void EaxxEaxReverbEffect::defer_environment_size(
	const EaxxEaxCall& eax_call)
{
	const auto& flEnvironmentSize =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flEnvironmentSize)>();

	validate_environment_size(flEnvironmentSize);

	if (eax_d_.flEnvironmentSize == flEnvironmentSize)
	{
		return;
	}

	const auto scale = flEnvironmentSize / eax_d_.flEnvironmentSize;

	defer_environment_size(flEnvironmentSize);

	if ((eax_d_.ulFlags & ::EAXREVERBFLAGS_DECAYTIMESCALE) != 0)
	{
		const auto flDecayTime = clamp(
			scale * eax_d_.flDecayTime,
			::EAXREVERB_MINDECAYTIME,
			::EAXREVERB_MAXDECAYTIME
		);

		defer_decay_time(flDecayTime);
	}

	if ((eax_d_.ulFlags & ::EAXREVERBFLAGS_REFLECTIONSSCALE) != 0)
	{
		if ((eax_d_.ulFlags & ::EAXREVERBFLAGS_REFLECTIONSDELAYSCALE) != 0)
		{
			const auto lReflections = clamp(
				eax_d_.lReflections - static_cast<long>(gain_to_level_mb(scale)),
				::EAXREVERB_MINREFLECTIONS,
				::EAXREVERB_MAXREFLECTIONS
			);

			defer_reflections(lReflections);
		}
	}

	if ((eax_d_.ulFlags & ::EAXREVERBFLAGS_REFLECTIONSDELAYSCALE) != 0)
	{
		const auto flReflectionsDelay = clamp(
			eax_d_.flReflectionsDelay * scale,
			::EAXREVERB_MINREFLECTIONSDELAY,
			::EAXREVERB_MAXREFLECTIONSDELAY
		);

		defer_reflections_delay(flReflectionsDelay);
	}

	if ((eax_d_.ulFlags & ::EAXREVERBFLAGS_REVERBSCALE) != 0)
	{
		const auto log_scalar = ((eax_d_.ulFlags & ::EAXREVERBFLAGS_DECAYTIMESCALE) != 0) ? 2'000.0F : 3'000.0F;

		const auto lReverb = clamp(
			eax_d_.lReverb - static_cast<long>(std::log10(scale) * log_scalar),
			::EAXREVERB_MINREVERB,
			::EAXREVERB_MAXREVERB
		);

		defer_reverb(lReverb);
	}

	if ((eax_d_.ulFlags & ::EAXREVERBFLAGS_REVERBDELAYSCALE) != 0)
	{
		const auto flReverbDelay = clamp(
			scale * eax_d_.flReverbDelay,
			::EAXREVERB_MINREVERBDELAY,
			::EAXREVERB_MAXREVERBDELAY
		);

		defer_reverb_delay(flReverbDelay);
	}

	if ((eax_d_.ulFlags & ::EAXREVERBFLAGS_ECHOTIMESCALE) != 0)
	{
		const auto flEchoTime = clamp(
			eax_d_.flEchoTime * scale,
			::EAXREVERB_MINECHOTIME,
			::EAXREVERB_MAXECHOTIME
		);

		defer_echo_time(flEchoTime);
	}

	if ((eax_d_.ulFlags & ::EAXREVERBFLAGS_MODULATIONTIMESCALE) != 0)
	{
		const auto flModulationTime = clamp(
			scale * eax_d_.flModulationTime,
			::EAXREVERB_MINMODULATIONTIME,
			::EAXREVERB_MAXMODULATIONTIME
		);

		defer_modulation_time(flModulationTime);
	}
}

void EaxxEaxReverbEffect::defer_environment_diffusion(
		const EaxxEaxCall& eax_call)
{
	const auto& flEnvironmentDiffusion =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flEnvironmentDiffusion)>();

	validate_environment_diffusion(flEnvironmentDiffusion);
	defer_environment_diffusion(flEnvironmentDiffusion);
}

void EaxxEaxReverbEffect::defer_room(
	const EaxxEaxCall& eax_call)
{
	const auto& lRoom =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::lRoom)>();

	validate_room(lRoom);
	defer_room(lRoom);
}

void EaxxEaxReverbEffect::defer_room_hf(
	const EaxxEaxCall& eax_call)
{
	const auto& lRoomHF =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::lRoomHF)>();

	validate_room_hf(lRoomHF);
	defer_room_hf(lRoomHF);
}

void EaxxEaxReverbEffect::defer_room_lf(
	const EaxxEaxCall& eax_call)
{
	const auto& lRoomLF =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::lRoomLF)>();

	validate_room_lf(lRoomLF);
	defer_room_lf(lRoomLF);
}

void EaxxEaxReverbEffect::defer_decay_time(
	const EaxxEaxCall& eax_call)
{
	const auto& flDecayTime =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flDecayTime)>();

	validate_decay_time(flDecayTime);
	defer_decay_time(flDecayTime);
}

void EaxxEaxReverbEffect::defer_decay_hf_ratio(
	const EaxxEaxCall& eax_call)
{
	const auto& flDecayHFRatio =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flDecayHFRatio)>();

	validate_decay_hf_ratio(flDecayHFRatio);
	defer_decay_hf_ratio(flDecayHFRatio);
}

void EaxxEaxReverbEffect::defer_decay_lf_ratio(
	const EaxxEaxCall& eax_call)
{
	const auto& flDecayLFRatio =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flDecayLFRatio)>();

	validate_decay_lf_ratio(flDecayLFRatio);
	defer_decay_lf_ratio(flDecayLFRatio);
}

void EaxxEaxReverbEffect::defer_reflections(
	const EaxxEaxCall& eax_call)
{
	const auto& lReflections =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::lReflections)>();

	validate_reflections(lReflections);
	defer_reflections(lReflections);
}

void EaxxEaxReverbEffect::defer_reflections_delay(
	const EaxxEaxCall& eax_call)
{
	const auto& flReflectionsDelay =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flReflectionsDelay)>();

	validate_reflections_delay(flReflectionsDelay);
	defer_reflections_delay(flReflectionsDelay);
}

void EaxxEaxReverbEffect::defer_reflections_pan(
	const EaxxEaxCall& eax_call)
{
	const auto& vReflectionsPan =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::vReflectionsPan)>();

	validate_reflections_pan(vReflectionsPan);
	defer_reflections_pan(vReflectionsPan);
}

void EaxxEaxReverbEffect::defer_reverb(
	const EaxxEaxCall& eax_call)
{
	const auto& lReverb =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::lReverb)>();

	validate_reverb(lReverb);
	defer_reverb(lReverb);
}

void EaxxEaxReverbEffect::defer_reverb_delay(
	const EaxxEaxCall& eax_call)
{
	const auto& flReverbDelay =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flReverbDelay)>();

	validate_reverb_delay(flReverbDelay);
	defer_reverb_delay(flReverbDelay);
}

void EaxxEaxReverbEffect::defer_reverb_pan(
	const EaxxEaxCall& eax_call)
{
	const auto& vReverbPan =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::vReverbPan)>();

	validate_reverb_pan(vReverbPan);
	defer_reverb_pan(vReverbPan);
}

void EaxxEaxReverbEffect::defer_echo_time(
	const EaxxEaxCall& eax_call)
{
	const auto& flEchoTime =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flEchoTime)>();

	validate_echo_time(flEchoTime);
	defer_echo_time(flEchoTime);
}

void EaxxEaxReverbEffect::defer_echo_depth(
	const EaxxEaxCall& eax_call)
{
	const auto& flEchoDepth =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flEchoDepth)>();

	validate_echo_depth(flEchoDepth);
	defer_echo_depth(flEchoDepth);
}

void EaxxEaxReverbEffect::defer_modulation_time(
	const EaxxEaxCall& eax_call)
{
	const auto& flModulationTime =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flModulationTime)>();

	validate_modulation_time(flModulationTime);
	defer_modulation_time(flModulationTime);
}

void EaxxEaxReverbEffect::defer_modulation_depth(
	const EaxxEaxCall& eax_call)
{
	const auto& flModulationDepth =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flModulationDepth)>();

	validate_modulation_depth(flModulationDepth);
	defer_modulation_depth(flModulationDepth);
}

void EaxxEaxReverbEffect::defer_air_absorbtion_hf(
	const EaxxEaxCall& eax_call)
{
	const auto& air_absorbtion_hf =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flAirAbsorptionHF)>();

	validate_air_absorbtion_hf(air_absorbtion_hf);
	defer_air_absorbtion_hf(air_absorbtion_hf);
}

void EaxxEaxReverbEffect::defer_hf_reference(
	const EaxxEaxCall& eax_call)
{
	const auto& flHFReference =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flHFReference)>();

	validate_hf_reference(flHFReference);
	defer_hf_reference(flHFReference);
}

void EaxxEaxReverbEffect::defer_lf_reference(
	const EaxxEaxCall& eax_call)
{
	const auto& flLFReference =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flLFReference)>();

	validate_lf_reference(flLFReference);
	defer_lf_reference(flLFReference);
}

void EaxxEaxReverbEffect::defer_room_rolloff_factor(
	const EaxxEaxCall& eax_call)
{
	const auto& flRoomRolloffFactor =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::flRoomRolloffFactor)>();

	validate_room_rolloff_factor(flRoomRolloffFactor);
	defer_room_rolloff_factor(flRoomRolloffFactor);
}

void EaxxEaxReverbEffect::defer_flags(
	const EaxxEaxCall& eax_call)
{
	const auto& ulFlags =
		eax_call.get_value<EaxxEaxReverbEffectException, const decltype(::EAXREVERBPROPERTIES::ulFlags)>();

	validate_flags(ulFlags);
	defer_flags(ulFlags);
}

void EaxxEaxReverbEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto eax_version = eax_call.get_version();

	if (eax_version == 2)
	{
		const auto& listener =
			eax_call.get_value<EaxxEaxReverbEffectException, const ::EAX20LISTENERPROPERTIES>();

		validate_all(listener, eax_version);
		defer_all(listener);
	}
	else
	{
		const auto& reverb_all =
			eax_call.get_value<EaxxEaxReverbEffectException, const ::EAXREVERBPROPERTIES>();

		validate_all(reverb_all, eax_version);
		defer_all(reverb_all);
	}
}

void EaxxEaxReverbEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxEaxReverbEffectDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.ulEnvironment)
	{
	}

	if (eax_dirty_flags_.flEnvironmentSize)
	{
		set_efx_density();
	}

	if (eax_dirty_flags_.flEnvironmentDiffusion)
	{
		set_efx_diffusion();
	}

	if (eax_dirty_flags_.lRoom)
	{
		set_efx_gain();
	}

	if (eax_dirty_flags_.lRoomHF)
	{
		set_efx_gain_hf();
	}

	if (eax_dirty_flags_.lRoomLF)
	{
		set_efx_gain_lf();
	}

	if (eax_dirty_flags_.flDecayTime)
	{
		set_efx_decay_time();
	}

	if (eax_dirty_flags_.flDecayHFRatio)
	{
		set_efx_decay_hf_ratio();
	}

	if (eax_dirty_flags_.flDecayLFRatio)
	{
		set_efx_decay_lf_ratio();
	}

	if (eax_dirty_flags_.lReflections)
	{
		set_efx_reflections_gain();
	}

	if (eax_dirty_flags_.flReflectionsDelay)
	{
		set_efx_reflections_delay();
	}

	if (eax_dirty_flags_.vReflectionsPan)
	{
		set_efx_reflections_pan();
	}

	if (eax_dirty_flags_.lReverb)
	{
		set_efx_late_reverb_gain();
	}

	if (eax_dirty_flags_.flReverbDelay)
	{
		set_efx_late_reverb_delay();
	}

	if (eax_dirty_flags_.vReverbPan)
	{
		set_efx_late_reverb_pan();
	}

	if (eax_dirty_flags_.flEchoTime)
	{
		set_efx_echo_time();
	}

	if (eax_dirty_flags_.flEchoDepth)
	{
		set_efx_echo_depth();
	}

	if (eax_dirty_flags_.flModulationTime)
	{
		set_efx_modulation_time();
	}

	if (eax_dirty_flags_.flModulationDepth)
	{
		set_efx_modulation_depth();
	}

	if (eax_dirty_flags_.flAirAbsorptionHF)
	{
		set_efx_air_absorption_gain_hf();
	}

	if (eax_dirty_flags_.flHFReference)
	{
		set_efx_hf_reference();
	}

	if (eax_dirty_flags_.flLFReference)
	{
		set_efx_lf_reference();
	}

	if (eax_dirty_flags_.flRoomRolloffFactor)
	{
		set_efx_room_rolloff_factor();
	}

	if (eax_dirty_flags_.ulFlags)
	{
		set_efx_flags();
	}

	eax_dirty_flags_ = EaxxEaxReverbEffectDirtyFlags{};

	load();
}

void EaxxEaxReverbEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXREVERB_NONE:
			break;

		case ::EAXREVERB_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXREVERB_ENVIRONMENT:
			defer_environment(eax_call);
			break;

		case ::EAXREVERB_ENVIRONMENTSIZE:
			defer_environment_size(eax_call);
			break;

		case ::EAXREVERB_ENVIRONMENTDIFFUSION:
			defer_environment_diffusion(eax_call);
			break;

		case ::EAXREVERB_ROOM:
			defer_room(eax_call);
			break;

		case ::EAXREVERB_ROOMHF:
			defer_room_hf(eax_call);
			break;

		case ::EAXREVERB_ROOMLF:
			defer_room_lf(eax_call);
			break;

		case ::EAXREVERB_DECAYTIME:
			defer_decay_time(eax_call);
			break;

		case ::EAXREVERB_DECAYHFRATIO:
			defer_decay_hf_ratio(eax_call);
			break;

		case ::EAXREVERB_DECAYLFRATIO:
			defer_decay_lf_ratio(eax_call);
			break;

		case ::EAXREVERB_REFLECTIONS:
			defer_reflections(eax_call);
			break;

		case ::EAXREVERB_REFLECTIONSDELAY:
			defer_reflections_delay(eax_call);
			break;

		case ::EAXREVERB_REFLECTIONSPAN:
			defer_reflections_pan(eax_call);
			break;

		case ::EAXREVERB_REVERB:
			defer_reverb(eax_call);
			break;

		case ::EAXREVERB_REVERBDELAY:
			defer_reverb_delay(eax_call);
			break;

		case ::EAXREVERB_REVERBPAN:
			defer_reverb_pan(eax_call);
			break;

		case ::EAXREVERB_ECHOTIME:
			defer_echo_time(eax_call);
			break;

		case ::EAXREVERB_ECHODEPTH:
			defer_echo_depth(eax_call);
			break;

		case ::EAXREVERB_MODULATIONTIME:
			defer_modulation_time(eax_call);
			break;

		case ::EAXREVERB_MODULATIONDEPTH:
			defer_modulation_depth(eax_call);
			break;

		case ::EAXREVERB_AIRABSORPTIONHF:
			defer_air_absorbtion_hf(eax_call);
			break;

		case ::EAXREVERB_HFREFERENCE:
			defer_hf_reference(eax_call);
			break;

		case ::EAXREVERB_LFREFERENCE:
			defer_lf_reference(eax_call);
			break;

		case ::EAXREVERB_ROOMROLLOFFFACTOR:
			defer_room_rolloff_factor(eax_call);
			break;

		case ::EAXREVERB_FLAGS:
			defer_flags(eax_call);
			break;

		default:
			throw EaxxEaxReverbEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
