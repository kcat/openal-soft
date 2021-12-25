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
#include "eax_unit_converters.h"

#include "eax_eaxx_auto_wah_effect.h"
#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxAutoWahEffectEaxDirtyFlags& lhs,
	const EaxxAutoWahEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxAutoWahEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxAutoWahEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxAutoWahEffectException :
	public Exception
{
public:
	explicit EaxxAutoWahEffectException(
		const char* message)
		:
		Exception{"EAXX_AUTO_WAH_EFFECT", message}
	{
	}
}; // EaxxAutoWahEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxAutoWahEffect::EaxxAutoWahEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_AUTOWAH)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxAutoWahEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxAutoWahEffect::dispatch(
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

void EaxxAutoWahEffect::set_eax_defaults()
{
	eax_.flAttackTime = ::EAXAUTOWAH_DEFAULTATTACKTIME;
	eax_.flReleaseTime = ::EAXAUTOWAH_DEFAULTRELEASETIME;
	eax_.lResonance = ::EAXAUTOWAH_DEFAULTRESONANCE;
	eax_.lPeakLevel = ::EAXAUTOWAH_DEFAULTPEAKLEVEL;

	eax_d_ = eax_;
}

void EaxxAutoWahEffect::set_efx_attack_time()
{
	const auto attack_time = clamp(
		eax_.flAttackTime,
		AL_AUTOWAH_MIN_ATTACK_TIME,
		AL_AUTOWAH_MAX_ATTACK_TIME
	);

	::alEffectf(efx_effect_object_.get(), AL_AUTOWAH_ATTACK_TIME, attack_time);
}

void EaxxAutoWahEffect::set_efx_release_time()
{
	const auto release_time = clamp(
		eax_.flReleaseTime,
		AL_AUTOWAH_MIN_RELEASE_TIME,
		AL_AUTOWAH_MAX_RELEASE_TIME
	);

	::alEffectf(efx_effect_object_.get(), AL_AUTOWAH_RELEASE_TIME, release_time);
}

void EaxxAutoWahEffect::set_efx_resonance()
{
	const auto resonance = clamp(
		level_mb_to_gain(eax_.lResonance),
		AL_AUTOWAH_MIN_RESONANCE,
		AL_AUTOWAH_MAX_RESONANCE
	);

	::alEffectf(efx_effect_object_.get(), AL_AUTOWAH_RESONANCE, resonance);
}

void EaxxAutoWahEffect::set_efx_peak_gain()
{
	const auto peak_gain = clamp(
		level_mb_to_gain(eax_.lPeakLevel),
		AL_AUTOWAH_MIN_PEAK_GAIN,
		AL_AUTOWAH_MAX_PEAK_GAIN
	);

	::alEffectf(efx_effect_object_.get(), AL_AUTOWAH_PEAK_GAIN, peak_gain);
}

void EaxxAutoWahEffect::set_efx_defaults()
{
	set_efx_attack_time();
	set_efx_release_time();
	set_efx_resonance();
	set_efx_peak_gain();
}

void EaxxAutoWahEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXAUTOWAH_NONE:
			break;

		case ::EAXAUTOWAH_ALLPARAMETERS:
			eax_call.set_value<EaxxAutoWahEffectException>(eax_);
			break;

		case ::EAXAUTOWAH_ATTACKTIME:
			eax_call.set_value<EaxxAutoWahEffectException>(eax_.flAttackTime);
			break;

		case ::EAXAUTOWAH_RELEASETIME:
			eax_call.set_value<EaxxAutoWahEffectException>(eax_.flReleaseTime);
			break;

		case ::EAXAUTOWAH_RESONANCE:
			eax_call.set_value<EaxxAutoWahEffectException>(eax_.lResonance);
			break;

		case ::EAXAUTOWAH_PEAKLEVEL:
			eax_call.set_value<EaxxAutoWahEffectException>(eax_.lPeakLevel);
			break;

		default:
			throw EaxxAutoWahEffectException{"Unsupported property id."};
	}
}

void EaxxAutoWahEffect::validate_attack_time(
	float flAttackTime)
{
	eaxx_validate_range<EaxxAutoWahEffectException>(
		"Attack Time",
		flAttackTime,
		::EAXAUTOWAH_MINATTACKTIME,
		::EAXAUTOWAH_MAXATTACKTIME
	);
}

void EaxxAutoWahEffect::validate_release_time(
	float flReleaseTime)
{
	eaxx_validate_range<EaxxAutoWahEffectException>(
		"Release Time",
		flReleaseTime,
		::EAXAUTOWAH_MINRELEASETIME,
		::EAXAUTOWAH_MAXRELEASETIME
	);
}

void EaxxAutoWahEffect::validate_resonance(
	long lResonance)
{
	eaxx_validate_range<EaxxAutoWahEffectException>(
		"Resonance",
		lResonance,
		::EAXAUTOWAH_MINRESONANCE,
		::EAXAUTOWAH_MAXRESONANCE
	);
}

