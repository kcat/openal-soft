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


#ifndef EAX_EAXX_EQUALIZER_EFFECT_INCLUDED
#define EAX_EAXX_EQUALIZER_EFFECT_INCLUDED


#include <cstdint>

#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_effect.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using EaxxEqualizerEffectEaxDirtyFlagsValue = unsigned int;

struct EaxxEqualizerEffectEaxDirtyFlags
{
	EaxxEqualizerEffectEaxDirtyFlagsValue lLowGain : 1;
	EaxxEqualizerEffectEaxDirtyFlagsValue flLowCutOff : 1;
	EaxxEqualizerEffectEaxDirtyFlagsValue lMid1Gain : 1;
	EaxxEqualizerEffectEaxDirtyFlagsValue flMid1Center : 1;
	EaxxEqualizerEffectEaxDirtyFlagsValue flMid1Width : 1;
	EaxxEqualizerEffectEaxDirtyFlagsValue lMid2Gain : 1;
	EaxxEqualizerEffectEaxDirtyFlagsValue flMid2Center : 1;
	EaxxEqualizerEffectEaxDirtyFlagsValue flMid2Width : 1;
	EaxxEqualizerEffectEaxDirtyFlagsValue lHighGain : 1;
	EaxxEqualizerEffectEaxDirtyFlagsValue flHighCutOff : 1;
}; // EaxxEqualizerEffectEaxDirtyFlags

static_assert(sizeof(EaxxEqualizerEffectEaxDirtyFlags) == sizeof(EaxxEqualizerEffectEaxDirtyFlagsValue), "Type size.");

bool operator==(
	const EaxxEqualizerEffectEaxDirtyFlags& lhs,
	const EaxxEqualizerEffectEaxDirtyFlags& rhs) noexcept;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEqualizerEffect final :
	public EaxxEffect
{
public:
	EaxxEqualizerEffect(
		::ALuint al_effect_slot);


	void load() override;

	void dispatch(
		const EaxxEaxCall& eax_call) override;


private:
	const ::ALuint al_effect_slot_;
	EfxEffectObject efx_effect_object_;

	::EAXEQUALIZERPROPERTIES eax_;
	::EAXEQUALIZERPROPERTIES eax_d_;
	EaxxEqualizerEffectEaxDirtyFlags eax_dirty_flags_;


	void set_eax_defaults();


	void set_efx_low_gain();

	void set_efx_low_cutoff();

	void set_efx_mid1_gain();

	void set_efx_mid1_center();

	void set_efx_mid1_width();

	void set_efx_mid2_gain();

	void set_efx_mid2_center();

	void set_efx_mid2_width();

	void set_efx_high_gain();

	void set_efx_high_cutoff();

	void set_efx_defaults();


	void get(
		const EaxxEaxCall& eax_call);


	void validate_low_gain(
		long lLowGain);

	void validate_low_cutoff(
		float flLowCutOff);

	void validate_mid1_gain(
		long lMid1Gain);

	void validate_mid1_center(
		float flMid1Center);

	void validate_mid1_width(
		float flMid1Width);

	void validate_mid2_gain(
		long lMid2Gain);

	void validate_mid2_center(
		float flMid2Center);

	void validate_mid2_width(
		float flMid2Width);

	void validate_high_gain(
		long lHighGain);

	void validate_high_cutoff(
		float flHighCutOff);

	void validate_all(
		const ::EAXEQUALIZERPROPERTIES& all);


	void defer_low_gain(
		long lLowGain);

	void defer_low_cutoff(
		float flLowCutOff);

	void defer_mid1_gain(
		long lMid1Gain);

	void defer_mid1_center(
		float flMid1Center);

	void defer_mid1_width(
		float flMid1Width);

	void defer_mid2_gain(
		long lMid2Gain);

	void defer_mid2_center(
		float flMid2Center);

	void defer_mid2_width(
		float flMid2Width);

	void defer_high_gain(
		long lHighGain);

	void defer_high_cutoff(
		float flHighCutOff);

	void defer_all(
		const ::EAXEQUALIZERPROPERTIES& all);


	void defer_low_gain(
		const EaxxEaxCall& eax_call);

	void defer_low_cutoff(
		const EaxxEaxCall& eax_call);

	void defer_mid1_gain(
		const EaxxEaxCall& eax_call);

	void defer_mid1_center(
		const EaxxEaxCall& eax_call);

	void defer_mid1_width(
		const EaxxEaxCall& eax_call);

	void defer_mid2_gain(
		const EaxxEaxCall& eax_call);

	void defer_mid2_center(
		const EaxxEaxCall& eax_call);

	void defer_mid2_width(
		const EaxxEaxCall& eax_call);

	void defer_high_gain(
		const EaxxEaxCall& eax_call);

	void defer_high_cutoff(
		const EaxxEaxCall& eax_call);

	void defer_all(
		const EaxxEaxCall& eax_call);

	void apply_deferred();


	void set(
		const EaxxEaxCall& eax_call);
}; // EaxxEqualizerEffect

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_EQUALIZER_EFFECT_INCLUDED
