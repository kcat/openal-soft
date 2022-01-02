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


#ifndef EAX_EAXX_FX_SLOT_INCLUDED
#define EAX_EAXX_FX_SLOT_INCLUDED


#include "AL/al.h"

#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_effect.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxFxSlot
{
public:
	void initialize(
		int index);


	void activate_default_reverb_effect();


	::ALuint get_efx_effect_slot() const noexcept;

	const EAX50FXSLOTPROPERTIES& get_eax_fx_slot() const noexcept;


	// [[nodiscard]]
	bool dispatch(
		const EaxxEaxCall& eax_call);


private:
	struct Eax
	{
		EAX50FXSLOTPROPERTIES fx_slot{};
	}; // Eax

	struct Efx
	{
		EfxEffectSlotObject effect_slot{};
	}; // Efx


	int index_{};

	Eax eax_{};
	Efx efx_{};

	EaxxEffect* effect_{};

	EaxxEffectUPtr null_effect_;
	EaxxEffectUPtr auto_wah_effect_;
	EaxxEffectUPtr chorus_effect_;
	EaxxEffectUPtr compressor_effect_;
	EaxxEffectUPtr distortion_effect_;
	EaxxEffectUPtr eax_reverb_effect_;
	EaxxEffectUPtr echo_effect_;
	EaxxEffectUPtr equalizer_effect_;
	EaxxEffectUPtr flanger_effect_;
	EaxxEffectUPtr frequency_shifter_effect_;
	EaxxEffectUPtr pitch_shifter_effect_;
	EaxxEffectUPtr ring_modulator_effect_;
	EaxxEffectUPtr vocal_morpher_effect_;


	[[noreturn]]
	static void fail(
		const char* message);


	void set_eax_fx_slot_defaults();

	void initialize_eax();


	void create_efx_effect_slot();

	void create_efx_objects();

	void initialize_efx();


	EaxxEffectUPtr create_effect(
		EaxxEffectType effect_type);

	void initialize_effects();


	void set_default_slot_0_defaults();

	void set_default_slot_1_defaults();

	void set_default_slots_defaults();


	void set_null_effect();


	void get_fx_slot_all(
		const EaxxEaxCall& eax_call) const;

	void get_fx_slot(
		const EaxxEaxCall& eax_call) const;

	void get(
		const EaxxEaxCall& eax_call);


	void set_fx_slot_effect(
		EaxxEffectUPtr& effect);

	void set_fx_slot_effect(
		EaxxEffectType effect_type,
		EaxxEffectUPtr& effect);

	void set_fx_slot_effect();


	void set_efx_effect_slot_gain();

	void set_fx_slot_volume();


	void set_effect_slot_send_auto();

	void set_fx_slot_flags();


	void validate_fx_slot_effect(
		const ::GUID& eax_effect_id);

	void validate_fx_slot_volume(
		long eax_volume);

	void validate_fx_slot_lock(
		long eax_lock);

	void validate_fx_slot_lock_state(
		long eax_lock,
		const ::GUID& eax_effect_id);

	void validate_fx_slot_flags(
		unsigned long eax_flags,
		int eax_version);

	void validate_fx_slot_occlusion(
		long eax_occlusion);

	void validate_fx_slot_occlusion_lf_ratio(
		float eax_occlusion_lf_ratio);

	void validate_fx_slot_all(
		const EAX40FXSLOTPROPERTIES& fx_slot,
		int eax_version);

	void validate_fx_slot_all(
		const EAX50FXSLOTPROPERTIES& fx_slot,
		int eax_version);


	void set_fx_slot_effect(
		const ::GUID& eax_effect_id);

	void set_fx_slot_volume(
		long eax_volume);

	void set_fx_slot_lock(
		long eax_lock);

	void set_fx_slot_flags(
		unsigned long eax_flags);

	// [[nodiscard]]
	bool set_fx_slot_occlusion(
		long eax_occlusion);

	// [[nodiscard]]
	bool set_fx_slot_occlusion_lf_ratio(
		float eax_occlusion_lf_ratio);

	void set_fx_slot_all(
		const EAX40FXSLOTPROPERTIES& eax_fx_slot);

	// [[nodiscard]]
	bool set_fx_slot_all(
		const EAX50FXSLOTPROPERTIES& eax_fx_slot);


	void set_fx_slot_effect(
		const EaxxEaxCall& eax_call);

	void set_fx_slot_volume(
		const EaxxEaxCall& eax_call);

	void set_fx_slot_lock(
		const EaxxEaxCall& eax_call);

	void set_fx_slot_flags(
		const EaxxEaxCall& eax_call);

	// [[nodiscard]]
	bool set_fx_slot_occlusion(
		const EaxxEaxCall& eax_call);

	// [[nodiscard]]
	bool set_fx_slot_occlusion_lf_ratio(
		const EaxxEaxCall& eax_call);

	// [[nodiscard]]
	bool set_fx_slot_all(
		const EaxxEaxCall& eax_call);

	bool set_fx_slot(
		const EaxxEaxCall& eax_call);

	// [[nodiscard]]
	bool set(
		const EaxxEaxCall& eax_call);


	void dispatch_effect(
		const EaxxEaxCall& eax_call);
}; // EaxxFxSlot

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_FX_SLOT_INCLUDED
