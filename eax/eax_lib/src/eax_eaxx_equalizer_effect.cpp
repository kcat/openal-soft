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

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_equalizer_effect.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxEqualizerEffectEaxDirtyFlags& lhs,
	const EaxxEqualizerEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxEqualizerEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxEqualizerEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEqualizerEffectException :
	public Exception
{
public:
	explicit EaxxEqualizerEffectException(
		const char* message)
		:
		Exception{"EAXX_EQUALIZER_EFFECT", message}
	{
	}
}; // EaxxEqualizerEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxEqualizerEffect::EaxxEqualizerEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_EQUALIZER)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxEqualizerEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxEqualizerEffect::dispatch(
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

void EaxxEqualizerEffect::set_eax_defaults()
{
	eax_.lLowGain = ::EAXEQUALIZER_DEFAULTLOWGAIN;
	eax_.flLowCutOff = ::EAXEQUALIZER_DEFAULTLOWCUTOFF;
	eax_.lMid1Gain = ::EAXEQUALIZER_DEFAULTMID1GAIN;
	eax_.flMid1Center = ::EAXEQUALIZER_DEFAULTMID1CENTER;
	eax_.flMid1Width = ::EAXEQUALIZER_DEFAULTMID1WIDTH;
	eax_.lMid2Gain = ::EAXEQUALIZER_DEFAULTMID2GAIN;
	eax_.flMid2Center = ::EAXEQUALIZER_DEFAULTMID2CENTER;
	eax_.flMid2Width = ::EAXEQUALIZER_DEFAULTMID2WIDTH;
	eax_.lHighGain = ::EAXEQUALIZER_DEFAULTHIGHGAIN;
	eax_.flHighCutOff = ::EAXEQUALIZER_DEFAULTHIGHCUTOFF;

	eax_d_ = eax_;
}

void EaxxEqualizerEffect::set_efx_low_gain()
{
	const auto low_gain = clamp(
		level_mb_to_gain(eax_.lLowGain),
		AL_EQUALIZER_MIN_LOW_GAIN,
		AL_EQUALIZER_MAX_LOW_GAIN
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_LOW_GAIN, low_gain);
}

void EaxxEqualizerEffect::set_efx_low_cutoff()
{
	const auto low_cutoff = clamp(
		eax_.flLowCutOff,
		AL_EQUALIZER_MIN_LOW_CUTOFF,
		AL_EQUALIZER_MAX_LOW_CUTOFF
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_LOW_CUTOFF, low_cutoff);
}

void EaxxEqualizerEffect::set_efx_mid1_gain()
{
	const auto mid1_gain = clamp(
		level_mb_to_gain(eax_.lMid1Gain),
		AL_EQUALIZER_MIN_MID1_GAIN,
		AL_EQUALIZER_MAX_MID1_GAIN
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_MID1_GAIN, mid1_gain);
}

void EaxxEqualizerEffect::set_efx_mid1_center()
{
	const auto mid1_center = clamp(
		eax_.flMid1Center,
		AL_EQUALIZER_MIN_MID1_CENTER,
		AL_EQUALIZER_MAX_MID1_CENTER
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_MID1_CENTER, mid1_center);
}

void EaxxEqualizerEffect::set_efx_mid1_width()
{
	const auto mid1_width = clamp(
		eax_.flMid1Width,
		AL_EQUALIZER_MIN_MID1_WIDTH,
		AL_EQUALIZER_MAX_MID1_WIDTH
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_MID1_WIDTH, mid1_width);
}

void EaxxEqualizerEffect::set_efx_mid2_gain()
{
	const auto mid2_gain = clamp(
		level_mb_to_gain(eax_.lMid2Gain),
		AL_EQUALIZER_MIN_MID2_GAIN,
		AL_EQUALIZER_MAX_MID2_GAIN
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_MID2_GAIN, mid2_gain);
}

void EaxxEqualizerEffect::set_efx_mid2_center()
{
	const auto mid2_center = clamp(
		eax_.flMid2Center,
		AL_EQUALIZER_MIN_MID2_CENTER,
		AL_EQUALIZER_MAX_MID2_CENTER
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_MID2_CENTER, mid2_center);
}

void EaxxEqualizerEffect::set_efx_mid2_width()
{
	const auto mid2_width = clamp(
		eax_.flMid2Width,
		AL_EQUALIZER_MIN_MID2_WIDTH,
		AL_EQUALIZER_MAX_MID2_WIDTH
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_MID2_WIDTH, mid2_width);
}

void EaxxEqualizerEffect::set_efx_high_gain()
{
	const auto high_gain = clamp(
		level_mb_to_gain(eax_.lHighGain),
		AL_EQUALIZER_MIN_HIGH_GAIN,
		AL_EQUALIZER_MAX_HIGH_GAIN
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_HIGH_GAIN, high_gain);
}

