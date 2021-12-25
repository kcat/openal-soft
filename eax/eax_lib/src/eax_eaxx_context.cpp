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


#include "eax_eaxx_context.h"

#include "AL/efx.h"

#include "eax_exception.h"

#include "eax_al_api.h"
#include "eax_al_object.h"

#include "eax_eaxx_source.h"
#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxContextContextDirtyFlags& lhs,
	const EaxxContextContextDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxContextContextDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxContextContextDirtyFlagsValue&>(rhs);
}

bool operator!=(
	const EaxxContextContextDirtyFlags& lhs,
	const EaxxContextContextDirtyFlags& rhs) noexcept
{
	return !(lhs == rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxContextException :
	public Exception
{
public:
	explicit EaxxContextException(
		const char* message)
		:
		Exception{"EAXX_CONTEXT", message}
	{
	}
}; // EaxxContextException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxContext::EaxxContext(
	::ALCdevice* alc_device,
	::ALCcontext* alc_context)
{
	al_.device = alc_device;
	al_.context = alc_context;

	ensure_compatibility();
	initialize_extended_filter_gain();
	initialize_filter();
	set_eax_defaults();
	set_air_absorbtion_hf();
	initialize_fx_slots();
}

void EaxxContext::activate_default_reverb_effect()
{
	shared_.fx_slots.activate_default_reverb_effect();
}

EaxxFxSlot& EaxxContext::get_slot(
	EaxxFxSlotIndex fx_slot_index)
{
	return shared_.fx_slots.get(fx_slot_index);
}

void EaxxContext::initialize_sources(
	::ALsizei count,
	::ALuint* al_names)
{
	if (count <= 0 || !al_names || al_names[0] == AL_NONE)
	{
		return;
	}

	auto param = EaxxSourceInitParam{};
	param.al_filter = al_.filter.get();
	param.context_shared = &shared_;

	for (auto i = decltype(count){}; i < count; ++i)
	{
		param.al_source = al_names[i];
		source_map_.emplace(param.al_source, EaxxSource{param});
	}
}

void EaxxContext::uninitialize_sources(
	::ALsizei count,
	const ::ALuint* al_names)
{
	if (count <= 0 || !al_names || al_names[0] == AL_NONE)
	{
		return;
	}

	for (auto i = decltype(count){}; i < count; ++i)
	{
		const auto al_name = al_names[i];

		source_map_.erase(al_name);
	}
}

void EaxxContext::dispatch(
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

EaxxSource* EaxxContext::find_source(
	::ALuint al_source_name)
{
	const auto map_it = source_map_.find(al_source_name);

	if (map_it != source_map_.cend())
	{
		return &map_it->second;
	}

	return nullptr;
}

void EaxxContext::update_filters()
{
	for (auto& source_item : source_map_)
	{
		source_item.second.update_filters();
	}
}

void EaxxContext::set_last_error() noexcept
{
	eax_last_error_ = ::EAXERR_INVALID_OPERATION;
}

[[noreturn]]
void EaxxContext::fail(
	const char* message)
{
	throw EaxxContextException{message};
}

void EaxxContext::ensure_compatibility()
{
	const auto has_efx_extension = ::alcIsExtensionPresent(al_.device, ALC_EXT_EFX_NAME);

	if (!has_efx_extension)
	{
		fail("EFX extension not found.");
	}

	auto aux_send_count = ::ALint{};

	::alcGetIntegerv(al_.device, ALC_MAX_AUXILIARY_SENDS, 1, &aux_send_count);

	if (aux_send_count < ::EAX_MAX_FXSLOTS)
	{
		const auto message =
			std::string{} +
			"Expected at least " +
			std::to_string(::EAX_MAX_FXSLOTS) +
			" EFX auxiliary effect slots.";

		fail(message.c_str());
	}

	const auto low_pass_efx_object = make_efx_filter_object();
	const auto low_pass_al_name = low_pass_efx_object.get();
	auto efx_filter_type = ::ALint{};
	::alFilteri(low_pass_al_name, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
	::alGetFilteri(low_pass_al_name, AL_FILTER_TYPE, &efx_filter_type);

	if (efx_filter_type != AL_FILTER_LOWPASS)
	{
		fail("EFX low-pass filter not supported.");
	}

	try
	{
		make_efx_effect_object(AL_EFFECT_EAXREVERB);
	}
	catch (...)
	{
		fail("EFX EAX-reverb not supported.");
	}
}

bool EaxxContext::has_softx_filter_gain_ex_extension()
{
	return true;
}

void EaxxContext::initialize_extended_filter_gain()
{
	if (has_softx_filter_gain_ex_extension())
	{
		shared_.max_filter_gain = 4.0F;
	}
	else
	{
		shared_.max_filter_gain = 1.0F;
	}
}

void EaxxContext::set_eax_last_error_defaults() noexcept
{
	eax_last_error_ = ::EAX_OK;
}

void EaxxContext::set_eax_speaker_config_defaults() noexcept
{
	eax_speaker_config_ = ::HEADPHONES;
}

void EaxxContext::set_eax_session_defaults() noexcept
{
	eax_session_.ulEAXVersion = ::EAXCONTEXT_MINEAXSESSION;
	eax_session_.ulMaxActiveSends = ::EAXCONTEXT_DEFAULTMAXACTIVESENDS;
}

void EaxxContext::set_eax_context_defaults() noexcept
{
	eax_.context.guidPrimaryFXSlotID = ::EAXCONTEXT_DEFAULTPRIMARYFXSLOTID;
	eax_.context.flDistanceFactor = ::EAXCONTEXT_DEFAULTDISTANCEFACTOR;
	eax_.context.flAirAbsorptionHF = ::EAXCONTEXT_DEFAULTAIRABSORPTIONHF;
	eax_.context.flHFReference = ::EAXCONTEXT_DEFAULTHFREFERENCE;
}

void EaxxContext::set_eax_defaults() noexcept
{
	set_eax_last_error_defaults();
	set_eax_speaker_config_defaults();
	set_eax_session_defaults();
	set_eax_context_defaults();

	eax_d_ = eax_;
}

void EaxxContext::initialize_filter()
{
	al_.filter = make_efx_filter_object();
	const auto al_filter = al_.filter.get();

	::alFilteri(al_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
	auto al_filter_type = ::ALint{};
	::alGetFilteri(al_filter, AL_FILTER_TYPE, &al_filter_type);

	if (al_filter_type != AL_FILTER_LOWPASS)
	{
		fail("Failed to set EFX filter type to low-pass.");
	}
}

void EaxxContext::get_primary_fx_slot_id(
	const EaxxEaxCall& eax_call)
{
	eax_call.set_value<EaxxContextException>(eax_.context.guidPrimaryFXSlotID);
}

void EaxxContext::get_distance_factor(
	const EaxxEaxCall& eax_call)
{
	eax_call.set_value<EaxxContextException>(eax_.context.flDistanceFactor);
}

void EaxxContext::get_air_absorption_hf(
	const EaxxEaxCall& eax_call)
{
	eax_call.set_value<EaxxContextException>(eax_.context.flAirAbsorptionHF);
}

void EaxxContext::get_hf_reference(
	const EaxxEaxCall& eax_call)
{
	eax_call.set_value<EaxxContextException>(eax_.context.flHFReference);
}

void EaxxContext::get_last_error(
	const EaxxEaxCall& eax_call)
{
	const auto eax_last_error = eax_last_error_;
	eax_last_error_ = ::EAX_OK;
	eax_call.set_value<EaxxContextException>(eax_last_error);
}

void EaxxContext::get_speaker_config(
	const EaxxEaxCall& eax_call)
{
	eax_call.set_value<EaxxContextException>(eax_speaker_config_);
}

void EaxxContext::get_eax_session(
	const EaxxEaxCall& eax_call)
{
	eax_call.set_value<EaxxContextException>(eax_session_);
}

void EaxxContext::get_macro_fx_factor(
	const EaxxEaxCall& eax_call)
{
	eax_call.set_value<EaxxContextException>(eax_.context.flMacroFXFactor);
}

void EaxxContext::get_context_all(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_version())
	{
		case 4:
			eax_call.set_value<EaxxContextException>(static_cast<const ::EAX40CONTEXTPROPERTIES&>(eax_.context));
			break;

		case 5:
			eax_call.set_value<EaxxContextException>(static_cast<const ::EAX50CONTEXTPROPERTIES&>(eax_.context));
			break;

		default:
			fail("Unsupported EAX version.");
	}
}

void EaxxContext::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXCONTEXT_NONE:
			break;

		case ::EAXCONTEXT_ALLPARAMETERS:
			get_context_all(eax_call);
			break;

		case ::EAXCONTEXT_PRIMARYFXSLOTID:
			get_primary_fx_slot_id(eax_call);
			break;

		case ::EAXCONTEXT_DISTANCEFACTOR:
			get_distance_factor(eax_call);
			break;

		case ::EAXCONTEXT_AIRABSORPTIONHF:
			get_air_absorption_hf(eax_call);
			break;

		case ::EAXCONTEXT_HFREFERENCE:
			get_hf_reference(eax_call);
			break;

		case ::EAXCONTEXT_LASTERROR:
			get_last_error(eax_call);
			break;

		case ::EAXCONTEXT_SPEAKERCONFIG:
			get_speaker_config(eax_call);
			break;

		case ::EAXCONTEXT_EAXSESSION:
			get_eax_session(eax_call);
			break;

		case ::EAXCONTEXT_MACROFXFACTOR:
			get_macro_fx_factor(eax_call);
			break;

		default:
			fail("Unsupported property id.");
	}
}

void EaxxContext::set_primary_fx_slot_id()
{
	shared_.previous_primary_fx_slot_index = shared_.primary_fx_slot_index;
	shared_.primary_fx_slot_index = eax_.context.guidPrimaryFXSlotID;
}

void EaxxContext::set_distance_factor()
{
	::alListenerf(AL_METERS_PER_UNIT, eax_.context.flDistanceFactor);
}

void EaxxContext::set_air_absorbtion_hf()
{
	shared_.air_absorption_factor = eax_.context.flAirAbsorptionHF / ::EAXCONTEXT_DEFAULTAIRABSORPTIONHF;
}

void EaxxContext::set_hf_reference()
{
	// TODO
}

void EaxxContext::set_macro_fx_factor()
{
	// TODO
}

void EaxxContext::set_context()
{
	set_primary_fx_slot_id();
	set_distance_factor();
	set_air_absorbtion_hf();
	set_hf_reference();
}

void EaxxContext::initialize_fx_slots()
{
	shared_.fx_slots.initialize();
	shared_.previous_primary_fx_slot_index = eax_.context.guidPrimaryFXSlotID;
	shared_.primary_fx_slot_index = eax_.context.guidPrimaryFXSlotID;
}

void EaxxContext::update_sources()
{
	for (auto& source_item : source_map_)
	{
		source_item.second.update(context_shared_dirty_flags_);
	}
}

void EaxxContext::validate_primary_fx_slot_id(
	const ::GUID& primary_fx_slot_id)
{
	if (primary_fx_slot_id != ::EAX_NULL_GUID &&
		primary_fx_slot_id != ::EAXPROPERTYID_EAX40_FXSlot0 &&
		primary_fx_slot_id != ::EAXPROPERTYID_EAX50_FXSlot0 &&
		primary_fx_slot_id != ::EAXPROPERTYID_EAX40_FXSlot1 &&
		primary_fx_slot_id != ::EAXPROPERTYID_EAX50_FXSlot1 &&
		primary_fx_slot_id != ::EAXPROPERTYID_EAX40_FXSlot2 &&
		primary_fx_slot_id != ::EAXPROPERTYID_EAX50_FXSlot2 &&
		primary_fx_slot_id != ::EAXPROPERTYID_EAX40_FXSlot3 &&
		primary_fx_slot_id != ::EAXPROPERTYID_EAX50_FXSlot3)
	{
		fail("Unsupported primary FX slot id.");
	}
}

void EaxxContext::validate_distance_factor(
	float distance_factor)
{
	eaxx_validate_range<EaxxContextException>(
		"Distance Factor",
		distance_factor,
		::EAXCONTEXT_MINDISTANCEFACTOR,
		::EAXCONTEXT_MAXDISTANCEFACTOR
	);
}

void EaxxContext::validate_air_absorption_hf(
	float air_absorption_hf)
{
	eaxx_validate_range<EaxxContextException>(
		"Air Absorption HF",
		air_absorption_hf,
		::EAXCONTEXT_MINAIRABSORPTIONHF,
		::EAXCONTEXT_MAXAIRABSORPTIONHF
	);
}

void EaxxContext::validate_hf_reference(
	float hf_reference)
{
	eaxx_validate_range<EaxxContextException>(
		"HF Reference",
		hf_reference,
		::EAXCONTEXT_MINHFREFERENCE,
		::EAXCONTEXT_MAXHFREFERENCE
	);
}

void EaxxContext::validate_speaker_config(
	unsigned long speaker_config)
{
	switch (speaker_config)
	{
		case ::HEADPHONES:
		case ::SPEAKERS_2:
		case ::SPEAKERS_4:
		case ::SPEAKERS_5:
		case ::SPEAKERS_6:
		case ::SPEAKERS_7:
			break;

		default:
			fail("Unsupported speaker configuration.");
	}
}

void EaxxContext::validate_eax_session_eax_version(
	unsigned long eax_version)
{
	switch (eax_version)
	{
		case ::EAX_40:
		case ::EAX_50:
			break;

		default:
			fail("Unsupported session EAX version.");
	}
}

void EaxxContext::validate_eax_session_max_active_sends(
	unsigned long max_active_sends)
{
	eaxx_validate_range<EaxxContextException>(
		"Max Active Sends",
		max_active_sends,
		::EAXCONTEXT_MINMAXACTIVESENDS,
		::EAXCONTEXT_MAXMAXACTIVESENDS
	);
}

void EaxxContext::validate_eax_session(
	const ::EAXSESSIONPROPERTIES& eax_session)
{
	validate_eax_session_eax_version(eax_session.ulEAXVersion);
	validate_eax_session_max_active_sends(eax_session.ulMaxActiveSends);
}

void EaxxContext::validate_macro_fx_factor(
	float macro_fx_factor)
{
	eaxx_validate_range<EaxxContextException>(
		"Macro FX Factor",
		macro_fx_factor,
		::EAXCONTEXT_MINMACROFXFACTOR,
		::EAXCONTEXT_MAXMACROFXFACTOR
	);
}

void EaxxContext::validate_context_all(
	const ::EAX40CONTEXTPROPERTIES& context_all)
{
	validate_primary_fx_slot_id(context_all.guidPrimaryFXSlotID);
	validate_distance_factor(context_all.flDistanceFactor);
	validate_air_absorption_hf(context_all.flAirAbsorptionHF);
	validate_hf_reference(context_all.flHFReference);
}

void EaxxContext::validate_context_all(
	const ::EAX50CONTEXTPROPERTIES& context_all)
{
	validate_context_all(static_cast<const ::EAX40CONTEXTPROPERTIES>(context_all));
	validate_macro_fx_factor(context_all.flMacroFXFactor);
}

void EaxxContext::defer_primary_fx_slot_id(
	const ::GUID& primary_fx_slot_id)
{
	eax_d_.context.guidPrimaryFXSlotID = primary_fx_slot_id;

	context_dirty_flags_.guidPrimaryFXSlotID =
		(eax_.context.guidPrimaryFXSlotID != eax_d_.context.guidPrimaryFXSlotID);
}

void EaxxContext::defer_distance_factor(
	float distance_factor)
{
	eax_d_.context.flDistanceFactor = distance_factor;

	context_dirty_flags_.flDistanceFactor =
		(eax_.context.flDistanceFactor != eax_d_.context.flDistanceFactor);
}

void EaxxContext::defer_air_absorption_hf(
	float air_absorption_hf)
{
	eax_d_.context.flAirAbsorptionHF = air_absorption_hf;

	context_dirty_flags_.flAirAbsorptionHF =
		(eax_.context.flAirAbsorptionHF != eax_d_.context.flAirAbsorptionHF);
}

void EaxxContext::defer_hf_reference(
	float hf_reference)
{
	eax_d_.context.flHFReference = hf_reference;

	context_dirty_flags_.flHFReference =
		(eax_.context.flHFReference != eax_d_.context.flHFReference);
}

void EaxxContext::defer_macro_fx_factor(
	float macro_fx_factor)
{
	eax_d_.context.flMacroFXFactor = macro_fx_factor;

	context_dirty_flags_.flMacroFXFactor =
		(eax_.context.flMacroFXFactor != eax_d_.context.flMacroFXFactor);
}

void EaxxContext::defer_context_all(
	const ::EAX40CONTEXTPROPERTIES& context_all)
{
	defer_primary_fx_slot_id(context_all.guidPrimaryFXSlotID);
	defer_distance_factor(context_all.flDistanceFactor);
	defer_air_absorption_hf(context_all.flAirAbsorptionHF);
	defer_hf_reference(context_all.flHFReference);
}

void EaxxContext::defer_context_all(
	const ::EAX50CONTEXTPROPERTIES& context_all)
{
	defer_context_all(static_cast<const ::EAX40CONTEXTPROPERTIES&>(context_all));
	defer_macro_fx_factor(context_all.flMacroFXFactor);
}

void EaxxContext::defer_context_all(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_version())
	{
		case 4:
			{
				const auto& context_all =
					eax_call.get_value<EaxxContextException, ::EAX40CONTEXTPROPERTIES>();

				validate_context_all(context_all);
			}

			break;

		case 5:
			{
				const auto& context_all =
					eax_call.get_value<EaxxContextException, ::EAX50CONTEXTPROPERTIES>();

				validate_context_all(context_all);
			}

			break;

		default:
			fail("Unsupported EAX version.");
	}
}

