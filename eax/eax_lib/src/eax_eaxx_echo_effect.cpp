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


#include <algorithm>

#include "AL/efx.h"

#include "eax_algorithm.h"
#include "eax_exception.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_echo_effect.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxEchoEffectEaxDirtyFlags& lhs,
	const EaxxEchoEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxEchoEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxEchoEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEchoEffectException :
	public Exception
{
public:
	explicit EaxxEchoEffectException(
		const char* message)
		:
		Exception{"EAXX_ECHO_EFFECT", message}
	{
	}
}; // EaxxEchoEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxEchoEffect::EaxxEchoEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_ECHO)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxEchoEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxEchoEffect::dispatch(
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

void EaxxEchoEffect::set_eax_defaults()
{
	eax_.flDelay = ::EAXECHO_DEFAULTDELAY;
	eax_.flLRDelay = ::EAXECHO_DEFAULTLRDELAY;
	eax_.flDamping = ::EAXECHO_DEFAULTDAMPING;
	eax_.flFeedback = ::EAXECHO_DEFAULTFEEDBACK;
	eax_.flSpread = ::EAXECHO_DEFAULTSPREAD;

	eax_d_ = eax_;
}

void EaxxEchoEffect::set_efx_delay()
{
	const auto delay = clamp(
		eax_.flDelay,
		AL_ECHO_MIN_DELAY,
		AL_ECHO_MAX_DELAY
	);

	::alEffectf(efx_effect_object_.get(), AL_ECHO_DELAY, delay);
}

void EaxxEchoEffect::set_efx_lr_delay()
{
	const auto lr_delay = clamp(
		eax_.flLRDelay,
		AL_ECHO_MIN_LRDELAY,
		AL_ECHO_MAX_LRDELAY
	);

	::alEffectf(efx_effect_object_.get(), AL_ECHO_LRDELAY, lr_delay);
}

void EaxxEchoEffect::set_efx_damping()
{
	const auto damping = clamp(
		eax_.flDamping,
		AL_ECHO_MIN_DAMPING,
		AL_ECHO_MAX_DAMPING
	);

	::alEffectf(efx_effect_object_.get(), AL_ECHO_DAMPING, damping);
}

void EaxxEchoEffect::set_efx_feedback()
{
	const auto feedback = clamp(
		eax_.flFeedback,
		AL_ECHO_MIN_FEEDBACK,
		AL_ECHO_MAX_FEEDBACK
	);

	::alEffectf(efx_effect_object_.get(), AL_ECHO_FEEDBACK, feedback);
}

void EaxxEchoEffect::set_efx_spread()
{
	const auto spread = clamp(
		eax_.flSpread,
		AL_ECHO_MIN_SPREAD,
		AL_ECHO_MAX_SPREAD
	);

	::alEffectf(efx_effect_object_.get(), AL_ECHO_SPREAD, spread);
}

void EaxxEchoEffect::set_efx_defaults()
{
	set_efx_delay();
	set_efx_lr_delay();
	set_efx_damping();
	set_efx_feedback();
	set_efx_spread();
}

void EaxxEchoEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXECHO_NONE:
			break;

		case ::EAXECHO_ALLPARAMETERS:
			eax_call.set_value<EaxxEchoEffectException>(eax_);
			break;

		case ::EAXECHO_DELAY:
			eax_call.set_value<EaxxEchoEffectException>(eax_.flDelay);
			break;

		case ::EAXECHO_LRDELAY:
			eax_call.set_value<EaxxEchoEffectException>(eax_.flLRDelay);
			break;

		case ::EAXECHO_DAMPING:
			eax_call.set_value<EaxxEchoEffectException>(eax_.flDamping);
			break;

		case ::EAXECHO_FEEDBACK:
			eax_call.set_value<EaxxEchoEffectException>(eax_.flFeedback);
			break;

		case ::EAXECHO_SPREAD:
			eax_call.set_value<EaxxEchoEffectException>(eax_.flSpread);
			break;

		default:
			throw EaxxEchoEffectException{"Unsupported property id."};
	}
}

void EaxxEchoEffect::validate_delay(
	float flDelay)
{
	eaxx_validate_range<EaxxEchoEffectException>(
		"Delay",
		flDelay,
		::EAXECHO_MINDELAY,
		::EAXECHO_MAXDELAY
	);
}

void EaxxEchoEffect::validate_lr_delay(
	float flLRDelay)
{
	eaxx_validate_range<EaxxEchoEffectException>(
		"LR Delay",
		flLRDelay,
		::EAXECHO_MINLRDELAY,
		::EAXECHO_MAXLRDELAY
	);
}

void EaxxEchoEffect::validate_damping(
	float flDamping)
{
	eaxx_validate_range<EaxxEchoEffectException>(
		"Damping",
		flDamping,
		::EAXECHO_MINDAMPING,
		::EAXECHO_MAXDAMPING
	);
}

void EaxxEchoEffect::validate_feedback(
	float flFeedback)
{
	eaxx_validate_range<EaxxEchoEffectException>(
		"Feedback",
		flFeedback,
		::EAXECHO_MINFEEDBACK,
		::EAXECHO_MAXFEEDBACK
	);
}

