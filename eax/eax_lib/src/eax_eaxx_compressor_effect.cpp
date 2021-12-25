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

#include "eax_eaxx_compressor_effect.h"
#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxCompressorEffectEaxDirtyFlags& lhs,
	const EaxxCompressorEffectEaxDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxCompressorEffectEaxDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxCompressorEffectEaxDirtyFlagsValue&>(rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxCompressorEffectException :
	public Exception
{
public:
	explicit EaxxCompressorEffectException(
		const char* message)
		:
		Exception{"EAXX_COMPRESSOR_EFFECT", message}
	{
	}
}; // EaxxCompressorEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxCompressorEffect::EaxxCompressorEffect(
	::ALuint al_effect_slot)
	:
	al_effect_slot_{al_effect_slot},
	efx_effect_object_{make_efx_effect_object(AL_EFFECT_COMPRESSOR)}
{
	set_eax_defaults();
	set_efx_defaults();
}

void EaxxCompressorEffect::load()
{
	::alAuxiliaryEffectSloti(
		al_effect_slot_,
		AL_EFFECTSLOT_EFFECT,
		static_cast<::ALint>(efx_effect_object_.get())
	);
}

void EaxxCompressorEffect::dispatch(
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

void EaxxCompressorEffect::set_eax_defaults()
{
	eax_.ulOnOff = ::EAXAGCCOMPRESSOR_DEFAULTONOFF;

	eax_d_ = eax_;
}

void EaxxCompressorEffect::set_efx_on_off()
{
	const auto on_off = clamp(
		static_cast<::ALint>(eax_.ulOnOff),
		AL_COMPRESSOR_MIN_ONOFF,
		AL_COMPRESSOR_MAX_ONOFF
	);

	::alEffecti(efx_effect_object_.get(), AL_COMPRESSOR_ONOFF, on_off);
}

void EaxxCompressorEffect::set_efx_defaults()
{
	set_efx_on_off();
}

void EaxxCompressorEffect::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXAGCCOMPRESSOR_NONE:
			break;

		case ::EAXAGCCOMPRESSOR_ALLPARAMETERS:
			eax_call.set_value<EaxxCompressorEffectException>(eax_);
			break;

		case ::EAXAGCCOMPRESSOR_ONOFF:
			eax_call.set_value<EaxxCompressorEffectException>(eax_.ulOnOff);
			break;

		default:
			throw EaxxCompressorEffectException{"Unsupported property id."};
	}
}

void EaxxCompressorEffect::validate_on_off(
	unsigned long ulOnOff)
{
	eaxx_validate_range<EaxxCompressorEffectException>(
		"On-Off",
		ulOnOff,
		::EAXAGCCOMPRESSOR_MINONOFF,
		::EAXAGCCOMPRESSOR_MAXONOFF
	);
}

void EaxxCompressorEffect::validate_all(
	const ::EAXAGCCOMPRESSORPROPERTIES& eax_all)
{
	validate_on_off(eax_all.ulOnOff);
}

void EaxxCompressorEffect::defer_on_off(
	unsigned long ulOnOff)
{
	eax_d_.ulOnOff = ulOnOff;
	eax_dirty_flags_.ulOnOff = (eax_.ulOnOff != eax_d_.ulOnOff);
}

void EaxxCompressorEffect::defer_all(
	const ::EAXAGCCOMPRESSORPROPERTIES& eax_all)
{
	defer_on_off(eax_all.ulOnOff);
}

void EaxxCompressorEffect::defer_on_off(
	const EaxxEaxCall& eax_call)
{
	const auto& on_off =
		eax_call.get_value<EaxxCompressorEffectException, const decltype(::EAXAGCCOMPRESSORPROPERTIES::ulOnOff)>();

	validate_on_off(on_off);
	defer_on_off(on_off);
}

void EaxxCompressorEffect::defer_all(
	const EaxxEaxCall& eax_call)
{
	const auto& all =
		eax_call.get_value<EaxxCompressorEffectException, const ::EAXAGCCOMPRESSORPROPERTIES>();

	validate_all(all);
	defer_all(all);
}

void EaxxCompressorEffect::apply_deferred()
{
	if (eax_dirty_flags_ == EaxxCompressorEffectEaxDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (eax_dirty_flags_.ulOnOff)
	{
		set_efx_on_off();
	}

	eax_dirty_flags_ = EaxxCompressorEffectEaxDirtyFlags{};

	load();
}

void EaxxCompressorEffect::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXAGCCOMPRESSOR_NONE:
			break;

		case ::EAXAGCCOMPRESSOR_ALLPARAMETERS:
			defer_all(eax_call);
			break;

		case ::EAXAGCCOMPRESSOR_ONOFF:
			defer_on_off(eax_call);
			break;

		default:
			throw EaxxCompressorEffectException{"Unsupported property id."};
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