void EaxxContext::defer_primary_fx_slot_id(
	const EaxxEaxCall& eax_call)
{
	const auto& primary_fx_slot_id =
		eax_call.get_value<EaxxContextException, const decltype(::EAX50CONTEXTPROPERTIES::guidPrimaryFXSlotID)>();

	validate_primary_fx_slot_id(primary_fx_slot_id);
	defer_primary_fx_slot_id(primary_fx_slot_id);
}

void EaxxContext::defer_distance_factor(
	const EaxxEaxCall& eax_call)
{
	const auto& distance_factor =
		eax_call.get_value<EaxxContextException, const decltype(::EAX50CONTEXTPROPERTIES::flDistanceFactor)>();

	validate_distance_factor(distance_factor);
	defer_distance_factor(distance_factor);
}

void EaxxContext::defer_air_absorption_hf(
	const EaxxEaxCall& eax_call)
{
	const auto& air_absorption_hf =
		eax_call.get_value<EaxxContextException, const decltype(::EAX50CONTEXTPROPERTIES::flAirAbsorptionHF)>();

	validate_air_absorption_hf(air_absorption_hf);
	defer_air_absorption_hf(air_absorption_hf);
}

void EaxxContext::defer_hf_reference(
	const EaxxEaxCall& eax_call)
{
	const auto& hf_reference =
		eax_call.get_value<EaxxContextException, const decltype(::EAX50CONTEXTPROPERTIES::flHFReference)>();

	validate_hf_reference(hf_reference);
	defer_hf_reference(hf_reference);
}

