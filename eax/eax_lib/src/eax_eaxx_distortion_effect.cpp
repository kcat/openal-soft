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

#include "eax_eaxx_distortion_effect.h"
#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxDistortionEffectEaxDirtyFlags& lhs,
	const EaxxDistortionEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxDistortionEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxDistortionEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxDistortionEffectException :
	public Exception
{
public:
	explicit EaxxDistortionEffectException(
		const char* message)
		:
		Exception{"EAXX_DISTORTION_EFFECT", message}
	{
	}
}; // EaxxDistortionEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxDistortionEffect::EaxxDistortionEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_DISTORTION)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxDistortionEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxDistortionEffect::dispatch(
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

void EaxxDistortionEffect::set_eax_defaults()
{
	eax_.flEdge = ::EAXDISTORTION_DEFAULTEDGE;
	eax_.lGain = ::EAXDISTORTION_DEFAULTGAIN;
	eax_.flLowPassCutOff = ::EAXDISTORTION_DEFAULTLOWPASSCUTOFF;
	eax_.flEQCenter = ::EAXDISTORTION_DEFAULTEQCENTER;
	eax_.flEQBandwidth = ::EAXDISTORTION_DEFAULTEQBANDWIDTH;

	eax_d_ = eax_;
}

void EaxxDistortionEffect::set_efx_edge()
{
	const auto edge = clamp(
		eax_.flEdge,
		AL_DISTORTION_MIN_EDGE,
		AL_DISTORTION_MAX_EDGE
	);

	::alEffectf(efx_effect_object_.get(), AL_DISTORTION_EDGE, edge);
}

void EaxxDistortionEffect::set_efx_gain()
{
	const auto gain = clamp(
		level_mb_to_gain(eax_.lGain),
		AL_DISTORTION_MIN_GAIN,
		AL_DISTORTION_MAX_GAIN
	);

	::alEffectf(efx_effect_object_.get(), AL_DISTORTION_GAIN, gain);
}

void EaxxDistortionEffect::set_efx_low_pass_cutoff()
{
	const auto low_pass_cutoff = clamp(
		eax_.flLowPassCutOff,
		AL_DISTORTION_MIN_LOWPASS_CUTOFF,
		AL_DISTORTION_MAX_LOWPASS_CUTOFF
	);

	::alEffectf(efx_effect_object_.get(), AL_DISTORTION_LOWPASS_CUTOFF, low_pass_cutoff);
}

void EaxxDistortionEffect::set_efx_eq_center()
{
	const auto eq_center = clamp(
		eax_.flEQCenter,
		AL_DISTORTION_MIN_EQCENTER,
		AL_DISTORTION_MAX_EQCENTER
	);

	::alEffectf(efx_effect_object_.get(), AL_DISTORTION_EQCENTER, eq_center);
}

void EaxxDistortionEffect::set_efx_eq_bandwidth()
{
	const auto eq_bandwidth = clamp(
		eax_.flEdge,
		AL_DISTORTION_MIN_EQBANDWIDTH,
		AL_DISTORTION_MAX_EQBANDWIDTH
	);

	::alEffectf(efx_effect_object_.get(), AL_DISTORTION_EQBANDWIDTH, eq_bandwidth);
}

void EaxxDistortionEffect::set_efx_defaults()
{
	set_efx_edge();
	set_efx_gain();
	set_efx_low_pass_cutoff();
	set_efx_eq_center();
	set_efx_eq_bandwidth();
}

void EaxxDistortionEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXDISTORTION_NONE:
			break;

		case ::EAXDISTORTION_ALLPARAMETERS:
			eax_call.set_value<EaxxDistortionEffectException>(eax_);
			break;

		case ::EAXDISTORTION_EDGE:
			eax_call.set_value<EaxxDistortionEffectException>(eax_.flEdge);
			break;

		case ::EAXDISTORTION_GAIN:
			eax_call.set_value<EaxxDistortionEffectException>(eax_.lGain);
			break;

		case ::EAXDISTORTION_LOWPASSCUTOFF:
			eax_call.set_value<EaxxDistortionEffectException>(eax_.flLowPassCutOff);
			break;

		case ::EAXDISTORTION_EQCENTER:
			eax_call.set_value<EaxxDistortionEffectException>(eax_.flEQCenter);
			break;

		case ::EAXDISTORTION_EQBANDWIDTH:
			eax_call.set_value<EaxxDistortionEffectException>(eax_.flEQBandwidth);
			break;

		default:
			throw EaxxDistortionEffectException{"Unsupported property id."};
	}
}

void EaxxDistortionEffect::validate_edge(
	float flEdge)
{
	eaxx_validate_range<EaxxDistortionEffectException>(
		"Edge",
		flEdge,
		::EAXDISTORTION_MINEDGE,
		::EAXDISTORTION_MAXEDGE
	);
}

void EaxxDistortionEffect::validate_gain(
	long lGain)
{
	eaxx_validate_range<EaxxDistortionEffectException>(
		"Gain",
		lGain,
		::EAXDISTORTION_MINGAIN,
		::EAXDISTORTION_MAXGAIN
	);
}

void EaxxDistortionEffect::validate_low_pass_cutoff(
	float flLowPassCutOff)
{
	eaxx_validate_range<EaxxDistortionEffectException>(
		"Low-pass Cut-off",
		flLowPassCutOff,
		::EAXDISTORTION_MINLOWPASSCUTOFF,
		::EAXDISTORTION_MAXLOWPASSCUTOFF
	);
}

void EaxxDistortionEffect::validate_eq_center(
	float flEQCenter)
{
	eaxx_validate_range<EaxxDistortionEffectException>(
		"EQ Center",
		flEQCenter,
		::EAXDISTORTION_MINEQCENTER,
		::EAXDISTORTION_MAXEQCENTER
	);
}

void EaxxDistortionEffect::validate_eq_bandwidth(
	float flEQBandwidth)
{
	eaxx_validate_range<EaxxDistortionEffectException>(
		"EQ Bandwidth",
		flEQBandwidth,
		::EAXDISTORTION_MINEQBANDWIDTH,
		::EAXDISTORTION_MAXEQBANDWIDTH
	);
}

void EaxxDistortionEffect::validate_all(
	const ::EAXDISTORTIONPROPERTIES& eax_all)
{
	validate_edge(eax_all.flEdge);
	validate_gain(eax_all.lGain);
	validate_low_pass_cutoff(eax_all.flLowPassCutOff);
	validate_eq_center(eax_all.flEQCenter);
	validate_eq_bandwidth(eax_all.flEQBandwidth);
}

void EaxxDistortionEffect::defer_edge(
	float flEdge)
{
	eax_d_.flEdge = flEdge;
	eax_dirty_flags_.flEdge = (eax_.flEdge != eax_d_.flEdge);
}

void EaxxDistortionEffect::defer_gain(
	long lGain)
{
	eax_d_.lGain = lGain;
	eax_dirty_flags_.lGain = (eax_.lGain != eax_d_.lGain);
}

void EaxxDistortionEffect::defer_low_pass_cutoff(
	float flLowPassCutOff)
{
	eax_d_.flLowPassCutOff = flLowPassCutOff;
	eax_dirty_flags_.flLowPassCutOff = (eax_.flLowPassCutOff != eax_d_.flLowPassCutOff);
}

void EaxxDistortionEffect::defer_eq_center(
	float flEQCenter)
{
	eax_d_.flEQCenter = flEQCenter;
	eax_dirty_flags_.flEQCenter = (eax_.flEQCenter != eax_d_.flEQCenter);
}