void EaxxEchoEffect::validate_spread(
	float flSpread)
{
	eaxx_validate_range<EaxxEchoEffectException>(
		"Spread",
		flSpread,
		::EAXECHO_MINSPREAD,
		::EAXECHO_MAXSPREAD
	);
}

void EaxxEchoEffect::validate_all(
	const ::EAXECHOPROPERTIES& all)
{
	validate_delay(all.flDelay);
	validate_lr_delay(all.flLRDelay);
	validate_damping(all.flDamping);
	validate_feedback(all.flFeedback);
	validate_spread(all.flSpread);
}

void EaxxEchoEffect::defer_delay(
	float flDelay)
{
	eax_d_.flDelay = flDelay;
	eax_dirty_flags_.flDelay = (eax_.flDelay != eax_d_.flDelay);
}

void EaxxEchoEffect::defer_lr_delay(
	float flLRDelay)
{
	eax_d_.flLRDelay = flLRDelay;
	eax_dirty_flags_.flLRDelay = (eax_.flLRDelay != eax_d_.flLRDelay);
}

void EaxxEchoEffect::defer_damping(
	float flDamping)
{
	eax_d_.flDamping = flDamping;
	eax_dirty_flags_.flDamping = (eax_.flDamping != eax_d_.flDamping);
}

void EaxxEchoEffect::defer_feedback(
	float flFeedback)
{
	eax_d_.flFeedback = flFeedback;
	eax_dirty_flags_.flFeedback = (eax_.flFeedback != eax_d_.flFeedback);
}

void EaxxEchoEffect::defer_spread(
	float flSpread)
{
	eax_d_.flSpread = flSpread;
	eax_dirty_flags_.flSpread = (eax_.flSpread != eax_d_.flSpread);
}

void EaxxEchoEffect::defer_all(
	const ::EAXECHOPROPERTIES& all)
{
	defer_delay(all.flDelay);
	defer_lr_delay(all.flLRDelay);
	defer_damping(all.flDamping);
	defer_feedback(all.flFeedback);
	defer_spread(all.flSpread);
}

void EaxxEchoEffect::defer_delay(
	const EaxxEaxCall& eax_call)
{
	const auto& delay =
		eax_call.get_value<EaxxEchoEffectException, const decltype(::EAXECHOPROPERTIES::flDelay)>();

	validate_delay(delay);
	defer_delay(delay);
}

void EaxxEchoEffect::defer_lr_delay(
	const EaxxEaxCall& eax_call)
{
	const auto& lr_delay =
		eax_call.get_value<EaxxEchoEffectException, const decltype(::EAXECHOPROPERTIES::flLRDelay)>();

	validate_lr_delay(lr_delay);
	defer_lr_delay(lr_delay);
}

void EaxxEchoEffect::defer_damping(
	const EaxxEaxCall& eax_call)
{
	const auto& damping =
		eax_call.get_value<EaxxEchoEffectException, const decltype(::EAXECHOPROPERTIES::flDamping)>();

	validate_damping(damping);
	defer_damping(damping);
}

void EaxxEchoEffect::defer_feedback(
	const EaxxEaxCall& eax_call)
{
	const auto& feedback =
		eax_call.get_value<EaxxEchoEffectException, const decltype(::EAXECHOPROPERTIES::flFeedback)>();

	validate_feedback(feedback);
	defer_feedback(feedback);
}

void EaxxEchoEffect::defer_spread(
	const EaxxEaxCall& eax_call)
{
	const auto& spread =
		eax_call.get_value<EaxxEchoEffectException, const decltype(::EAXECHOPROPERTIES::flSpread)>();

	validate_spread(spread);
	defer_spread(spread);
}

void EaxxEchoEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<EaxxEchoEffectException, const ::EAXECHOPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxEchoEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxEchoEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.flDelay)
	{
		set_efx_delay();
	}

	if (eax_dirty_flags_.flLRDelay)
	{
		set_efx_lr_delay();
	}

	if (eax_dirty_flags_.flDamping)
	{
		set_efx_damping();
	}

	if (eax_dirty_flags_.flFeedback)
	{
		set_efx_feedback();
	}

	if (eax_dirty_flags_.flSpread)
	{
		set_efx_spread();
	}

	eax_dirty_flags_ = EaxxEchoEffectEaxDirtyFlags{};

	load();
}

void EaxxEchoEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXECHO_NONE:
			break;

		case ::EAXECHO_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXECHO_DELAY:
			defer_delay(eax_call);
			break;

		case ::EAXECHO_LRDELAY:
			defer_lr_delay(eax_call);
			break;

		case ::EAXECHO_DAMPING:
			defer_damping(eax_call);
			break;

		case ::EAXECHO_FEEDBACK:
			defer_feedback(eax_call);
			break;

		case ::EAXECHO_SPREAD:
			defer_spread(eax_call);
			break;

		default:
			throw EaxxEchoEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
