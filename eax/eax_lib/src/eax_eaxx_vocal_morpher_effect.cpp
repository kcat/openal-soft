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
#include "eax_eaxx_vocal_morpher_effect.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxVocalMorpherEffectEaxDirtyFlags& lhs,
	const EaxxVocalMorpherEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxVocalMorpherEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxVocalMorpherEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxVocalMorpherEffectException :
	public Exception
{
public:
	explicit EaxxVocalMorpherEffectException(
		const char* message)
		:
		Exception{"EAXX_VOCAL_MORPHER_EFFECT", message}
	{
	}
}; // EaxxVocalMorpherEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxVocalMorpherEffect::EaxxVocalMorpherEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_VOCAL_MORPHER)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxVocalMorpherEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxVocalMorpherEffect::dispatch(
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

void EaxxVocalMorpherEffect::set_eax_defaults()
{
	eax_.ulPhonemeA = ::EAXVOCALMORPHER_DEFAULTPHONEMEA;
	eax_.lPhonemeACoarseTuning = ::EAXVOCALMORPHER_DEFAULTPHONEMEACOARSETUNING;
	eax_.ulPhonemeB = ::EAXVOCALMORPHER_DEFAULTPHONEMEB;
	eax_.lPhonemeBCoarseTuning = ::EAXVOCALMORPHER_DEFAULTPHONEMEBCOARSETUNING;
	eax_.ulWaveform = ::EAXVOCALMORPHER_DEFAULTWAVEFORM;
	eax_.flRate = ::EAXVOCALMORPHER_DEFAULTRATE;

	eax_d_ = eax_;
}

void EaxxVocalMorpherEffect::set_efx_phoneme_a()
{
	const auto phoneme_a = clamp(
		static_cast<::ALint>(eax_.ulPhonemeA),
		AL_VOCAL_MORPHER_MIN_PHONEMEA,
		AL_VOCAL_MORPHER_MAX_PHONEMEA
	);

	::alEffecti(efx_effect_object_.get(), AL_VOCAL_MORPHER_PHONEMEA, phoneme_a);
}

void EaxxVocalMorpherEffect::set_efx_phoneme_a_coarse_tuning()
{
	const auto phoneme_a_coarse_tuning = clamp(
		static_cast<::ALint>(eax_.lPhonemeACoarseTuning),
		AL_VOCAL_MORPHER_MIN_PHONEMEA_COARSE_TUNING,
		AL_VOCAL_MORPHER_MAX_PHONEMEA_COARSE_TUNING
	);

	::alEffecti(efx_effect_object_.get(), AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING, phoneme_a_coarse_tuning);
}

void EaxxVocalMorpherEffect::set_efx_phoneme_b()
{
	const auto phoneme_b = clamp(
		static_cast<::ALint>(eax_.ulPhonemeB),
		AL_VOCAL_MORPHER_MIN_PHONEMEB,
		AL_VOCAL_MORPHER_MAX_PHONEMEB
	);

	::alEffecti(efx_effect_object_.get(), AL_VOCAL_MORPHER_PHONEMEB, phoneme_b);
}

void EaxxVocalMorpherEffect::set_efx_phoneme_b_coarse_tuning()
{
	const auto phoneme_b_coarse_tuning = clamp(
		static_cast<::ALint>(eax_.lPhonemeBCoarseTuning),
		AL_VOCAL_MORPHER_MIN_PHONEMEB_COARSE_TUNING,
		AL_VOCAL_MORPHER_MAX_PHONEMEB_COARSE_TUNING
	);

	::alEffecti(efx_effect_object_.get(), AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING, phoneme_b_coarse_tuning);
}

void EaxxVocalMorpherEffect::set_efx_waveform()
{
	const auto waveform = clamp(
		static_cast<::ALint>(eax_.ulWaveform),
		AL_VOCAL_MORPHER_MIN_WAVEFORM,
		AL_VOCAL_MORPHER_MAX_WAVEFORM
	);

	::alEffecti(efx_effect_object_.get(), AL_VOCAL_MORPHER_WAVEFORM, waveform);
}

