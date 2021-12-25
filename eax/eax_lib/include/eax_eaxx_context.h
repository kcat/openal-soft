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


#ifndef EAX_EAXX_CONTEXT_INCLUDED
#define EAX_EAXX_CONTEXT_INCLUDED


#include <unordered_map>

#include "AL/al.h"
#include "AL/alc.h"

#include "eax_al_api.h"
#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_context_shared.h"
#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_fx_slots.h"
#include "eax_eaxx_source.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using EaxxContextContextDirtyFlagsValue = unsigned int;

struct EaxxContextContextDirtyFlags
{
	EaxxContextContextDirtyFlagsValue guidPrimaryFXSlotID : 1;
	EaxxContextContextDirtyFlagsValue flDistanceFactor : 1;
	EaxxContextContextDirtyFlagsValue flAirAbsorptionHF : 1;
	EaxxContextContextDirtyFlagsValue flHFReference : 1;
	EaxxContextContextDirtyFlagsValue flMacroFXFactor : 1;
}; // EaxxContextContextDirtyFlags

static_assert(sizeof(EaxxContextContextDirtyFlags) == sizeof(EaxxContextContextDirtyFlagsValue), "Type size.");

bool operator==(
	const EaxxContextContextDirtyFlags& lhs,
	const EaxxContextContextDirtyFlags& rhs) noexcept;

bool operator!=(
	const EaxxContextContextDirtyFlags& lhs,
	const EaxxContextContextDirtyFlags& rhs) noexcept;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxContext
{
public:
	EaxxContext(
		::ALCdevice* alc_device,
		::ALCcontext* alc_context);


	void activate_default_reverb_effect();


	EaxxFxSlot& get_slot(
		EaxxFxSlotIndex fx_slot_index);


	void initialize_sources(
		::ALsizei count,
		::ALuint* al_names);

	void uninitialize_sources(
		::ALsizei count,
		const ::ALuint* al_names);


	void dispatch(
		const EaxxEaxCall& eax_call);


	EaxxSource* find_source(
		::ALuint al_source);

	void update_filters();


	void set_last_error() noexcept;


private:
	struct Al
	{
		::ALCdevice* device{};
		::ALCcontext* context{};
		EfxFilterObject filter{};
	}; // Al

	struct Eax
	{
		::EAX50CONTEXTPROPERTIES context{};
	}; // Eax

	using SourceMap = std::unordered_map<::ALuint, EaxxSource>;


	long eax_last_error_{};
	unsigned long eax_speaker_config_{};
	EaxxContextShared shared_{};
	SourceMap source_map_{};
	Al al_{};
	Eax eax_{};
	Eax eax_d_{};
	::EAXSESSIONPROPERTIES eax_session_{};

	EaxxContextSharedDirtyFlags context_shared_dirty_flags_{};
	EaxxContextContextDirtyFlags context_dirty_flags_{};


	[[noreturn]]
	static void fail(
		const char* message);


	void ensure_compatibility();


	bool has_softx_filter_gain_ex_extension();

	void initialize_extended_filter_gain();


	void set_eax_last_error_defaults() noexcept;

	void set_eax_speaker_config_defaults() noexcept;

	void set_eax_session_defaults() noexcept;

	void set_eax_context_defaults() noexcept;

	void set_eax_defaults() noexcept;

	void initialize_filter();


	void get_primary_fx_slot_id(
		const EaxxEaxCall& eax_call);

	void get_distance_factor(
		const EaxxEaxCall& eax_call);

	void get_air_absorption_hf(
		const EaxxEaxCall& eax_call);

	void get_hf_reference(
		const EaxxEaxCall& eax_call);

	void get_last_error(
		const EaxxEaxCall& eax_call);

	void get_speaker_config(
		const EaxxEaxCall& eax_call);

	void get_eax_session(
		const EaxxEaxCall& eax_call);

	void get_macro_fx_factor(
		const EaxxEaxCall& eax_call);

	void get_context_all(
		const EaxxEaxCall& eax_call);

	void get(
		const EaxxEaxCall& eax_call);


	void set_primary_fx_slot_id();

	void set_distance_factor();

	void set_air_absorbtion_hf();

	void set_hf_reference();

	void set_macro_fx_factor();

	void set_context();

	void initialize_fx_slots();


	void update_sources();


	void validate_primary_fx_slot_id(
		const ::GUID& primary_fx_slot_id);

	void validate_distance_factor(
		float distance_factor);

	void validate_air_absorption_hf(
		float air_absorption_hf);

	void validate_hf_reference(
		float hf_reference);

	void validate_speaker_config(
		unsigned long speaker_config);

	void validate_eax_session_eax_version(
		unsigned long eax_version);

	void validate_eax_session_max_active_sends(
		unsigned long max_active_sends);

	void validate_eax_session(
		const ::EAXSESSIONPROPERTIES& eax_session);

	void validate_macro_fx_factor(
		float macro_fx_factor);

	void validate_context_all(
		const ::EAX40CONTEXTPROPERTIES& context_all);

	void validate_context_all(
		const EAX50CONTEXTPROPERTIES& context_all);


	void defer_primary_fx_slot_id(
		const ::GUID& primary_fx_slot_id);

	void defer_distance_factor(
		float distance_factor);

	void defer_air_absorption_hf(
		float air_absorption_hf);

	void defer_hf_reference(
		float hf_reference);

	void defer_macro_fx_factor(
		float macro_fx_factor);

	void defer_context_all(
		const ::EAX40CONTEXTPROPERTIES& context_all);

	void defer_context_all(
		const EAX50CONTEXTPROPERTIES& context_all);


	void defer_context_all(
		const EaxxEaxCall& eax_call);

	void defer_primary_fx_slot_id(
		const EaxxEaxCall& eax_call);

	void defer_distance_factor(
		const EaxxEaxCall& eax_call);

	void defer_air_absorption_hf(
		const EaxxEaxCall& eax_call);

	void defer_hf_reference(
		const EaxxEaxCall& eax_call);

	void set_speaker_config(
		const EaxxEaxCall& eax_call);

	void set_eax_session(
		const EaxxEaxCall& eax_call);

	void defer_macro_fx_factor(
		const EaxxEaxCall& eax_call);

	void set(
		const EaxxEaxCall& eax_call);

	void apply_deferred();
}; // EaxxContext

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_CONTEXT_INCLUDED
