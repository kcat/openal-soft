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
#include "eax_eaxx_ring_modulator_effect.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxRingModulatorEffectEaxDirtyFlags& lhs,
	const EaxxRingModulatorEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxRingModulatorEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxRingModulatorEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxRingModulatorEffectException :
	public Exception
{
public:
	explicit EaxxRingModulatorEffectException(
		const char* message)
		:
		Exception{"EAXX_RING_MODULATOR_EFFECT", message}
	{
	}
}; // EaxxRingModulatorEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxRingModulatorEffect::EaxxRingModulatorEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_RING_MODULATOR)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxRingModulatorEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxRingModulatorEffect::dispatch(
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

void EaxxRingModulatorEffect::set_eax_defaults()
{
	eax_.flFrequency = ::EAXRINGMODULATOR_DEFAULTFREQUENCY;
	eax_.flHighPassCutOff = ::EAXRINGMODULATOR_DEFAULTHIGHPASSCUTOFF;
	eax_.ulWaveform = ::EAXRINGMODULATOR_DEFAULTWAVEFORM;

	eax_d_ = eax_;
}

void EaxxRingModulatorEffect::set_efx_frequency()
{
	const auto frequency = clamp(
		eax_.flFrequency,
		AL_RING_MODULATOR_MIN_FREQUENCY,
		AL_RING_MODULATOR_MAX_FREQUENCY
	);

	::alEffectf(efx_effect_object_.get(), AL_RING_MODULATOR_FREQUENCY, frequency);
}

void EaxxRingModulatorEffect::set_efx_high_pass_cutoff()
{
	const auto high_pass_cutoff = clamp(
		eax_.flHighPassCutOff,
		AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF,
		AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF
	);

	::alEffectf(efx_effect_object_.get(), AL_RING_MODULATOR_HIGHPASS_CUTOFF, high_pass_cutoff);
}

void EaxxRingModulatorEffect::set_efx_waveform()
{
	const auto waveform = clamp(
		static_cast<::ALint>(eax_.ulWaveform),
		AL_RING_MODULATOR_MIN_WAVEFORM,
		AL_RING_MODULATOR_MAX_WAVEFORM
	);

	::alEffecti(efx_effect_object_.get(), AL_RING_MODULATOR_WAVEFORM, waveform);
}

void EaxxRingModulatorEffect::set_efx_defaults()
{
	set_efx_frequency();
	set_efx_high_pass_cutoff();
	set_efx_waveform();
}

void EaxxRingModulatorEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXRINGMODULATOR_NONE:
			break;

		case ::EAXRINGMODULATOR_ALLPARAMETERS:
			eax_call.set_value<EaxxRingModulatorEffectException>(eax_);
			break;

		case ::EAXRINGMODULATOR_FREQUENCY:
			eax_call.set_value<EaxxRingModulatorEffectException>(eax_.flFrequency);
			break;

		case ::EAXRINGMODULATOR_HIGHPASSCUTOFF:
			eax_call.set_value<EaxxRingModulatorEffectException>(eax_.flHighPassCutOff);
			break;

		case ::EAXRINGMODULATOR_WAVEFORM:
			eax_call.set_value<EaxxRingModulatorEffectException>(eax_.ulWaveform);
			break;

		default:
			throw EaxxRingModulatorEffectException{"Unsupported property id."};
	}
}

void EaxxRingModulatorEffect::validate_frequency(
	float flFrequency)
{
	eaxx_validate_range<EaxxRingModulatorEffectException>(
		"Frequency",
		flFrequency,
		::EAXRINGMODULATOR_MINFREQUENCY,
		::EAXRINGMODULATOR_MAXFREQUENCY
	);
}

void EaxxRingModulatorEffect::validate_high_pass_cutoff(
	float flHighPassCutOff)
{
	eaxx_validate_range<EaxxRingModulatorEffectException>(
		"High-Pass Cutoff",
		flHighPassCutOff,
		::EAXRINGMODULATOR_MINHIGHPASSCUTOFF,
		::EAXRINGMODULATOR_MAXHIGHPASSCUTOFF
	);
}

