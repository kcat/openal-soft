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


#ifndef EAX_EAXX_EAX_REVERB_EFFECT_INCLUDED
#define EAX_EAXX_EAX_REVERB_EFFECT_INCLUDED


#include <cstdint>

#include "AL/al.h"

#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_effect.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

struct EaxxEaxReverbEffectDirtyFlags
{
	using Value = unsigned int;


	Value ulEnvironment : 1;
	Value flEnvironmentSize : 1;
	Value flEnvironmentDiffusion : 1;
	Value lRoom : 1;
	Value lRoomHF : 1;
	Value lRoomLF : 1;
	Value flDecayTime : 1;
	Value flDecayHFRatio : 1;
	Value flDecayLFRatio : 1;
	Value lReflections : 1;
	Value flReflectionsDelay : 1;
	Value vReflectionsPan : 1;
	Value lReverb : 1;
	Value flReverbDelay : 1;
	Value vReverbPan : 1;
	Value flEchoTime : 1;
	Value flEchoDepth : 1;
	Value flModulationTime : 1;
	Value flModulationDepth : 1;
	Value flAirAbsorptionHF : 1;
	Value flHFReference : 1;
	Value flLFReference : 1;
	Value flRoomRolloffFactor : 1;
	Value ulFlags : 1;
}; // EaxxEaxReverbEffectDirtyFlags

bool operator==(
	const EaxxEaxReverbEffectDirtyFlags& lhs,
	const EaxxEaxReverbEffectDirtyFlags& rhs) noexcept;

