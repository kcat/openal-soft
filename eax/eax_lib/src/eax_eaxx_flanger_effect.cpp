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
#include "eax_eaxx_flanger_effect.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxFlangerEffectEaxDirtyFlags& lhs,
	const EaxxFlangerEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxFlangerEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxFlangerEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxFlangerEffectException :
	public Exception
{
public:
	explicit EaxxFlangerEffectException(
		const char* message)
		:
		Exception{"EAXX_FLANGER_EFFECT", message}
	{
	}
}; // EaxxFlangerEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxFlangerEffect::EaxxFlangerEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_FLANGER)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxFlangerEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxFlangerEffect::dispatch(
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

void EaxxFlangerEffect::set_eax_defaults()
{
	eax_.ulWaveform = ::EAXFLANGER_DEFAULTWAVEFORM;
	eax_.lPhase = ::EAXFLANGER_DEFAULTPHASE;
	eax_.flRate = ::EAXFLANGER_DEFAULTRATE;
	eax_.flDepth = ::EAXFLANGER_DEFAULTDEPTH;
	eax_.flFeedback = ::EAXFLANGER_DEFAULTFEEDBACK;
	eax_.flDelay = ::EAXFLANGER_DEFAULTDELAY;

	eax_d_ = eax_;
}

void EaxxFlangerEffect::set_efx_waveform()
{
	const auto waveform = clamp(
		static_cast<::ALint>(eax_.ulWaveform),
		AL_FLANGER_MIN_WAVEFORM,
		AL_FLANGER_MAX_WAVEFORM
	);

	::alEffecti(efx_effect_object_.get(), AL_FLANGER_WAVEFORM, waveform);
}

void EaxxFlangerEffect::set_efx_phase()
{
	const auto phase = clamp(
		static_cast<::ALint>(eax_.lPhase),
		AL_FLANGER_MIN_PHASE,
		AL_FLANGER_MAX_PHASE
	);

	::alEffecti(efx_effect_object_.get(), AL_FLANGER_PHASE, phase);
}

void EaxxFlangerEffect::set_efx_rate()
{
	const auto rate = clamp(
		eax_.flRate,
		AL_FLANGER_MIN_RATE,
		AL_FLANGER_MAX_RATE
	);

	::alEffectf(efx_effect_object_.get(), AL_FLANGER_RATE, rate);
}

void EaxxFlangerEffect::set_efx_depth()
{
	const auto depth = clamp(
		eax_.flDepth,
		AL_FLANGER_MIN_DEPTH,
		AL_FLANGER_MAX_DEPTH
	);

	::alEffectf(efx_effect_object_.get(), AL_FLANGER_DEPTH, depth);
}

void EaxxFlangerEffect::set_efx_feedback()
{
	const auto feedback = clamp(
		eax_.flFeedback,
		AL_FLANGER_MIN_FEEDBACK,
		AL_FLANGER_MAX_FEEDBACK
	);

	::alEffectf(efx_effect_object_.get(), AL_FLANGER_FEEDBACK, feedback);
}

void EaxxFlangerEffect::set_efx_delay()
{
	const auto delay = clamp(
		eax_.flDelay,
		AL_FLANGER_MIN_DELAY,
		AL_FLANGER_MAX_DELAY
	);

	::alEffectf(efx_effect_object_.get(), AL_FLANGER_DELAY, delay);
}

void EaxxFlangerEffect::set_efx_defaults()
{
	set_efx_waveform();
	set_efx_phase();
	set_efx_rate();
	set_efx_depth();
	set_efx_feedback();
	set_efx_delay();
}

void EaxxFlangerEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXFLANGER_NONE:
			break;

		case ::EAXFLANGER_ALLPARAMETERS:
			eax_call.set_value<EaxxFlangerEffectException>(eax_);
			break;

		case ::EAXFLANGER_WAVEFORM:
			eax_call.set_value<EaxxFlangerEffectException>(eax_.ulWaveform);
			break;

		case ::EAXFLANGER_PHASE:
			eax_call.set_value<EaxxFlangerEffectException>(eax_.lPhase);
			break;

		case ::EAXFLANGER_RATE:
			eax_call.set_value<EaxxFlangerEffectException>(eax_.flRate);
			break;

		case ::EAXFLANGER_DEPTH:
			eax_call.set_value<EaxxFlangerEffectException>(eax_.flDepth);
			break;

		case ::EAXFLANGER_FEEDBACK:
			eax_call.set_value<EaxxFlangerEffectException>(eax_.flFeedback);
			break;

		case ::EAXFLANGER_DELAY:
			eax_call.set_value<EaxxFlangerEffectException>(eax_.flDelay);
			break;

		default:
			throw EaxxFlangerEffectException{"Unsupported property id."};
	}
}

void EaxxFlangerEffect::validate_waveform(
	unsigned long ulWaveform)
{
	eaxx_validate_range<EaxxFlangerEffectException>(
		"Waveform",
		ulWaveform,
		::EAXFLANGER_MINWAVEFORM,
		::EAXFLANGER_MAXWAVEFORM
	);
}

void EaxxFlangerEffect::validate_phase(
	long lPhase)
{
	eaxx_validate_range<EaxxFlangerEffectException>(
		"Phase",
		lPhase,
		::EAXFLANGER_MINPHASE,
		::EAXFLANGER_MAXPHASE
	);
}

void EaxxFlangerEffect::validate_rate(
	float flRate)
{
	eaxx_validate_range<EaxxFlangerEffectException>(
		"Rate",
		flRate,
		::EAXFLANGER_MINRATE,
		::EAXFLANGER_MAXRATE
	);
}

void EaxxFlangerEffect::validate_depth(
	float flDepth)
{
	eaxx_validate_range<EaxxFlangerEffectException>(
		"Depth",
		flDepth,
		::EAXFLANGER_MINDEPTH,
		::EAXFLANGER_MAXDEPTH
	);
}

void EaxxFlangerEffect::validate_feedback(
	float flFeedback)
{
	eaxx_validate_range<EaxxFlangerEffectException>(
		"Feedback",
		flFeedback,
		::EAXFLANGER_MINFEEDBACK,
		::EAXFLANGER_MAXFEEDBACK
	);
}

void EaxxFlangerEffect::validate_delay(
	float flDelay)
{
	eaxx_validate_range<EaxxFlangerEffectException>(
		"Delay",
		flDelay,
		::EAXFLANGER_MINDELAY,
		::EAXFLANGER_MAXDELAY
	);
}

void EaxxFlangerEffect::validate_all(
	const ::EAXFLANGERPROPERTIES& all)
{
	validate_waveform(all.ulWaveform);
	validate_phase(all.lPhase);
	validate_rate(all.flRate);
	validate_depth(all.flDepth);
	validate_feedback(all.flDelay);
	validate_delay(all.flDelay);
}

void EaxxFlangerEffect::defer_waveform(
	unsigned long ulWaveform)
{
	eax_d_.ulWaveform = ulWaveform;
	eax_dirty_flags_.ulWaveform = (eax_.ulWaveform != eax_d_.ulWaveform);
}

void EaxxFlangerEffect::defer_phase(
	long lPhase)
{
	eax_d_.lPhase = lPhase;
	eax_dirty_flags_.lPhase = (eax_.lPhase != eax_d_.lPhase);
}

void EaxxFlangerEffect::defer_rate(
	float flRate)
{
	eax_d_.flRate = flRate;
	eax_dirty_flags_.flRate = (eax_.flRate != eax_d_.flRate);
}

void EaxxFlangerEffect::defer_depth(
	float flDepth)
{
	eax_d_.flDepth = flDepth;
	eax_dirty_flags_.flDepth = (eax_.flDepth != eax_d_.flDepth);
}

void EaxxFlangerEffect::defer_feedback(
	float flFeedback)
{
	eax_d_.flFeedback = flFeedback;
	eax_dirty_flags_.flFeedback = (eax_.flFeedback != eax_d_.flFeedback);
}