void EaxxRingModulatorEffect::validate_waveform(
	unsigned long ulWaveform)
{
	eaxx_validate_range<EaxxRingModulatorEffectException>(
		"Waveform",
		ulWaveform,
		::EAXRINGMODULATOR_MINWAVEFORM,
		::EAXRINGMODULATOR_MAXWAVEFORM
	);
}

void EaxxRingModulatorEffect::validate_all(
	const ::EAXRINGMODULATORPROPERTIES& all)
{
	validate_frequency(all.flFrequency);
	validate_high_pass_cutoff(all.flHighPassCutOff);
	validate_waveform(all.ulWaveform);
}

void EaxxRingModulatorEffect::defer_frequency(
	float flFrequency)
{
	eax_d_.flFrequency = flFrequency;
	eax_dirty_flags_.flFrequency = (eax_.flFrequency != eax_d_.flFrequency);
}

void EaxxRingModulatorEffect::defer_high_pass_cutoff(
	float flHighPassCutOff)
{
	eax_d_.flHighPassCutOff = flHighPassCutOff;
	eax_dirty_flags_.flHighPassCutOff = (eax_.flHighPassCutOff != eax_d_.flHighPassCutOff);
}

void EaxxRingModulatorEffect::defer_waveform(
	unsigned long ulWaveform)
{
	eax_d_.ulWaveform = ulWaveform;
	eax_dirty_flags_.ulWaveform = (eax_.ulWaveform != eax_d_.ulWaveform);
}

void EaxxRingModulatorEffect::defer_all(
	const ::EAXRINGMODULATORPROPERTIES& all)
{
	defer_frequency(all.flFrequency);
	defer_high_pass_cutoff(all.flHighPassCutOff);
	defer_waveform(all.ulWaveform);
}

void EaxxRingModulatorEffect::defer_frequency(
	const EaxxEaxCall& eax_call)
{
	const auto& frequency =
		eax_call.get_value<
			EaxxRingModulatorEffectException, const decltype(::EAXRINGMODULATORPROPERTIES::flFrequency)>();

	validate_frequency(frequency);
	defer_frequency(frequency);
}

void EaxxRingModulatorEffect::defer_high_pass_cutoff(
	const EaxxEaxCall& eax_call)
{
	const auto& high_pass_cutoff =
		eax_call.get_value<
			EaxxRingModulatorEffectException, const decltype(::EAXRINGMODULATORPROPERTIES::flHighPassCutOff)>();

	validate_high_pass_cutoff(high_pass_cutoff);
	defer_high_pass_cutoff(high_pass_cutoff);
}

void EaxxRingModulatorEffect::defer_waveform(
	const EaxxEaxCall& eax_call)
{
	const auto& waveform =
		eax_call.get_value<
			EaxxRingModulatorEffectException, const decltype(::EAXRINGMODULATORPROPERTIES::ulWaveform)>();

	validate_waveform(waveform);
	defer_waveform(waveform);
}

void EaxxRingModulatorEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<EaxxRingModulatorEffectException, const ::EAXRINGMODULATORPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxRingModulatorEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxRingModulatorEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.flFrequency)
	{
		set_efx_frequency();
	}

	if (eax_dirty_flags_.flHighPassCutOff)
	{
		set_efx_high_pass_cutoff();
	}

	if (eax_dirty_flags_.ulWaveform)
	{
		set_efx_waveform();
	}

	eax_dirty_flags_ = EaxxRingModulatorEffectEaxDirtyFlags{};

	load();
}

void EaxxRingModulatorEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXRINGMODULATOR_NONE:
			break;

		case ::EAXRINGMODULATOR_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXRINGMODULATOR_FREQUENCY:
			defer_frequency(eax_call);
			break;

		case ::EAXRINGMODULATOR_HIGHPASSCUTOFF:
			defer_high_pass_cutoff(eax_call);
			break;

		case ::EAXRINGMODULATOR_WAVEFORM:
			defer_waveform(eax_call);
			break;

		default:
			throw EaxxRingModulatorEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
