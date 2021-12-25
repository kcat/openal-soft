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


#include "AL/efx.h"

#include "eax_algorithm.h"
#include "eax_exception.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_frequency_shifter_effect.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxFrequencyShifterEffectEaxDirtyFlags& lhs,
	const EaxxFrequencyShifterEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxFrequencyShifterEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxFrequencyShifterEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxFrequencyShifterEffectException :
	public Exception
{
public:
	explicit EaxxFrequencyShifterEffectException(
		const char* message)
		:
		Exception{"EAXX_FREQUENCY_SHIFTER_EFFECT", message}
	{
	}
}; // EaxxFrequencyShifterEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxFrequencyShifterEffect::EaxxFrequencyShifterEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_FREQUENCY_SHIFTER)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxFrequencyShifterEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxFrequencyShifterEffect::dispatch(
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

void EaxxFrequencyShifterEffect::set_eax_defaults()
{
	eax_.flFrequency = ::EAXFREQUENCYSHIFTER_DEFAULTFREQUENCY;
	eax_.ulLeftDirection = ::EAXFREQUENCYSHIFTER_DEFAULTLEFTDIRECTION;
	eax_.ulRightDirection = ::EAXFREQUENCYSHIFTER_DEFAULTRIGHTDIRECTION;

	eax_d_ = eax_;
}

void EaxxFrequencyShifterEffect::set_efx_frequency()
{
	const auto frequency = clamp(
		eax_.flFrequency,
		AL_FREQUENCY_SHIFTER_MIN_FREQUENCY,
		AL_FREQUENCY_SHIFTER_MAX_FREQUENCY
	);

	::alEffectf(efx_effect_object_.get(), AL_FREQUENCY_SHIFTER_FREQUENCY, frequency);
}

void EaxxFrequencyShifterEffect::set_efx_left_direction()
{
	const auto left_direction = clamp(
		static_cast<::ALint>(eax_.ulLeftDirection),
		AL_FREQUENCY_SHIFTER_MIN_LEFT_DIRECTION,
		AL_FREQUENCY_SHIFTER_MAX_LEFT_DIRECTION
	);

	::alEffecti(efx_effect_object_.get(), AL_FREQUENCY_SHIFTER_LEFT_DIRECTION, left_direction);
}

void EaxxFrequencyShifterEffect::set_efx_right_direction()
{
	const auto right_direction = clamp(
		static_cast<::ALint>(eax_.ulRightDirection),
		AL_FREQUENCY_SHIFTER_MIN_RIGHT_DIRECTION,
		AL_FREQUENCY_SHIFTER_MAX_RIGHT_DIRECTION
	);

	::alEffecti(efx_effect_object_.get(), AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION, right_direction);
}

void EaxxFrequencyShifterEffect::set_efx_defaults()
{
	set_efx_frequency();
	set_efx_left_direction();
	set_efx_right_direction();
}

void EaxxFrequencyShifterEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXFREQUENCYSHIFTER_NONE:
			break;

		case ::EAXFREQUENCYSHIFTER_ALLPARAMETERS:
			eax_call.set_value<EaxxFrequencyShifterEffectException>(eax_);
			break;

		case ::EAXFREQUENCYSHIFTER_FREQUENCY:
			eax_call.set_value<EaxxFrequencyShifterEffectException>(eax_.flFrequency);
			break;

		case ::EAXFREQUENCYSHIFTER_LEFTDIRECTION:
			eax_call.set_value<EaxxFrequencyShifterEffectException>(eax_.ulLeftDirection);
			break;

		case ::EAXFREQUENCYSHIFTER_RIGHTDIRECTION:
			eax_call.set_value<EaxxFrequencyShifterEffectException>(eax_.ulRightDirection);
			break;

		default:
			throw EaxxFrequencyShifterEffectException{"Unsupported property id."};
	}
}

void EaxxFrequencyShifterEffect::validate_frequency(
	float flFrequency)
{
	eaxx_validate_range<EaxxFrequencyShifterEffectException>(
		"Frequency",
		flFrequency,
		::EAXFREQUENCYSHIFTER_MINFREQUENCY,
		::EAXFREQUENCYSHIFTER_MAXFREQUENCY
	);
}

void EaxxFrequencyShifterEffect::validate_left_direction(
	unsigned long ulLeftDirection)
{
	eaxx_validate_range<EaxxFrequencyShifterEffectException>(
		"Left Direction",
		ulLeftDirection,
		::EAXFREQUENCYSHIFTER_MINLEFTDIRECTION,
		::EAXFREQUENCYSHIFTER_MAXLEFTDIRECTION
	);
}