void EaxxEqualizerEffect::set_efx_high_cutoff()
{
	const auto high_cutoff = clamp(
		eax_.flHighCutOff,
		AL_EQUALIZER_MIN_HIGH_CUTOFF,
		AL_EQUALIZER_MAX_HIGH_CUTOFF
	);

	::alEffectf(efx_effect_object_.get(), AL_EQUALIZER_HIGH_CUTOFF, high_cutoff);
}

void EaxxEqualizerEffect::set_efx_defaults()
{
	set_efx_low_gain();
	set_efx_low_cutoff();
	set_efx_mid1_gain();
	set_efx_mid1_center();
	set_efx_mid1_width();
	set_efx_mid2_gain();
	set_efx_mid2_center();
	set_efx_mid2_width();
	set_efx_high_gain();
	set_efx_high_cutoff();
}

void EaxxEqualizerEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXEQUALIZER_NONE:
			break;

		case ::EAXEQUALIZER_ALLPARAMETERS:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_);
			break;

		case ::EAXEQUALIZER_LOWGAIN:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.lLowGain);
			break;

		case ::EAXEQUALIZER_LOWCUTOFF:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.flLowCutOff);
			break;

		case ::EAXEQUALIZER_MID1GAIN:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.lMid1Gain);
			break;

		case ::EAXEQUALIZER_MID1CENTER:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.flMid1Center);
			break;

		case ::EAXEQUALIZER_MID1WIDTH:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.flMid1Width);
			break;

		case ::EAXEQUALIZER_MID2GAIN:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.lMid2Gain);
			break;

		case ::EAXEQUALIZER_MID2CENTER:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.flMid2Center);
			break;

		case ::EAXEQUALIZER_MID2WIDTH:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.flMid2Width);
			break;

		case ::EAXEQUALIZER_HIGHGAIN:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.lHighGain);
			break;

		case ::EAXEQUALIZER_HIGHCUTOFF:
			eax_call.set_value<EaxxEqualizerEffectException>(eax_.flHighCutOff);
			break;

		default:
			throw EaxxEqualizerEffectException{"Unsupported property id."};
	}
}

void EaxxEqualizerEffect::validate_low_gain(
	long lLowGain)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"Low Gain",
		lLowGain,
		::EAXEQUALIZER_MINLOWGAIN,
		::EAXEQUALIZER_MAXLOWGAIN
	);
}

void EaxxEqualizerEffect::validate_low_cutoff(
	float flLowCutOff)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"Low Cutoff",
		flLowCutOff,
		::EAXEQUALIZER_MINLOWCUTOFF,
		::EAXEQUALIZER_MAXLOWCUTOFF
	);
}

void EaxxEqualizerEffect::validate_mid1_gain(
	long lMid1Gain)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"Mid1 Gain",
		lMid1Gain,
		::EAXEQUALIZER_MINMID1GAIN,
		::EAXEQUALIZER_MAXMID1GAIN
	);
}

void EaxxEqualizerEffect::validate_mid1_center(
	float flMid1Center)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"Mid1 Center",
		flMid1Center,
		::EAXEQUALIZER_MINMID1CENTER,
		::EAXEQUALIZER_MAXMID1CENTER
	);
}

void EaxxEqualizerEffect::validate_mid1_width(
	float flMid1Width)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"Mid1 Width",
		flMid1Width,
		::EAXEQUALIZER_MINMID1WIDTH,
		::EAXEQUALIZER_MAXMID1WIDTH
	);
}

void EaxxEqualizerEffect::validate_mid2_gain(
	long lMid2Gain)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"Mid2 Gain",
		lMid2Gain,
		::EAXEQUALIZER_MINMID2GAIN,
		::EAXEQUALIZER_MAXMID2GAIN
	);
}

void EaxxEqualizerEffect::validate_mid2_center(
	float flMid2Center)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"Mid2 Center",
		flMid2Center,
		::EAXEQUALIZER_MINMID2CENTER,
		::EAXEQUALIZER_MAXMID2CENTER
	);
}

void EaxxEqualizerEffect::validate_mid2_width(
	float flMid2Width)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"Mid2 Width",
		flMid2Width,
		::EAXEQUALIZER_MINMID2WIDTH,
		::EAXEQUALIZER_MAXMID2WIDTH
	);
}

void EaxxEqualizerEffect::validate_high_gain(
	long lHighGain)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"High Gain",
		lHighGain,
		::EAXEQUALIZER_MINHIGHGAIN,
		::EAXEQUALIZER_MAXHIGHGAIN
	);
}

