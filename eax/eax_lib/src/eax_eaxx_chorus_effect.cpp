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

#include "eax_eaxx_chorus_effect.h"
#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxChorusEffectEaxDirtyFlags& lhs,
	const EaxxChorusEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxChorusEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxChorusEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxChorusEffectException :
	public Exception
{
public:
	explicit EaxxChorusEffectException(
		const char* message)
		:
		Exception{"EAXX_CHORUS_EFFECT", message}
	{
	}
}; // EaxxChorusEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxChorusEffect::EaxxChorusEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_CHORUS)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxChorusEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxChorusEffect::dispatch(
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

void EaxxChorusEffect::set_eax_defaults()
{
	eax_.ulWaveform = ::EAXCHORUS_DEFAULTWAVEFORM;
	eax_.lPhase = ::EAXCHORUS_DEFAULTPHASE;
	eax_.flRate = ::EAXCHORUS_DEFAULTRATE;
	eax_.flDepth = ::EAXCHORUS_DEFAULTDEPTH;
	eax_.flFeedback = ::EAXCHORUS_DEFAULTFEEDBACK;
	eax_.flDelay = ::EAXCHORUS_DEFAULTDELAY;

	eax_d_ = eax_;
}

void EaxxChorusEffect::set_efx_waveform()
{
	const auto wave_form = clamp(
		static_cast<::ALint>(eax_.ulWaveform),
		AL_CHORUS_MIN_PHASE,
		AL_CHORUS_MAX_PHASE
	);

	::alEffecti(efx_effect_object_.get(), AL_CHORUS_WAVEFORM, wave_form);
}

void EaxxChorusEffect::set_efx_phase()
{
	const auto phase = clamp(
		static_cast<::ALint>(eax_.lPhase),
		AL_CHORUS_MIN_PHASE,
		AL_CHORUS_MAX_PHASE
	);

	::alEffecti(efx_effect_object_.get(), AL_CHORUS_PHASE, phase);
}

void EaxxChorusEffect::set_efx_rate()
{
	const auto rate = clamp(
		eax_.flRate,
		AL_CHORUS_MIN_RATE,
		AL_CHORUS_MAX_RATE
	);

	::alEffectf(efx_effect_object_.get(), AL_CHORUS_RATE, rate);
}

void EaxxChorusEffect::set_efx_depth()
{
	const auto depth = clamp(
		eax_.flDepth,
		AL_CHORUS_MIN_DEPTH,
		AL_CHORUS_MAX_DEPTH
	);

	::alEffectf(efx_effect_object_.get(), AL_CHORUS_DEPTH, depth);
}

void EaxxChorusEffect::set_efx_feedback()
{
	const auto feedback = clamp(
		eax_.flFeedback,
		AL_CHORUS_MIN_FEEDBACK,
		AL_CHORUS_MAX_FEEDBACK
	);

	::alEffectf(efx_effect_object_.get(), AL_CHORUS_FEEDBACK, feedback);
}

void EaxxChorusEffect::set_efx_delay()
{
	const auto delay = clamp(
		eax_.flDelay,
		AL_CHORUS_MIN_DELAY,
		AL_CHORUS_MAX_DELAY
	);

	::alEffectf(efx_effect_object_.get(), AL_CHORUS_DELAY, delay);
}

void EaxxChorusEffect::set_efx_defaults()
{
	set_efx_waveform();
	set_efx_phase();
	set_efx_rate();
	set_efx_depth();
	set_efx_feedback();
	set_efx_delay();
}

void EaxxChorusEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXCHORUS_NONE:
			break;

		case ::EAXCHORUS_ALLPARAMETERS:
			eax_call.set_value<EaxxChorusEffectException>(eax_);
			break;

		case ::EAXCHORUS_WAVEFORM:
			eax_call.set_value<EaxxChorusEffectException>(eax_.ulWaveform);
			break;

		case ::EAXCHORUS_PHASE:
			eax_call.set_value<EaxxChorusEffectException>(eax_.lPhase);
			break;

		case ::EAXCHORUS_RATE:
			eax_call.set_value<EaxxChorusEffectException>(eax_.flRate);
			break;

		case ::EAXCHORUS_DEPTH:
			eax_call.set_value<EaxxChorusEffectException>(eax_.flDepth);
			break;

		case ::EAXCHORUS_FEEDBACK:
			eax_call.set_value<EaxxChorusEffectException>(eax_.flFeedback);
			break;

		case ::EAXCHORUS_DELAY:
			eax_call.set_value<EaxxChorusEffectException>(eax_.flDelay);
			break;

		default:
			throw EaxxChorusEffectException{"Unsupported property id."};
	}
}

void EaxxChorusEffect::validate_waveform(
	unsigned long ulWaveform)
{
	eaxx_validate_range<EaxxChorusEffectException>(
		"Waveform",
		ulWaveform,
		::EAXCHORUS_MINWAVEFORM,
		::EAXCHORUS_MAXWAVEFORM
	);
}

void EaxxChorusEffect::validate_phase(
	long lPhase)
{
	eaxx_validate_range<EaxxChorusEffectException>(
		"Phase",
		lPhase,
		::EAXCHORUS_MINPHASE,
		::EAXCHORUS_MAXPHASE
	);
}

void EaxxChorusEffect::validate_rate(
	float flRate)
{
	eaxx_validate_range<EaxxChorusEffectException>(
		"Rate",
		flRate,
		::EAXCHORUS_MINRATE,
		::EAXCHORUS_MAXRATE
	);
}

void EaxxChorusEffect::validate_depth(
	float flDepth)
{
	eaxx_validate_range<EaxxChorusEffectException>(
		"Depth",
		flDepth,
		::EAXCHORUS_MINDEPTH,
		::EAXCHORUS_MAXDEPTH
	);
}

