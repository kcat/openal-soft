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


#ifndef EAX_EAXX_FREQUENCY_SHIFTER_EFFECT_INCLUDED
#define EAX_EAXX_FREQUENCY_SHIFTER_EFFECT_INCLUDED


#include <cstdint>

#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_effect.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using EaxxFrequencyShifterEffectEaxDirtyFlagsValue = unsigned int;

struct EaxxFrequencyShifterEffectEaxDirtyFlags
{
	EaxxFrequencyShifterEffectEaxDirtyFlagsValue flFrequency : 1;
	EaxxFrequencyShifterEffectEaxDirtyFlagsValue ulLeftDirection : 1;
	EaxxFrequencyShifterEffectEaxDirtyFlagsValue ulRightDirection : 1;
}; // EaxxFrequencyShifterEffectEaxDirtyFlags

static_assert(sizeof(EaxxFrequencyShifterEffectEaxDirtyFlags) == sizeof(EaxxFrequencyShifterEffectEaxDirtyFlagsValue), "Type size.");

bool operator==(
	const EaxxFrequencyShifterEffectEaxDirtyFlags& lhs,
	const EaxxFrequencyShifterEffectEaxDirtyFlags& rhs) noexcept;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxFrequencyShifterEffect final :
	public EaxxEffect
{
public:
	EaxxFrequencyShifterEffect(
		::ALuint al_effect_slot);


	void load() override;

	void dispatch(
		const EaxxEaxCall& eax_call) override;


private:
	const ::ALuint al_effect_slot_;
	EfxEffectObject efx_effect_object_;

	::EAXFREQUENCYSHIFTERPROPERTIES eax_;
	::EAXFREQUENCYSHIFTERPROPERTIES eax_d_;
	EaxxFrequencyShifterEffectEaxDirtyFlags eax_dirty_flags_{};


	void set_eax_defaults();


	void set_efx_frequency();

	void set_efx_left_direction();

	void set_efx_right_direction();

	void set_efx_defaults();


	void get(
		const EaxxEaxCall& eax_call);


	void validate_frequency(
		float flFrequency);

	void validate_left_direction(
		unsigned long ulLeftDirection);

	void validate_right_direction(
		unsigned long ulRightDirection);

	void validate_all(
		const ::EAXFREQUENCYSHIFTERPROPERTIES& all);


	void defer_frequency(
		float flFrequency);

	void defer_left_direction(
		unsigned long ulLeftDirection);

	void defer_right_direction(
		unsigned long ulRightDirection);

	void defer_all(
		const ::EAXFREQUENCYSHIFTERPROPERTIES& all);


	void defer_frequency(
		const EaxxEaxCall& eax_call);

	void defer_left_direction(
		const EaxxEaxCall& eax_call);

	void defer_right_direction(
		const EaxxEaxCall& eax_call);

	void defer_all(
		const EaxxEaxCall& eax_call);


	void apply_deferred();

	void set(
		const EaxxEaxCall& eax_call);
}; // EaxxFrequencyShifterEffect

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_FREQUENCY_SHIFTER_EFFECT_INCLUDED