void EaxxEqualizerEffect::validate_high_cutoff(
	float flHighCutOff)
{
	eaxx_validate_range<EaxxEqualizerEffectException>(
		"High Cutoff",
		flHighCutOff,
		::EAXEQUALIZER_MINHIGHCUTOFF,
		::EAXEQUALIZER_MAXHIGHCUTOFF
	);
}

void EaxxEqualizerEffect::validate_all(
	const ::EAXEQUALIZERPROPERTIES& all)
{
	validate_low_gain(all.lLowGain);
	validate_low_cutoff(all.flLowCutOff);
	validate_mid1_gain(all.lMid1Gain);
	validate_mid1_center(all.flMid1Center);
	validate_mid1_width(all.flMid1Width);
	validate_mid2_gain(all.lMid2Gain);
	validate_mid2_center(all.flMid2Center);
	validate_mid2_width(all.flMid2Width);
	validate_high_gain(all.lHighGain);
	validate_high_cutoff(all.flHighCutOff);
}

void EaxxEqualizerEffect::defer_low_gain(
	long lLowGain)
{
	eax_d_.lLowGain = lLowGain;
	eax_dirty_flags_.lLowGain = (eax_.lLowGain != eax_d_.lLowGain);
}

void EaxxEqualizerEffect::defer_low_cutoff(
	float flLowCutOff)
{
	eax_d_.flLowCutOff = flLowCutOff;
	eax_dirty_flags_.flLowCutOff = (eax_.flLowCutOff != eax_d_.flLowCutOff);
}

void EaxxEqualizerEffect::defer_mid1_gain(
	long lMid1Gain)
{
	eax_d_.lMid1Gain = lMid1Gain;
	eax_dirty_flags_.lMid1Gain = (eax_.lMid1Gain != eax_d_.lMid1Gain);
}

void EaxxEqualizerEffect::defer_mid1_center(
	float flMid1Center)
{
	eax_d_.flMid1Center = flMid1Center;
	eax_dirty_flags_.flMid1Center = (eax_.flMid1Center != eax_d_.flMid1Center);
}

void EaxxEqualizerEffect::defer_mid1_width(
	float flMid1Width)
{
	eax_d_.flMid1Width = flMid1Width;
	eax_dirty_flags_.flMid1Width = (eax_.flMid1Width != eax_d_.flMid1Width);
}

void EaxxEqualizerEffect::defer_mid2_gain(
	long lMid2Gain)
{
	eax_d_.lMid2Gain = lMid2Gain;
	eax_dirty_flags_.lMid2Gain = (eax_.lMid2Gain != eax_d_.lMid2Gain);
}

void EaxxEqualizerEffect::defer_mid2_center(
	float flMid2Center)
{
	eax_d_.flMid2Center = flMid2Center;
	eax_dirty_flags_.flMid2Center = (eax_.flMid2Center != eax_d_.flMid2Center);
}

void EaxxEqualizerEffect::defer_mid2_width(
	float flMid2Width)
{
	eax_d_.flMid2Width = flMid2Width;
	eax_dirty_flags_.flMid2Width = (eax_.flMid2Width != eax_d_.flMid2Width);
}

void EaxxEqualizerEffect::defer_high_gain(
	long lHighGain)
{
	eax_d_.lHighGain = lHighGain;
	eax_dirty_flags_.lHighGain = (eax_.lHighGain != eax_d_.lHighGain);
}

void EaxxEqualizerEffect::defer_high_cutoff(
	float flHighCutOff)
{
	eax_d_.flHighCutOff = flHighCutOff;
	eax_dirty_flags_.flHighCutOff = (eax_.flHighCutOff != eax_d_.flHighCutOff);
}

void EaxxEqualizerEffect::defer_all(
	const ::EAXEQUALIZERPROPERTIES& all)
{
	defer_low_gain(all.lLowGain);
	defer_low_cutoff(all.flLowCutOff);
	defer_mid1_gain(all.lMid1Gain);
	defer_mid1_center(all.flMid1Center);
	defer_mid1_width(all.flMid1Width);
	defer_mid2_gain(all.lMid2Gain);
	defer_mid2_center(all.flMid2Center);
	defer_mid2_width(all.flMid2Width);
	defer_high_gain(all.lHighGain);
	defer_high_cutoff(all.flHighCutOff);
}

void EaxxEqualizerEffect::defer_low_gain(
	const EaxxEaxCall& eax_call)
{
	const auto& low_gain =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::lLowGain)>();

	validate_low_gain(low_gain);
	defer_low_gain(low_gain);
}

void EaxxEqualizerEffect::defer_low_cutoff(
	const EaxxEaxCall& eax_call)
{
	const auto& low_cutoff =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::flLowCutOff)>();

	validate_low_cutoff(low_cutoff);
	defer_low_cutoff(low_cutoff);
}