void EaxxVocalMorpherEffect::set_efx_rate()
{
	const auto rate = clamp(
		eax_.flRate,
		AL_VOCAL_MORPHER_MIN_RATE,
		AL_VOCAL_MORPHER_MAX_RATE
	);

	::alEffectf(efx_effect_object_.get(), AL_VOCAL_MORPHER_RATE, rate);
}

void EaxxVocalMorpherEffect::set_efx_defaults()
{
	set_efx_phoneme_a();
	set_efx_phoneme_a_coarse_tuning();
	set_efx_phoneme_b();
	set_efx_phoneme_b_coarse_tuning();
	set_efx_waveform();
	set_efx_rate();
}

void EaxxVocalMorpherEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXVOCALMORPHER_NONE:
			break;

		case ::EAXVOCALMORPHER_ALLPARAMETERS:
			eax_call.set_value<EaxxVocalMorpherEffectException>(eax_);
			break;

		case ::EAXVOCALMORPHER_PHONEMEA:
			eax_call.set_value<EaxxVocalMorpherEffectException>(eax_.ulPhonemeA);
			break;

		case ::EAXVOCALMORPHER_PHONEMEACOARSETUNING:
			eax_call.set_value<EaxxVocalMorpherEffectException>(eax_.lPhonemeACoarseTuning);
			break;

		case ::EAXVOCALMORPHER_PHONEMEB:
			eax_call.set_value<EaxxVocalMorpherEffectException>(eax_.ulPhonemeB);
			break;

		case ::EAXVOCALMORPHER_PHONEMEBCOARSETUNING:
			eax_call.set_value<EaxxVocalMorpherEffectException>(eax_.lPhonemeBCoarseTuning);
			break;

		case ::EAXVOCALMORPHER_WAVEFORM:
			eax_call.set_value<EaxxVocalMorpherEffectException>(eax_.ulWaveform);
			break;

		case ::EAXVOCALMORPHER_RATE:
			eax_call.set_value<EaxxVocalMorpherEffectException>(eax_.flRate);
			break;

		default:
			throw EaxxVocalMorpherEffectException{"Unsupported property id."};
	}
}

void EaxxVocalMorpherEffect::validate_phoneme_a(
	unsigned long ulPhonemeA)
{
	eaxx_validate_range<EaxxVocalMorpherEffectException>(
		"Phoneme A",
		ulPhonemeA,
		::EAXVOCALMORPHER_MINPHONEMEA,
		::EAXVOCALMORPHER_MAXPHONEMEA
	);
}

void EaxxVocalMorpherEffect::validate_phoneme_a_coarse_tuning(
	long lPhonemeACoarseTuning)
{
	eaxx_validate_range<EaxxVocalMorpherEffectException>(
		"Phoneme A Coarse Tuning",
		lPhonemeACoarseTuning,
		::EAXVOCALMORPHER_MINPHONEMEACOARSETUNING,
		::EAXVOCALMORPHER_MAXPHONEMEACOARSETUNING
	);
}

void EaxxVocalMorpherEffect::validate_phoneme_b(
	unsigned long ulPhonemeB)
{
	eaxx_validate_range<EaxxVocalMorpherEffectException>(
		"Phoneme B",
		ulPhonemeB,
		::EAXVOCALMORPHER_MINPHONEMEB,
		::EAXVOCALMORPHER_MAXPHONEMEB
	);
}

void EaxxVocalMorpherEffect::validate_phoneme_b_coarse_tuning(
	long lPhonemeBCoarseTuning)
{
	eaxx_validate_range<EaxxVocalMorpherEffectException>(
		"Phoneme B Coarse Tuning",
		lPhonemeBCoarseTuning,
		::EAXVOCALMORPHER_MINPHONEMEBCOARSETUNING,
		::EAXVOCALMORPHER_MAXPHONEMEBCOARSETUNING
	);
}

void EaxxVocalMorpherEffect::validate_waveform(
	unsigned long ulWaveform)
{
	eaxx_validate_range<EaxxVocalMorpherEffectException>(
		"Waveform",
		ulWaveform,
		::EAXVOCALMORPHER_MINWAVEFORM,
		::EAXVOCALMORPHER_MAXWAVEFORM
	);
}