void EaxxContext::set_speaker_config(
	const EaxxEaxCall& eax_call)
{
	const auto speaker_config =
		eax_call.get_value<EaxxContextException, const unsigned long>();

	validate_speaker_config(speaker_config);

	eax_speaker_config_ = speaker_config;
}

void EaxxContext::set_eax_session(
	const EaxxEaxCall& eax_call)
{
	const auto& eax_session =
		eax_call.get_value<EaxxContextException, const ::EAXSESSIONPROPERTIES>();

	validate_eax_session(eax_session);

	eax_session_ = eax_session;
}

void EaxxContext::defer_macro_fx_factor(
	const EaxxEaxCall& eax_call)
{
	const auto& macro_fx_factor =
		eax_call.get_value<EaxxContextException, const decltype(EAX50CONTEXTPROPERTIES::flMacroFXFactor)>();

	validate_macro_fx_factor(macro_fx_factor);
	defer_macro_fx_factor(macro_fx_factor);
}

void EaxxContext::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXCONTEXT_NONE:
			break;

		case ::EAXCONTEXT_ALLPARAMETERS:
			defer_context_all(eax_call);
			break;

		case ::EAXCONTEXT_PRIMARYFXSLOTID:
			defer_primary_fx_slot_id(eax_call);
			break;

		case ::EAXCONTEXT_DISTANCEFACTOR:
			defer_distance_factor(eax_call);
			break;

		case ::EAXCONTEXT_AIRABSORPTIONHF:
			defer_air_absorption_hf(eax_call);
			break;

		case ::EAXCONTEXT_HFREFERENCE:
			defer_hf_reference(eax_call);
			break;

		case ::EAXCONTEXT_LASTERROR:
			fail("Setting last error not supported.");

		case ::EAXCONTEXT_SPEAKERCONFIG:
			set_speaker_config(eax_call);
			break;

		case ::EAXCONTEXT_EAXSESSION:
			set_eax_session(eax_call);
			break;

		case ::EAXCONTEXT_MACROFXFACTOR:
			defer_macro_fx_factor(eax_call);
			break;

		default:
			fail("Unsupported property id.");
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

void EaxxContext::apply_deferred()
{
	if (context_dirty_flags_ == EaxxContextContextDirtyFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (context_dirty_flags_.guidPrimaryFXSlotID)
	{
		context_shared_dirty_flags_.primary_fx_slot_id = true;
		set_primary_fx_slot_id();
	}

	if (context_dirty_flags_.flDistanceFactor)
	{
		set_distance_factor();
	}

	if (context_dirty_flags_.flAirAbsorptionHF)
	{
		context_shared_dirty_flags_.air_absorption_hf = true;
		set_air_absorbtion_hf();
	}

	if (context_dirty_flags_.flHFReference)
	{
		set_hf_reference();
	}

	if (context_dirty_flags_.flMacroFXFactor)
	{
		set_macro_fx_factor();
	}

	if (context_shared_dirty_flags_ != EaxxContextSharedDirtyFlags{})
	{
		update_sources();
	}

	context_shared_dirty_flags_ = EaxxContextSharedDirtyFlags{};
	context_dirty_flags_ = EaxxContextContextDirtyFlags{};
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