void EaxxEqualizerEffect::defer_mid1_gain(
	const EaxxEaxCall& eax_call)
{
	const auto& mid1_gain =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::lMid1Gain)>();

	validate_mid1_gain(mid1_gain);
	defer_mid1_gain(mid1_gain);
}

void EaxxEqualizerEffect::defer_mid1_center(
	const EaxxEaxCall& eax_call)
{
	const auto& mid1_center =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::flMid1Center)>();

	validate_mid1_center(mid1_center);
	defer_mid1_center(mid1_center);
}

void EaxxEqualizerEffect::defer_mid1_width(
	const EaxxEaxCall& eax_call)
{
	const auto& mid1_width =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::flMid1Width)>();

	validate_mid1_width(mid1_width);
	defer_mid1_width(mid1_width);
}

void EaxxEqualizerEffect::defer_mid2_gain(
	const EaxxEaxCall& eax_call)
{
	const auto& mid2_gain =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::lMid2Gain)>();

	validate_mid2_gain(mid2_gain);
	defer_mid2_gain(mid2_gain);
}

void EaxxEqualizerEffect::defer_mid2_center(
	const EaxxEaxCall& eax_call)
{
	const auto& mid2_center =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::flMid2Center)>();

	validate_mid2_center(mid2_center);
	defer_mid2_center(mid2_center);
}

void EaxxEqualizerEffect::defer_mid2_width(
	const EaxxEaxCall& eax_call)
{
	const auto& mid2_width =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::flMid2Width)>();

	validate_mid2_width(mid2_width);
	defer_mid2_width(mid2_width);
}

void EaxxEqualizerEffect::defer_high_gain(
	const EaxxEaxCall& eax_call)
{
	const auto& high_gain =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::lHighGain)>();

	validate_high_gain(high_gain);
	defer_high_gain(high_gain);
}

void EaxxEqualizerEffect::defer_high_cutoff(
	const EaxxEaxCall& eax_call)
{
	const auto& high_cutoff =
		eax_call.get_value<EaxxEqualizerEffectException, const decltype(::EAXEQUALIZERPROPERTIES::flHighCutOff)>();

	validate_high_cutoff(high_cutoff);
	defer_high_cutoff(high_cutoff);
}

void EaxxEqualizerEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<EaxxEqualizerEffectException, const ::EAXEQUALIZERPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxEqualizerEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxEqualizerEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.lLowGain)
	{
		set_efx_low_gain();
	}

	if (eax_dirty_flags_.flLowCutOff)
	{
		set_efx_low_cutoff();
	}

	if (eax_dirty_flags_.lMid1Gain)
	{
		set_efx_mid1_gain();
	}

	if (eax_dirty_flags_.flMid1Center)
	{
		set_efx_mid1_center();
	}

	if (eax_dirty_flags_.flMid1Width)
	{
		set_efx_mid1_width();
	}

	if (eax_dirty_flags_.lMid2Gain)
	{
		set_efx_mid2_gain();
	}

	if (eax_dirty_flags_.flMid2Center)
	{
		set_efx_mid2_center();
	}

	if (eax_dirty_flags_.flMid2Width)
	{
		set_efx_mid2_width();
	}

	if (eax_dirty_flags_.lHighGain)
	{
		set_efx_high_gain();
	}

	if (eax_dirty_flags_.flHighCutOff)
	{
		set_efx_high_cutoff();
	}

	eax_dirty_flags_ = EaxxEqualizerEffectEaxDirtyFlags{};

	load();
}

void EaxxEqualizerEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXEQUALIZER_NONE:
			break;

		case ::EAXEQUALIZER_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXEQUALIZER_LOWGAIN:
			defer_low_gain(eax_call);
			break;

		case ::EAXEQUALIZER_LOWCUTOFF:
			defer_low_cutoff(eax_call);
			break;

		case ::EAXEQUALIZER_MID1GAIN:
			defer_mid1_gain(eax_call);
			break;

		case ::EAXEQUALIZER_MID1CENTER:
			defer_mid1_center(eax_call);
			break;

		case ::EAXEQUALIZER_MID1WIDTH:
			defer_mid1_width(eax_call);
			break;

		case ::EAXEQUALIZER_MID2GAIN:
			defer_mid2_gain(eax_call);
			break;

		case ::EAXEQUALIZER_MID2CENTER:
			defer_mid2_center(eax_call);
			break;

		case ::EAXEQUALIZER_MID2WIDTH:
			defer_mid2_width(eax_call);
			break;

		case ::EAXEQUALIZER_HIGHGAIN:
			defer_high_gain(eax_call);
			break;

		case ::EAXEQUALIZER_HIGHCUTOFF:
			defer_high_cutoff(eax_call);
			break;

		default:
			throw EaxxEqualizerEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