void EaxxVocalMorpherEffect::validate_rate(
	float flRate)
{
	eaxx_validate_range<EaxxVocalMorpherEffectException>(
		"Rate",
		flRate,
		::EAXVOCALMORPHER_MINRATE,
		::EAXVOCALMORPHER_MAXRATE
	);
}

void EaxxVocalMorpherEffect::validate_all(
	const ::EAXVOCALMORPHERPROPERTIES& all)
{
	validate_phoneme_a(all.ulPhonemeA);
	validate_phoneme_a_coarse_tuning(all.lPhonemeACoarseTuning);
	validate_phoneme_b(all.ulPhonemeB);
	validate_phoneme_b_coarse_tuning(all.lPhonemeBCoarseTuning);
	validate_waveform(all.ulWaveform);
	validate_rate(all.flRate);
}

void EaxxVocalMorpherEffect::defer_phoneme_a(
	unsigned long ulPhonemeA)
{
	eax_d_.ulPhonemeA = ulPhonemeA;
	eax_dirty_flags_.ulPhonemeA = (eax_.ulPhonemeA != eax_d_.ulPhonemeA);
}

void EaxxVocalMorpherEffect::defer_phoneme_a_coarse_tuning(
	long lPhonemeACoarseTuning)
{
	eax_d_.lPhonemeACoarseTuning = lPhonemeACoarseTuning;
	eax_dirty_flags_.lPhonemeACoarseTuning = (eax_.lPhonemeACoarseTuning != eax_d_.lPhonemeACoarseTuning);
}

void EaxxVocalMorpherEffect::defer_phoneme_b(
	unsigned long ulPhonemeB)
{
	eax_d_.ulPhonemeB = ulPhonemeB;
	eax_dirty_flags_.ulPhonemeB = (eax_.ulPhonemeB != eax_d_.ulPhonemeB);
}

void EaxxVocalMorpherEffect::defer_phoneme_b_coarse_tuning(
	long lPhonemeBCoarseTuning)
{
	eax_d_.lPhonemeBCoarseTuning = lPhonemeBCoarseTuning;
	eax_dirty_flags_.lPhonemeBCoarseTuning = (eax_.lPhonemeBCoarseTuning != eax_d_.lPhonemeBCoarseTuning);
}

void EaxxVocalMorpherEffect::defer_waveform(
	unsigned long ulWaveform)
{
	eax_d_.ulWaveform = ulWaveform;
	eax_dirty_flags_.ulWaveform = (eax_.ulWaveform != eax_d_.ulWaveform);
}

void EaxxVocalMorpherEffect::defer_rate(
	float flRate)
{
	eax_d_.flRate = flRate;
	eax_dirty_flags_.flRate = (eax_.flRate != eax_d_.flRate);
}

void EaxxVocalMorpherEffect::defer_all(
	const ::EAXVOCALMORPHERPROPERTIES& all)
{
	defer_phoneme_a(all.ulPhonemeA);
	defer_phoneme_a_coarse_tuning(all.lPhonemeACoarseTuning);
	defer_phoneme_b(all.ulPhonemeB);
	defer_phoneme_b_coarse_tuning(all.lPhonemeBCoarseTuning);
	defer_waveform(all.ulWaveform);
	defer_rate(all.flRate);
}

void EaxxVocalMorpherEffect::defer_phoneme_a(
	const EaxxEaxCall& eax_call)
{
	const auto& phoneme_a = eax_call.get_value<
		EaxxVocalMorpherEffectException,
		const decltype(::EAXVOCALMORPHERPROPERTIES::ulPhonemeA)
	>();

	validate_phoneme_a(phoneme_a);
	defer_phoneme_a(phoneme_a);
}