void EaxxChorusEffect::validate_feedback(
	float flFeedback)
{
	eaxx_validate_range<EaxxChorusEffectException>(
		"Feedback",
		flFeedback,
		::EAXCHORUS_MINFEEDBACK,
		::EAXCHORUS_MAXFEEDBACK
	);
}

void EaxxChorusEffect::validate_delay(
	float flDelay)
{
	eaxx_validate_range<EaxxChorusEffectException>(
		"Delay",
		flDelay,
		::EAXCHORUS_MINDELAY,
		::EAXCHORUS_MAXDELAY
	);
}

void EaxxChorusEffect::validate_all(
	const ::EAXCHORUSPROPERTIES& eax_all)
{
	validate_waveform(eax_all.ulWaveform);
	validate_phase(eax_all.lPhase);
	validate_rate(eax_all.flRate);
	validate_depth(eax_all.flDepth);
	validate_feedback(eax_all.flFeedback);
	validate_delay(eax_all.flDelay);
}

void EaxxChorusEffect::defer_waveform(
	unsigned long ulWaveform)
{
	eax_d_.ulWaveform = ulWaveform;
	eax_dirty_flags_.ulWaveform = (eax_.ulWaveform != eax_d_.ulWaveform);
}

void EaxxChorusEffect::defer_phase(
	long lPhase)
{
	eax_d_.lPhase = lPhase;
	eax_dirty_flags_.lPhase = (eax_.lPhase != eax_d_.lPhase);
}

void EaxxChorusEffect::defer_rate(
	float flRate)
{
	eax_d_.flRate = flRate;
	eax_dirty_flags_.flRate = (eax_.flRate != eax_d_.flRate);
}

void EaxxChorusEffect::defer_depth(
	float flDepth)
{
	eax_d_.flDepth = flDepth;
	eax_dirty_flags_.flDepth = (eax_.flDepth != eax_d_.flDepth);
}

void EaxxChorusEffect::defer_feedback(
	float flFeedback)
{
	eax_d_.flFeedback = flFeedback;
	eax_dirty_flags_.flFeedback = (eax_.flFeedback != eax_d_.flFeedback);
}

void EaxxChorusEffect::defer_delay(
	float flDelay)
{
	eax_d_.flDelay = flDelay;
	eax_dirty_flags_.flDelay = (eax_.flDelay != eax_d_.flDelay);
}

void EaxxChorusEffect::defer_all(
	const ::EAXCHORUSPROPERTIES& eax_all)
{
	defer_waveform(eax_all.ulWaveform);
	defer_phase(eax_all.lPhase);
	defer_rate(eax_all.flRate);
	defer_depth(eax_all.flDepth);
	defer_feedback(eax_all.flFeedback);
	defer_delay(eax_all.flDelay);
}

void EaxxChorusEffect::defer_waveform(
	const EaxxEaxCall& eax_call)
{
	const auto& waveform =
		eax_call.get_value<EaxxChorusEffectException, const decltype(::EAXCHORUSPROPERTIES::ulWaveform)>();

	validate_waveform(waveform);
	defer_waveform(waveform);
}

void EaxxChorusEffect::defer_phase(
	const EaxxEaxCall& eax_call)
{
	const auto& phase =
		eax_call.get_value<EaxxChorusEffectException, const decltype(::EAXCHORUSPROPERTIES::lPhase)>();

	validate_phase(phase);
	defer_phase(phase);
}

void EaxxChorusEffect::defer_rate(
	const EaxxEaxCall& eax_call)
{
	const auto& rate =
		eax_call.get_value<EaxxChorusEffectException, const decltype(::EAXCHORUSPROPERTIES::flRate)>();

	validate_rate(rate);
	defer_rate(rate);
}

void EaxxChorusEffect::defer_depth(
	const EaxxEaxCall& eax_call)
{
	const auto& depth =
		eax_call.get_value<EaxxChorusEffectException, const decltype(::EAXCHORUSPROPERTIES::flDepth)>();

	validate_depth(depth);
	defer_depth(depth);
}

void EaxxChorusEffect::defer_feedback(
	const EaxxEaxCall& eax_call)
{
	const auto& feedback =
		eax_call.get_value<EaxxChorusEffectException, const decltype(::EAXCHORUSPROPERTIES::flFeedback)>();

	validate_feedback(feedback);
	defer_feedback(feedback);
}

void EaxxChorusEffect::defer_delay(
	const EaxxEaxCall& eax_call)
{
	const auto& delay =
		eax_call.get_value<EaxxChorusEffectException, const decltype(::EAXCHORUSPROPERTIES::flDelay)>();

	validate_delay(delay);
	defer_delay(delay);
}

void EaxxChorusEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<EaxxChorusEffectException, const ::EAXCHORUSPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxChorusEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxChorusEffectEaxDirtyFlags{})
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

	eax_dirty_flags_ = EaxxChorusEffectEaxDirtyFlags{};

	load();
}

void EaxxChorusEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXCHORUS_NONE:
			break;

		case ::EAXCHORUS_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXCHORUS_WAVEFORM:
			defer_waveform(eax_call);
			break;

		case ::EAXCHORUS_PHASE:
			defer_phase(eax_call);
			break;

		case ::EAXCHORUS_RATE:
			defer_rate(eax_call);
			break;

		case ::EAXCHORUS_DEPTH:
			defer_depth(eax_call);
			break;

		case ::EAXCHORUS_FEEDBACK:
			defer_feedback(eax_call);
			break;

		case ::EAXCHORUS_DELAY:
			defer_delay(eax_call);
			break;

		default:
			throw EaxxChorusEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