static_assert(sizeof(EaxxEaxReverbEffectDirtyFlags) == sizeof(EaxxEaxReverbEffectDirtyFlags::Value), "Type size.");

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEaxReverbEffect final :
	public EaxxEffect
{
public:
	EaxxEaxReverbEffect(
		::ALuint al_effect_slot);


	// ----------------------------------------------------------------------
	// Effect

	void load() override;

	void dispatch(
		const EaxxEaxCall& eax_call) override;

	// Effect
	// ----------------------------------------------------------------------


private:
	::ALuint al_effect_slot_;
	EfxEffectObject efx_effect_object_;

	::EAXREVERBPROPERTIES eax_;
	::EAXREVERBPROPERTIES eax_d_;
	EaxxEaxReverbEffectDirtyFlags eax_dirty_flags_{};


	void set_eax_defaults();


	void set_efx_density();

	void set_efx_diffusion();

	void set_efx_gain();

	void set_efx_gain_hf();

	void set_efx_gain_lf();

	void set_efx_decay_time();

	void set_efx_decay_hf_ratio();

	void set_efx_decay_lf_ratio();

	void set_efx_reflections_gain();

	void set_efx_reflections_delay();

	void set_efx_reflections_pan();

	void set_efx_late_reverb_gain();

	void set_efx_late_reverb_delay();

	void set_efx_late_reverb_pan();

	void set_efx_echo_time();

	void set_efx_echo_depth();

	void set_efx_modulation_time();

	void set_efx_modulation_depth();

	void set_efx_air_absorption_gain_hf();

	void set_efx_hf_reference();

	void set_efx_lf_reference();

	void set_efx_room_rolloff_factor();

	void set_efx_flags();

	void set_efx_defaults();


	void get_all(
		const EaxxEaxCall& eax_call) const;

	void get(
		const EaxxEaxCall& eax_call) const;


	void validate_environment(
		unsigned long ulEnvironment,
		int version,
		bool is_standalone);

	void validate_environment_size(
		float flEnvironmentSize);

	void validate_environment_diffusion(
		 float flEnvironmentDiffusion);

	void validate_room(
		long lRoom);

	void validate_room_hf(
		long lRoomHF);

	void validate_room_lf(
		long lRoomLF);

	void validate_decay_time(
		float flDecayTime);

	void validate_decay_hf_ratio(
		float flDecayHFRatio);

	void validate_decay_lf_ratio(
		float flDecayLFRatio);

	void validate_reflections(
		long lReflections);

	void validate_reflections_delay(
		float flReflectionsDelay);

	void validate_reflections_pan(
		const ::EAXVECTOR& vReflectionsPan);

	void validate_reverb(
		long lReverb);

	void validate_reverb_delay(
		float flReverbDelay);

	void validate_reverb_pan(
		const ::EAXVECTOR& vReverbPan);

	void validate_echo_time(
		float flEchoTime);

	void validate_echo_depth(
		float flEchoDepth);

	void validate_modulation_time(
		float flModulationTime);

	void validate_modulation_depth(
		float flModulationDepth);

	void validate_air_absorbtion_hf(
		float air_absorbtion_hf);

	void validate_hf_reference(
		float flHFReference);

	void validate_lf_reference(
		float flLFReference);

	void validate_room_rolloff_factor(
		float flRoomRolloffFactor);

	void validate_flags(
		unsigned long ulFlags);

	void validate_all(
		const ::EAX20LISTENERPROPERTIES& all,
		int version);

	void validate_all(
		const ::EAXREVERBPROPERTIES& all,
		int version);


	void defer_environment(
		unsigned long ulEnvironment);

	void defer_environment_size(
		float flEnvironmentSize);

	void defer_environment_diffusion(
		 float flEnvironmentDiffusion);

	void defer_room(
		long lRoom);

	void defer_room_hf(
		long lRoomHF);

	void defer_room_lf(
		long lRoomLF);

	void defer_decay_time(
		float flDecayTime);

	void defer_decay_hf_ratio(
		float flDecayHFRatio);

	void defer_decay_lf_ratio(
		float flDecayLFRatio);

	void defer_reflections(
		long lReflections);

	void defer_reflections_delay(
		float flReflectionsDelay);

	void defer_reflections_pan(
		const ::EAXVECTOR& vReflectionsPan);

	void defer_reverb(
		long lReverb);

	void defer_reverb_delay(
		float flReverbDelay);

	void defer_reverb_pan(
		const ::EAXVECTOR& vReverbPan);

	void defer_echo_time(
		float flEchoTime);

	void defer_echo_depth(
		float flEchoDepth);

	void defer_modulation_time(
		float flModulationTime);

	void defer_modulation_depth(
		float flModulationDepth);

	void defer_air_absorbtion_hf(
		float flAirAbsorptionHF);

	void defer_hf_reference(
		float flHFReference);

	void defer_lf_reference(
		float flLFReference);

	void defer_room_rolloff_factor(
		float flRoomRolloffFactor);

	void defer_flags(
		unsigned long ulFlags);

	void defer_all(
		const ::EAX20LISTENERPROPERTIES& all);

	void defer_all(
		const ::EAXREVERBPROPERTIES& all);


	void defer_environment(
		const EaxxEaxCall& eax_call);

	void defer_environment_size(
		const EaxxEaxCall& eax_call);

	void defer_environment_diffusion(
		 const EaxxEaxCall& eax_call);

	void defer_room(
		const EaxxEaxCall& eax_call);

	void defer_room_hf(
		const EaxxEaxCall& eax_call);

	void defer_room_lf(
		const EaxxEaxCall& eax_call);

	void defer_decay_time(
		const EaxxEaxCall& eax_call);

	void defer_decay_hf_ratio(
		const EaxxEaxCall& eax_call);

	void defer_decay_lf_ratio(
		const EaxxEaxCall& eax_call);

	void defer_reflections(
		const EaxxEaxCall& eax_call);

	void defer_reflections_delay(
		const EaxxEaxCall& eax_call);

	void defer_reflections_pan(
		const EaxxEaxCall& eax_call);

	void defer_reverb(
		const EaxxEaxCall& eax_call);

	void defer_reverb_delay(
		const EaxxEaxCall& eax_call);

	void defer_reverb_pan(
		const EaxxEaxCall& eax_call);

	void defer_echo_time(
		const EaxxEaxCall& eax_call);

	void defer_echo_depth(
		const EaxxEaxCall& eax_call);

	void defer_modulation_time(
		const EaxxEaxCall& eax_call);

	void defer_modulation_depth(
		const EaxxEaxCall& eax_call);

	void defer_air_absorbtion_hf(
		const EaxxEaxCall& eax_call);

	void defer_hf_reference(
		const EaxxEaxCall& eax_call);

	void defer_lf_reference(
		const EaxxEaxCall& eax_call);

	void defer_room_rolloff_factor(
		const EaxxEaxCall& eax_call);

	void defer_flags(
		const EaxxEaxCall& eax_call);

	void defer_all(
		const EaxxEaxCall& eax_call);


	void apply_deferred();

	void set(
		const EaxxEaxCall& eax_call);
}; // EaxxEaxReverbEffect

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_EAX_REVERB_EFFECT_INCLUDED