void EaxxDistortionEffect::defer_eq_bandwidth(
	float flEQBandwidth)
{
	eax_d_.flEQBandwidth = flEQBandwidth;
	eax_dirty_flags_.flEQBandwidth = (eax_.flEQBandwidth != eax_d_.flEQBandwidth);
}

void EaxxDistortionEffect::defer_all(
	const ::EAXDISTORTIONPROPERTIES& eax_all)
{
	defer_edge(eax_all.flEdge);
	defer_gain(eax_all.lGain);
	defer_low_pass_cutoff(eax_all.flLowPassCutOff);
	defer_eq_center(eax_all.flEQCenter);
	defer_eq_bandwidth(eax_all.flEQBandwidth);
}

void EaxxDistortionEffect::defer_edge(
	const EaxxEaxCall& eax_call)
{
	const auto& edge =
		eax_call.get_value<EaxxDistortionEffectException, const decltype(::EAXDISTORTIONPROPERTIES::flEdge)>();

	validate_edge(edge);
	defer_edge(edge);
}

void EaxxDistortionEffect::defer_gain(
	const EaxxEaxCall& eax_call)
{
	const auto& gain =
		eax_call.get_value<EaxxDistortionEffectException, const decltype(::EAXDISTORTIONPROPERTIES::lGain)>();

	validate_gain(gain);
	defer_gain(gain);
}

void EaxxDistortionEffect::defer_low_pass_cutoff(
	const EaxxEaxCall& eax_call)
{
	const auto& low_pass_cutoff =
		eax_call.get_value<EaxxDistortionEffectException, const decltype(::EAXDISTORTIONPROPERTIES::flLowPassCutOff)>();

	validate_low_pass_cutoff(low_pass_cutoff);
	defer_low_pass_cutoff(low_pass_cutoff);
}

void EaxxDistortionEffect::defer_eq_center(
	const EaxxEaxCall& eax_call)
{
	const auto& eq_center =
		eax_call.get_value<EaxxDistortionEffectException, const decltype(::EAXDISTORTIONPROPERTIES::flEQCenter)>();

	validate_eq_center(eq_center);
	defer_eq_center(eq_center);
}

void EaxxDistortionEffect::defer_eq_bandwidth(
	const EaxxEaxCall& eax_call)
{
	const auto& eq_bandwidth =
		eax_call.get_value<EaxxDistortionEffectException, const decltype(::EAXDISTORTIONPROPERTIES::flEQBandwidth)>();

	validate_eq_bandwidth(eq_bandwidth);
	defer_eq_bandwidth(eq_bandwidth);
}

void EaxxDistortionEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<EaxxDistortionEffectException, const ::EAXDISTORTIONPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxDistortionEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxDistortionEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.flEdge)
	{
		set_efx_edge();
	}

	if (eax_dirty_flags_.lGain)
	{
		set_efx_gain();
	}

	if (eax_dirty_flags_.flLowPassCutOff)
	{
		set_efx_low_pass_cutoff();
	}

	if (eax_dirty_flags_.flEQCenter)
	{
		set_efx_eq_center();
	}

	if (eax_dirty_flags_.flEQBandwidth)
	{
		set_efx_eq_bandwidth();
	}

	eax_dirty_flags_ = EaxxDistortionEffectEaxDirtyFlags{};

	load();
}

void EaxxDistortionEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXDISTORTION_NONE:
			break;

		case ::EAXDISTORTION_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXDISTORTION_EDGE:
			defer_edge(eax_call);
			break;

		case ::EAXDISTORTION_GAIN:
			defer_gain(eax_call);
			break;

		case ::EAXDISTORTION_LOWPASSCUTOFF:
			defer_low_pass_cutoff(eax_call);
			break;

		case ::EAXDISTORTION_EQCENTER:
			defer_eq_center(eax_call);
			break;

		case ::EAXDISTORTION_EQBANDWIDTH:
			defer_eq_bandwidth(eax_call);
			break;

		default:
			throw EaxxDistortionEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
