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


#ifndef EAX_EAXX_ECHO_EFFECT_INCLUDED
#define EAX_EAXX_ECHO_EFFECT_INCLUDED


#include <cstdint>

#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_effect.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using EaxxEchoEffectEaxDirtyFlagsValue = unsigned int;

struct EaxxEchoEffectEaxDirtyFlags
{
	EaxxEchoEffectEaxDirtyFlagsValue flDelay : 1;
	EaxxEchoEffectEaxDirtyFlagsValue flLRDelay : 1;
	EaxxEchoEffectEaxDirtyFlagsValue flDamping : 1;
	EaxxEchoEffectEaxDirtyFlagsValue flFeedback : 1;
	EaxxEchoEffectEaxDirtyFlagsValue flSpread : 1;
}; // EaxxEchoEffectEaxDirtyFlags

static_assert(sizeof(EaxxEchoEffectEaxDirtyFlags) == sizeof(EaxxEchoEffectEaxDirtyFlagsValue), "Type size.");

bool operator==(
	const EaxxEchoEffectEaxDirtyFlags& lhs,
	const EaxxEchoEffectEaxDirtyFlags& rhs) noexcept;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEchoEffect final :
	public EaxxEffect
{
public:
	EaxxEchoEffect(
		::ALuint al_effect_slot);


	void load() override;

	void dispatch(
		const EaxxEaxCall& eax_call) override;


private:
	const ::ALuint al_effect_slot_;
	EfxEffectObject efx_effect_object_;

	::EAXECHOPROPERTIES eax_;
	::EAXECHOPROPERTIES eax_d_;
	EaxxEchoEffectEaxDirtyFlags eax_dirty_flags_{};


	void set_eax_defaults();


	void set_efx_delay();

	void set_efx_lr_delay();

	void set_efx_damping();

	void set_efx_feedback();

	void set_efx_spread();

	void set_efx_defaults();


	void get(
		const EaxxEaxCall& eax_call);


	void validate_delay(
		float flDelay);

	void validate_lr_delay(
		float flLRDelay);

	void validate_damping(
		float flDamping);

	void validate_feedback(
		float flFeedback);

	void validate_spread(
		float flSpread);

	void validate_all(
		const ::EAXECHOPROPERTIES& all);


	void defer_delay(
		float flDelay);

	void defer_lr_delay(
		float flLRDelay);

	void defer_damping(
		float flDamping);

	void defer_feedback(
		float flFeedback);

	void defer_spread(
		float flSpread);

	void defer_all(
		const ::EAXECHOPROPERTIES& all);


	void defer_delay(
		const EaxxEaxCall& eax_call);

	void defer_lr_delay(
		const EaxxEaxCall& eax_call);

	void defer_damping(
		const EaxxEaxCall& eax_call);

	void defer_feedback(
		const EaxxEaxCall& eax_call);

	void defer_spread(
		const EaxxEaxCall& eax_call);

	void defer_all(
		const EaxxEaxCall& eax_call);


	void apply_deferred();

	void set(
		const EaxxEaxCall& eax_call);
}; // EaxxEchoEffect

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_ECHO_EFFECT_INCLUDED