void EaxxFrequencyShifterEffect::validate_right_direction(
	unsigned long ulRightDirection)
{
	eaxx_validate_range<EaxxFrequencyShifterEffectException>(
		"Right Direction",
		ulRightDirection,
		::EAXFREQUENCYSHIFTER_MINRIGHTDIRECTION,
		::EAXFREQUENCYSHIFTER_MAXRIGHTDIRECTION
	);
}

void EaxxFrequencyShifterEffect::validate_all(
	const ::EAXFREQUENCYSHIFTERPROPERTIES& all)
{
	validate_frequency(all.flFrequency);
	validate_left_direction(all.ulLeftDirection);
	validate_right_direction(all.ulRightDirection);
}

void EaxxFrequencyShifterEffect::defer_frequency(
	float flFrequency)
{
	eax_d_.flFrequency = flFrequency;
	eax_dirty_flags_.flFrequency = (eax_.flFrequency != eax_d_.flFrequency);
}

void EaxxFrequencyShifterEffect::defer_left_direction(
	unsigned long ulLeftDirection)
{
	eax_d_.ulLeftDirection = ulLeftDirection;
	eax_dirty_flags_.ulLeftDirection = (eax_.ulLeftDirection != eax_d_.ulLeftDirection);
}

void EaxxFrequencyShifterEffect::defer_right_direction(
	unsigned long ulRightDirection)
{
	eax_d_.ulRightDirection = ulRightDirection;
	eax_dirty_flags_.ulRightDirection = (eax_.ulRightDirection != eax_d_.ulRightDirection);
}

void EaxxFrequencyShifterEffect::defer_all(
	const ::EAXFREQUENCYSHIFTERPROPERTIES& all)
{
	defer_frequency(all.flFrequency);
	defer_left_direction(all.ulLeftDirection);
	defer_right_direction(all.ulRightDirection);
}

void EaxxFrequencyShifterEffect::defer_frequency(
	const EaxxEaxCall& eax_call)
{
	const auto& frequency =
		eax_call.get_value<
			EaxxFrequencyShifterEffectException, const decltype(::EAXFREQUENCYSHIFTERPROPERTIES::flFrequency)>();

	validate_frequency(frequency);
	defer_frequency(frequency);
}

void EaxxFrequencyShifterEffect::defer_left_direction(
	const EaxxEaxCall& eax_call)
{
	const auto& left_direction =
		eax_call.get_value<
			EaxxFrequencyShifterEffectException, const decltype(::EAXFREQUENCYSHIFTERPROPERTIES::ulLeftDirection)>();

	validate_left_direction(left_direction);
	defer_left_direction(left_direction);
}

void EaxxFrequencyShifterEffect::defer_right_direction(
	const EaxxEaxCall& eax_call)
{
	const auto& right_direction =
		eax_call.get_value<
			EaxxFrequencyShifterEffectException, const decltype(::EAXFREQUENCYSHIFTERPROPERTIES::ulRightDirection)>();

	validate_right_direction(right_direction);
	defer_right_direction(right_direction);
}

void EaxxFrequencyShifterEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<
			EaxxFrequencyShifterEffectException, const ::EAXFREQUENCYSHIFTERPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxFrequencyShifterEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxFrequencyShifterEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.flFrequency)
	{
		set_efx_frequency();
	}

	if (eax_dirty_flags_.ulLeftDirection)
	{
		set_efx_left_direction();
	}

	if (eax_dirty_flags_.ulRightDirection)
	{
		set_efx_right_direction();
	}

	eax_dirty_flags_ = EaxxFrequencyShifterEffectEaxDirtyFlags{};

	load();
}

void EaxxFrequencyShifterEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXFREQUENCYSHIFTER_NONE:
			break;

		case ::EAXFREQUENCYSHIFTER_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXFREQUENCYSHIFTER_FREQUENCY:
			defer_frequency(eax_call);
			break;

		case ::EAXFREQUENCYSHIFTER_LEFTDIRECTION:
			defer_left_direction(eax_call);
			break;

		case ::EAXFREQUENCYSHIFTER_RIGHTDIRECTION:
			defer_right_direction(eax_call);
			break;

		default:
			throw EaxxFrequencyShifterEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
