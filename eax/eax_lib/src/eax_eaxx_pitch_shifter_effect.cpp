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
#include "eax_eaxx_pitch_shifter_effect.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxPitchShifterEffectEaxDirtyFlags& lhs,
	const EaxxPitchShifterEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxPitchShifterEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxPitchShifterEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxPitchShifterEffectException :
	public Exception
{
public:
	explicit EaxxPitchShifterEffectException(
		const char* message)
		:
		Exception{"EAXX_PITCH_SHIFTER_EFFECT", message}
	{
	}
}; // EaxxPitchShifterEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxPitchShifterEffect::EaxxPitchShifterEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_PITCH_SHIFTER)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxPitchShifterEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxPitchShifterEffect::dispatch(
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

void EaxxPitchShifterEffect::set_eax_defaults()
{
	eax_.lCoarseTune = ::EAXPITCHSHIFTER_DEFAULTCOARSETUNE;
	eax_.lFineTune = ::EAXPITCHSHIFTER_DEFAULTFINETUNE;

	eax_d_ = eax_;
}

void EaxxPitchShifterEffect::set_efx_coarse_tune()
{
	const auto coarse_tune = clamp(
		eax_.lCoarseTune,
		::EAXPITCHSHIFTER_MINCOARSETUNE,
		::EAXPITCHSHIFTER_MAXCOARSETUNE
	);

	::alEffecti(efx_effect_object_.get(), AL_PITCH_SHIFTER_COARSE_TUNE, coarse_tune);
}

void EaxxPitchShifterEffect::set_efx_fine_tune()
{
	const auto fine_tune = clamp(
		eax_.lFineTune,
		::EAXPITCHSHIFTER_MINFINETUNE,
		::EAXPITCHSHIFTER_MAXFINETUNE
	);

	::alEffecti(efx_effect_object_.get(), AL_PITCH_SHIFTER_FINE_TUNE, fine_tune);
}

void EaxxPitchShifterEffect::set_efx_defaults()
{
	set_efx_coarse_tune();
	set_efx_fine_tune();
}

void EaxxPitchShifterEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXPITCHSHIFTER_NONE:
			break;

		case ::EAXPITCHSHIFTER_ALLPARAMETERS:
			eax_call.set_value<EaxxPitchShifterEffectException>(eax_);
			break;

		case ::EAXPITCHSHIFTER_COARSETUNE:
			eax_call.set_value<EaxxPitchShifterEffectException>(eax_.lCoarseTune);
			break;

		case ::EAXPITCHSHIFTER_FINETUNE:
			eax_call.set_value<EaxxPitchShifterEffectException>(eax_.lFineTune);
			break;

		default:
			throw EaxxPitchShifterEffectException{"Unsupported property id."};
	}
}

void EaxxPitchShifterEffect::validate_coarse_tune(
	long lCoarseTune)
{
	eaxx_validate_range<EaxxPitchShifterEffectException>(
		"Coarse Tune",
		lCoarseTune,
		::EAXPITCHSHIFTER_MINCOARSETUNE,
		::EAXPITCHSHIFTER_MAXCOARSETUNE
	);
}

void EaxxPitchShifterEffect::validate_fine_tune(
	long lFineTune)
{
	eaxx_validate_range<EaxxPitchShifterEffectException>(
		"Fine Tune",
		lFineTune,
		::EAXPITCHSHIFTER_MINFINETUNE,
		::EAXPITCHSHIFTER_MAXFINETUNE
	);
}

void EaxxPitchShifterEffect::validate_all(
	const ::EAXPITCHSHIFTERPROPERTIES& all)
{
	validate_coarse_tune(all.lCoarseTune);
	validate_fine_tune(all.lFineTune);
}

void EaxxPitchShifterEffect::defer_coarse_tune(
	long lCoarseTune)
{
	eax_d_.lCoarseTune = lCoarseTune;
	eax_dirty_flags_.lCoarseTune = (eax_.lCoarseTune != eax_d_.lCoarseTune);
}

void EaxxPitchShifterEffect::defer_fine_tune(
	long lFineTune)
{
	eax_d_.lFineTune = lFineTune;
	eax_dirty_flags_.lFineTune = (eax_.lFineTune != eax_d_.lFineTune);
}

void EaxxPitchShifterEffect::defer_all(
	const ::EAXPITCHSHIFTERPROPERTIES& all)
{
	defer_coarse_tune(all.lCoarseTune);
	defer_fine_tune(all.lFineTune);
}

void EaxxPitchShifterEffect::defer_coarse_tune(
	const EaxxEaxCall& eax_call)
{
	const auto& coarse_tune =
		eax_call.get_value<EaxxPitchShifterEffectException, const decltype(::EAXPITCHSHIFTERPROPERTIES::lCoarseTune)>();

	validate_coarse_tune(coarse_tune);
	defer_coarse_tune(coarse_tune);
}

void EaxxPitchShifterEffect::defer_fine_tune(
	const EaxxEaxCall& eax_call)
{
	const auto& fine_tune =
		eax_call.get_value<EaxxPitchShifterEffectException, const decltype(::EAXPITCHSHIFTERPROPERTIES::lFineTune)>();

	validate_fine_tune(fine_tune);
	defer_fine_tune(fine_tune);
}

void EaxxPitchShifterEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<EaxxPitchShifterEffectException, const ::EAXPITCHSHIFTERPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxPitchShifterEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxPitchShifterEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.lCoarseTune)
	{
		set_efx_coarse_tune();
	}

	if (eax_dirty_flags_.lFineTune)
	{
		set_efx_fine_tune();
	}

	eax_dirty_flags_ = EaxxPitchShifterEffectEaxDirtyFlags{};

	load();
}

void EaxxPitchShifterEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXPITCHSHIFTER_NONE:
			break;

		case ::EAXPITCHSHIFTER_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXPITCHSHIFTER_COARSETUNE:
			defer_coarse_tune(eax_call);
			break;

		case ::EAXPITCHSHIFTER_FINETUNE:
			defer_fine_tune(eax_call);
			break;

		default:
			throw EaxxPitchShifterEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
