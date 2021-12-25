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


#ifndef EAX_EAXX_DISTORTION_EFFECT_INCLUDED
#define EAX_EAXX_DISTORTION_EFFECT_INCLUDED


#include <cstdint>

#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_effect.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using EaxxDistortionEffectEaxDirtyFlagsValue = unsigned int;

struct EaxxDistortionEffectEaxDirtyFlags
{
	EaxxDistortionEffectEaxDirtyFlagsValue flEdge : 1;
	EaxxDistortionEffectEaxDirtyFlagsValue lGain : 1;
	EaxxDistortionEffectEaxDirtyFlagsValue flLowPassCutOff : 1;
	EaxxDistortionEffectEaxDirtyFlagsValue flEQCenter : 1;
	EaxxDistortionEffectEaxDirtyFlagsValue flEQBandwidth : 1;
}; // EaxxDistortionEffectEaxDirtyFlags

static_assert(sizeof(EaxxDistortionEffectEaxDirtyFlags) == sizeof(EaxxDistortionEffectEaxDirtyFlagsValue), "Type size.");

bool operator==(
	const EaxxDistortionEffectEaxDirtyFlags& lhs,
	const EaxxDistortionEffectEaxDirtyFlags& rhs) noexcept;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxDistortionEffect final :
	public EaxxEffect
{
public:
	EaxxDistortionEffect(
		::ALuint al_effect_slot);


	void load() override;

	void dispatch(
		const EaxxEaxCall& eax_call) override;


private:
	const ::ALuint al_effect_slot_;
	EfxEffectObject efx_effect_object_;

	::EAXDISTORTIONPROPERTIES eax_;
	::EAXDISTORTIONPROPERTIES eax_d_;
	EaxxDistortionEffectEaxDirtyFlags eax_dirty_flags_{};


	void set_eax_defaults();


	void set_efx_edge();

	void set_efx_gain();

	void set_efx_low_pass_cutoff();

	void set_efx_eq_center();

	void set_efx_eq_bandwidth();

	void set_efx_defaults();


	void get(
		const EaxxEaxCall& eax_call);


	void validate_edge(
		float flEdge);

	void validate_gain(
		long lGain);

	void validate_low_pass_cutoff(
		float flLowPassCutOff);

	void validate_eq_center(
		float flEQCenter);

	void validate_eq_bandwidth(
		float flEQBandwidth);

	void validate_all(
		const ::EAXDISTORTIONPROPERTIES& eax_all);


	void defer_edge(
		float flEdge);

	void defer_gain(
		long lGain);

	void defer_low_pass_cutoff(
		float flLowPassCutOff);

	void defer_eq_center(
		float flEQCenter);

	void defer_eq_bandwidth(
		float flEQBandwidth);

	void defer_all(
		const ::EAXDISTORTIONPROPERTIES& eax_all);


	void defer_edge(
		const EaxxEaxCall& eax_call);

	void defer_gain(
		const EaxxEaxCall& eax_call);

	void defer_low_pass_cutoff(
		const EaxxEaxCall& eax_call);

	void defer_eq_center(
		const EaxxEaxCall& eax_call);

	void defer_eq_bandwidth(
		const EaxxEaxCall& eax_call);

	void defer_all(
		const EaxxEaxCall& eax_call);


	void apply_deferred();

	void set(
		const EaxxEaxCall& eax_call);
}; // EaxxDistortionEffect

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_DISTORTION_EFFECT_INCLUDED