void EaxxAutoWahEffect::validate_peak_level(
	long lPeakLevel)
{
	eaxx_validate_range<EaxxAutoWahEffectException>(
		"Peak Level",
		lPeakLevel,
		::EAXAUTOWAH_MINPEAKLEVEL,
		::EAXAUTOWAH_MAXPEAKLEVEL
	);
}

void EaxxAutoWahEffect::validate_all(
	const ::EAXAUTOWAHPROPERTIES& eax_all)
{
	validate_attack_time(eax_all.flAttackTime);
	validate_release_time(eax_all.flReleaseTime);
	validate_resonance(eax_all.lResonance);
	validate_peak_level(eax_all.lPeakLevel);
}

void EaxxAutoWahEffect::defer_attack_time(
	float flAttackTime)
{
	eax_d_.flAttackTime = flAttackTime;
	eax_dirty_flags_.flAttackTime = (eax_.flAttackTime != eax_d_.flAttackTime);
}

void EaxxAutoWahEffect::defer_release_time(
	float flReleaseTime)
{
	eax_d_.flReleaseTime = flReleaseTime;
	eax_dirty_flags_.flReleaseTime = (eax_.flReleaseTime != eax_d_.flReleaseTime);
}

void EaxxAutoWahEffect::defer_resonance(
	long lResonance)
{
	eax_d_.lResonance = lResonance;
	eax_dirty_flags_.lResonance = (eax_.lResonance != eax_d_.lResonance);
}

void EaxxAutoWahEffect::defer_peak_level(
	long lPeakLevel)
{
	eax_d_.lPeakLevel = lPeakLevel;
	eax_dirty_flags_.lPeakLevel = (eax_.lPeakLevel != eax_d_.lPeakLevel);
}

void EaxxAutoWahEffect::defer_all(
	const ::EAXAUTOWAHPROPERTIES& eax_all)
{
	validate_all(eax_all);

	defer_attack_time(eax_all.flAttackTime);
	defer_release_time(eax_all.flReleaseTime);
	defer_resonance(eax_all.lResonance);
	defer_peak_level(eax_all.lPeakLevel);
}

void EaxxAutoWahEffect::defer_attack_time(
	const EaxxEaxCall& eax_call)
{
	const auto& attack_time =
		eax_call.get_value<EaxxAutoWahEffectException, const decltype(::EAXAUTOWAHPROPERTIES::flAttackTime)>();

	validate_attack_time(attack_time);
	defer_attack_time(attack_time);
}

void EaxxAutoWahEffect::defer_release_time(
	const EaxxEaxCall& eax_call)
{
	const auto& release_time =
		eax_call.get_value<EaxxAutoWahEffectException, const decltype(::EAXAUTOWAHPROPERTIES::flReleaseTime)>();

	validate_release_time(release_time);
	defer_release_time(release_time);
}

void EaxxAutoWahEffect::defer_resonance(
	const EaxxEaxCall& eax_call)
{
	const auto& resonance =
		eax_call.get_value<EaxxAutoWahEffectException, const decltype(::EAXAUTOWAHPROPERTIES::lResonance)>();

	validate_resonance(resonance);
	defer_resonance(resonance);
}

void EaxxAutoWahEffect::defer_peak_level(
	const EaxxEaxCall& eax_call)
{
	const auto& peak_level =
		eax_call.get_value<EaxxAutoWahEffectException, const decltype(::EAXAUTOWAHPROPERTIES::lPeakLevel)>();

	validate_peak_level(peak_level);
	defer_peak_level(peak_level);
}

void EaxxAutoWahEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<EaxxAutoWahEffectException, const ::EAXAUTOWAHPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxAutoWahEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxAutoWahEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.flAttackTime)
	{
		set_efx_attack_time();
	}

	if (eax_dirty_flags_.flReleaseTime)
	{
		set_efx_release_time();
	}

	if (eax_dirty_flags_.lResonance)
	{
		set_efx_resonance();
	}

	if (eax_dirty_flags_.lPeakLevel)
	{
		set_efx_peak_gain();
	}

	eax_dirty_flags_ = EaxxAutoWahEffectEaxDirtyFlags{};

	load();
}

void EaxxAutoWahEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXAUTOWAH_NONE:
			break;

		case ::EAXAUTOWAH_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXAUTOWAH_ATTACKTIME:
			defer_attack_time(eax_call);
			break;

		case ::EAXAUTOWAH_RELEASETIME:
			defer_release_time(eax_call);
			break;

		case ::EAXAUTOWAH_RESONANCE:
			defer_resonance(eax_call);
			break;

		case ::EAXAUTOWAH_PEAKLEVEL:
			defer_peak_level(eax_call);
			break;

		default:
			throw EaxxAutoWahEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
