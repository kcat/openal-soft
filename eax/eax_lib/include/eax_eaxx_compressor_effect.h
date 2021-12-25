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


#ifndef EAX_EAXX_COMPRESSOR_EFFECT_INCLUDED
#define EAX_EAXX_COMPRESSOR_EFFECT_INCLUDED


#include <cstdint>

#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_effect.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using EaxxCompressorEffectEaxDirtyFlagsValue = unsigned int;

struct EaxxCompressorEffectEaxDirtyFlags
{
	EaxxCompressorEffectEaxDirtyFlagsValue ulOnOff : 1;
}; // EaxxCompressorEffectEaxDirtyFlags

static_assert(sizeof(EaxxCompressorEffectEaxDirtyFlags) == sizeof(EaxxCompressorEffectEaxDirtyFlags), "Type size.");

bool operator==(
	const EaxxCompressorEffectEaxDirtyFlags& lhs,
	const EaxxCompressorEffectEaxDirtyFlags& rhs) noexcept;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxCompressorEffect final :
	public EaxxEffect
{
public:
	EaxxCompressorEffect(
		::ALuint al_effect_slot);


	void load() override;

	void dispatch(
		const EaxxEaxCall& eax_call) override;


private:
	const ::ALuint al_effect_slot_;
	EfxEffectObject efx_effect_object_;

	::EAXAGCCOMPRESSORPROPERTIES eax_;
	::EAXAGCCOMPRESSORPROPERTIES eax_d_;
	EaxxCompressorEffectEaxDirtyFlags eax_dirty_flags_{};


	void set_eax_defaults();


	void set_efx_on_off();

	void set_efx_defaults();


	void get(
		const EaxxEaxCall& eax_call);


	void validate_on_off(
		unsigned long ulOnOff);

	void validate_all(
		const ::EAXAGCCOMPRESSORPROPERTIES& eax_all);


	void defer_on_off(
		unsigned long ulOnOff);

	void defer_all(
		const ::EAXAGCCOMPRESSORPROPERTIES& eax_all);


	void defer_on_off(
		const EaxxEaxCall& eax_call);

	void defer_all(
		const EaxxEaxCall& eax_call);


	void apply_deferred();

	void set(
		const EaxxEaxCall& eax_call);
}; // EaxxCompressorEffect

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_COMPRESSOR_EFFECT_INCLUDED