void EaxxVocalMorpherEffect::defer_phoneme_a_coarse_tuning(
	const EaxxEaxCall& eax_call)
{
	const auto& phoneme_a_coarse_tuning = eax_call.get_value<
		EaxxVocalMorpherEffectException,
		const decltype(::EAXVOCALMORPHERPROPERTIES::lPhonemeACoarseTuning)
	>();

	validate_phoneme_a_coarse_tuning(phoneme_a_coarse_tuning);
	defer_phoneme_a_coarse_tuning(phoneme_a_coarse_tuning);
}

void EaxxVocalMorpherEffect::defer_phoneme_b(
	const EaxxEaxCall& eax_call)
{
	const auto& phoneme_b = eax_call.get_value<
		EaxxVocalMorpherEffectException,
		const decltype(::EAXVOCALMORPHERPROPERTIES::ulPhonemeB)
	>();

	validate_phoneme_b(phoneme_b);
	defer_phoneme_b(phoneme_b);
}

void EaxxVocalMorpherEffect::defer_phoneme_b_coarse_tuning(
	const EaxxEaxCall& eax_call)
{
	const auto& phoneme_b_coarse_tuning = eax_call.get_value<
		EaxxVocalMorpherEffectException,
		const decltype(::EAXVOCALMORPHERPROPERTIES::lPhonemeBCoarseTuning)
	>();

	validate_phoneme_b_coarse_tuning(phoneme_b_coarse_tuning);
	defer_phoneme_b_coarse_tuning(phoneme_b_coarse_tuning);
}

void EaxxVocalMorpherEffect::defer_waveform(
	const EaxxEaxCall& eax_call)
{
	const auto& waveform = eax_call.get_value<
		EaxxVocalMorpherEffectException,
		const decltype(::EAXVOCALMORPHERPROPERTIES::ulWaveform)
	>();

	validate_waveform(waveform);
	defer_waveform(waveform);
}

void EaxxVocalMorpherEffect::defer_rate(
	const EaxxEaxCall& eax_call)
{
	const auto& rate = eax_call.get_value<
		EaxxVocalMorpherEffectException,
		const decltype(::EAXVOCALMORPHERPROPERTIES::flRate)
	>();

	validate_rate(rate);
	defer_rate(rate);
}

void EaxxVocalMorpherEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all = eax_call.get_value<
		EaxxVocalMorpherEffectException,
		const ::EAXVOCALMORPHERPROPERTIES
	>();

	validate_all(all);
	defer_all(all);
}

void EaxxVocalMorpherEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxVocalMorpherEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.ulPhonemeA)
	{
		set_efx_phoneme_a();
	}

	if (eax_dirty_flags_.lPhonemeACoarseTuning)
	{
		set_efx_phoneme_a_coarse_tuning();
	}

	if (eax_dirty_flags_.ulPhonemeB)
	{
		set_efx_phoneme_b();
	}

	if (eax_dirty_flags_.lPhonemeBCoarseTuning)
	{
		set_efx_phoneme_b_coarse_tuning();
	}

	if (eax_dirty_flags_.ulWaveform)
	{
		set_efx_waveform();
	}

	if (eax_dirty_flags_.flRate)
	{
		set_efx_rate();
	}

	eax_dirty_flags_ = EaxxVocalMorpherEffectEaxDirtyFlags{};

	load();
}

void EaxxVocalMorpherEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXVOCALMORPHER_NONE:
			break;

		case ::EAXVOCALMORPHER_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXVOCALMORPHER_PHONEMEA:
			defer_phoneme_a(eax_call);
			break;

		case ::EAXVOCALMORPHER_PHONEMEACOARSETUNING:
			defer_phoneme_a_coarse_tuning(eax_call);
			break;

		case ::EAXVOCALMORPHER_PHONEMEB:
			defer_phoneme_b(eax_call);
			break;

		case ::EAXVOCALMORPHER_PHONEMEBCOARSETUNING:
			defer_phoneme_b_coarse_tuning(eax_call);
			break;

		case ::EAXVOCALMORPHER_WAVEFORM:
			defer_waveform(eax_call);
			break;

		case ::EAXVOCALMORPHER_RATE:
			defer_rate(eax_call);
			break;

		default:
			throw EaxxVocalMorpherEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
