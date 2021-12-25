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


#ifndef EAX_EAXX_FLANGER_EFFECT_INCLUDED
#define EAX_EAXX_FLANGER_EFFECT_INCLUDED


#include <cstdint>

#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_effect.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using EaxxFlangerEffectEaxDirtyFlagsValue = unsigned int;

struct EaxxFlangerEffectEaxDirtyFlags
{
	EaxxFlangerEffectEaxDirtyFlagsValue ulWaveform : 1;
	EaxxFlangerEffectEaxDirtyFlagsValue lPhase : 1;
	EaxxFlangerEffectEaxDirtyFlagsValue flRate : 1;
	EaxxFlangerEffectEaxDirtyFlagsValue flDepth : 1;
	EaxxFlangerEffectEaxDirtyFlagsValue flFeedback : 1;
	EaxxFlangerEffectEaxDirtyFlagsValue flDelay : 1;
}; // EaxxFlangerEffectEaxDirtyFlags

static_assert(sizeof(EaxxFlangerEffectEaxDirtyFlags) == sizeof(EaxxFlangerEffectEaxDirtyFlagsValue), "Type size.");

bool operator==(
	const EaxxFlangerEffectEaxDirtyFlags& lhs,
	const EaxxFlangerEffectEaxDirtyFlags& rhs) noexcept;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxFlangerEffect final :
	public EaxxEffect
{
public:
	EaxxFlangerEffect(
		::ALuint al_effect_slot);


	void load() override;

	void dispatch(
		const EaxxEaxCall& eax_call) override;


private:
	const ::ALuint al_effect_slot_;
	EfxEffectObject efx_effect_object_;

	::EAXFLANGERPROPERTIES eax_;
	::EAXFLANGERPROPERTIES eax_d_;
	EaxxFlangerEffectEaxDirtyFlags eax_dirty_flags_{};


	void set_eax_defaults();


	void set_efx_waveform();

	void set_efx_phase();

	void set_efx_rate();

	void set_efx_depth();

	void set_efx_feedback();

	void set_efx_delay();

	void set_efx_defaults();


	void get(
		const EaxxEaxCall& eax_call);


	void validate_waveform(
		unsigned long ulWaveform);

	void validate_phase(
		long lPhase);

	void validate_rate(
		float flRate);

	void validate_depth(
		float flDepth);

	void validate_feedback(
		float flFeedback);

	void validate_delay(
		float flDelay);

	void validate_all(
		const ::EAXFLANGERPROPERTIES& all);


	void defer_waveform(
		unsigned long ulWaveform);

	void defer_phase(
		long lPhase);

	void defer_rate(
		float flRate);

	void defer_depth(
		float flDepth);

	void defer_feedback(
		float flFeedback);

	void defer_delay(
		float flDelay);

	void defer_all(
		const ::EAXFLANGERPROPERTIES& all);


	void defer_waveform(
		const EaxxEaxCall& eax_call);

	void defer_phase(
		const EaxxEaxCall& eax_call);

	void defer_rate(
		const EaxxEaxCall& eax_call);

	void defer_depth(
		const EaxxEaxCall& eax_call);

	void defer_feedback(
		const EaxxEaxCall& eax_call);

	void defer_delay(
		const EaxxEaxCall& eax_call);

	void defer_all(
		const EaxxEaxCall& eax_call);


	void apply_deferred();

	void set(
		const EaxxEaxCall& eax_call);
}; // EaxxFlangerEffect

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_FLANGER_EFFECT_INCLUDED