void EaxxFlangerEffect::defer_delay(
	float flDelay)
{
	eax_d_.flDelay = flDelay;
	eax_dirty_flags_.flDelay = (eax_.flDelay != eax_d_.flDelay);
}

void EaxxFlangerEffect::defer_all(
	const ::EAXFLANGERPROPERTIES& all)
{
	defer_waveform(all.ulWaveform);
	defer_phase(all.lPhase);
	defer_rate(all.flRate);
	defer_depth(all.flDepth);
	defer_feedback(all.flDelay);
	defer_delay(all.flDelay);
}

void EaxxFlangerEffect::defer_waveform(
	const EaxxEaxCall& eax_call)
{
	const auto& waveform =
		eax_call.get_value<EaxxFlangerEffectException, const decltype(::EAXFLANGERPROPERTIES::ulWaveform)>();

	validate_waveform(waveform);
	defer_waveform(waveform);
}

void EaxxFlangerEffect::defer_phase(
	const EaxxEaxCall& eax_call)
{
	const auto& phase =
		eax_call.get_value<EaxxFlangerEffectException, const decltype(::EAXFLANGERPROPERTIES::lPhase)>();

	validate_phase(phase);
	defer_phase(phase);
}

void EaxxFlangerEffect::defer_rate(
	const EaxxEaxCall& eax_call)
{
	const auto& rate =
		eax_call.get_value<EaxxFlangerEffectException, const decltype(::EAXFLANGERPROPERTIES::flRate)>();

	validate_rate(rate);
	defer_rate(rate);
}

void EaxxFlangerEffect::defer_depth(
	const EaxxEaxCall& eax_call)
{
	const auto& depth =
		eax_call.get_value<EaxxFlangerEffectException, const decltype(::EAXFLANGERPROPERTIES::flDepth)>();

	validate_depth(depth);
	defer_depth(depth);
}

void EaxxFlangerEffect::defer_feedback(
	const EaxxEaxCall& eax_call)
{
	const auto& feedback =
		eax_call.get_value<EaxxFlangerEffectException, const decltype(::EAXFLANGERPROPERTIES::flFeedback)>();

	validate_feedback(feedback);
	defer_feedback(feedback);
}

void EaxxFlangerEffect::defer_delay(
	const EaxxEaxCall& eax_call)
{
	const auto& delay =
		eax_call.get_value<EaxxFlangerEffectException, const decltype(::EAXFLANGERPROPERTIES::flDelay)>();

	validate_delay(delay);
	defer_delay(delay);
}

void EaxxFlangerEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<EaxxFlangerEffectException, const ::EAXFLANGERPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxFlangerEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxFlangerEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.ulWaveform)
	{
		set_efx_waveform();
	}

	if (eax_dirty_flags_.lPhase)
	{
		set_efx_phase();
	}

	if (eax_dirty_flags_.flRate)
	{
		set_efx_rate();
	}

	if (eax_dirty_flags_.flDepth)
	{
		set_efx_depth();
	}

	if (eax_dirty_flags_.flFeedback)
	{
		set_efx_feedback();
	}

	if (eax_dirty_flags_.flDelay)
	{
		set_efx_delay();
	}

	eax_dirty_flags_ = EaxxFlangerEffectEaxDirtyFlags{};

	load();
}

void EaxxFlangerEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXFLANGER_NONE:
			break;

		case ::EAXFLANGER_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXFLANGER_WAVEFORM:
			defer_waveform(eax_call);
			break;

		case ::EAXFLANGER_PHASE:
			defer_phase(eax_call);
			break;

		case ::EAXFLANGER_RATE:
			defer_rate(eax_call);
			break;

		case ::EAXFLANGER_DEPTH:
			defer_depth(eax_call);
			break;

		case ::EAXFLANGER_FEEDBACK:
			defer_feedback(eax_call);
			break;

		case ::EAXFLANGER_DELAY:
			defer_delay(eax_call);
			break;

		default:
			throw EaxxFlangerEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
