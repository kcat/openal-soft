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


#include "eax_eaxx_source.h"

#include <algorithm>
#include <memory>

#include "AL/efx.h"

#include "eax_algorithm.h"
#include "eax_exception.h"

#include "eax_al_api.h"
#include "eax_api.h"
#include "eax_unit_converters.h"

#include "eax_eaxx_validators.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxSourceSourceDirtyFilterFlags& lhs,
	const EaxxSourceSourceDirtyFilterFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxSourceSourceDirtyFilterFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxSourceSourceDirtyFilterFlagsValue&>(rhs);
}

bool operator!=(
	const EaxxSourceSourceDirtyFilterFlags& lhs,
	const EaxxSourceSourceDirtyFilterFlags& rhs) noexcept
{
	return !(lhs == rhs);
}


bool operator==(
	const EaxxSourceSourceDirtyMiscFlags& lhs,
	const EaxxSourceSourceDirtyMiscFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxSourceSourceDirtyMiscFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxSourceSourceDirtyMiscFlagsValue&>(rhs);
}

bool operator!=(
	const EaxxSourceSourceDirtyMiscFlags& lhs,
	const EaxxSourceSourceDirtyMiscFlags& rhs) noexcept
{
	return !(lhs == rhs);
}


bool operator==(
	const EaxxSourceSendDirtyFlags& lhs,
	const EaxxSourceSendDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxSourceSendDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxSourceSendDirtyFlagsValue&>(rhs);
}

bool operator!=(
	const EaxxSourceSendDirtyFlags& lhs,
	const EaxxSourceSendDirtyFlags& rhs) noexcept
{
	return !(lhs == rhs);
}


bool operator==(
	const EaxxSourceSendsDirtyFlags& lhs,
	const EaxxSourceSendsDirtyFlags& rhs) noexcept
{
	return
		reinterpret_cast<const EaxxSourceSendsDirtyFlagsValue&>(lhs) ==
			reinterpret_cast<const EaxxSourceSendsDirtyFlagsValue&>(rhs);
}

bool operator!=(
	const EaxxSourceSendsDirtyFlags& lhs,
	const EaxxSourceSendsDirtyFlags& rhs) noexcept
{
	return !(lhs == rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxSourceException :
	public Exception
{
public:
	explicit EaxxSourceException(
		const char* message)
		:
		Exception{"EAXX_SOURCE", message}
	{
	}
}; // EaxxSourceException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxSourceActiveFxSlotsException :
	public Exception
{
public:
	explicit EaxxSourceActiveFxSlotsException(
		const char* message)
		:
		Exception{"EAXX_SOURCE_ACTIVE_FX_SLOTS", message}
	{
	}
}; // EaxxSourceActiveFxSlotsException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxSourceSendException :
	public Exception
{
public:
	explicit EaxxSourceSendException(
		const char* message)
		:
		Exception{"EAXX_SOURCE_SEND", message}
	{
	}
}; // EaxxSourceSendException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxSource::EaxxSource(
	const EaxxSourceInitParam& param)
{
	initialize(param);
}

void EaxxSource::on_initialize_context(
	::ALuint al_filter)
{
	if (al_filter == 0)
	{
		fail("Null AL filter.");
	}

	if (al_.filter != 0 && al_.filter != al_filter)
	{
		fail("AL filter already set.");
	}

	al_.filter = al_filter;

	set_fx_slots();
	update_filters_internal();

	set_outside_volume_hf();
	set_doppler_factor();
	set_rolloff_factor();
	set_room_rolloff_factor();
	set_air_absorption_factor();
	set_flags();
	set_macro_fx_factor();
}

void EaxxSource::dispatch(
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

void EaxxSource::update_filters()
{
	update_filters_internal();
}

void EaxxSource::update(
	EaxxContextSharedDirtyFlags dirty_flags)
{
	if (dirty_flags.primary_fx_slot_id)
	{
		if (uses_primary_id_)
		{
			update_primary_fx_slot_id();
		}
	}

	if (dirty_flags.air_absorption_hf)
	{
		set_air_absorption_factor();
	}
}

[[noreturn]]
void EaxxSource::fail(
	const char* message)
{
	throw EaxxSourceException{message};
}

void EaxxSource::validate_init_param(
	const EaxxSourceInitParam& param)
{
	if (param.al_source == 0)
	{
		fail("Null AL source.");
	}

	if (!param.context_shared)
	{
		fail("Null context shared.");
	}
}

void EaxxSource::copy_init_param(
	const EaxxSourceInitParam& param)
{
	al_.source = param.al_source;
	al_.filter = param.al_filter;
	context_shared_ = param.context_shared;
}

void EaxxSource::set_eax_source_defaults()
{
	eax_.source.lDirect = ::EAXSOURCE_DEFAULTDIRECT;
	eax_.source.lDirectHF = ::EAXSOURCE_DEFAULTDIRECTHF;
	eax_.source.lRoom = ::EAXSOURCE_DEFAULTROOM;
	eax_.source.lRoomHF = ::EAXSOURCE_DEFAULTROOMHF;
	eax_.source.lObstruction = ::EAXSOURCE_DEFAULTOBSTRUCTION;
	eax_.source.flObstructionLFRatio = ::EAXSOURCE_DEFAULTOBSTRUCTIONLFRATIO;
	eax_.source.lOcclusion = ::EAXSOURCE_DEFAULTOCCLUSION;
	eax_.source.flOcclusionLFRatio = ::EAXSOURCE_DEFAULTOCCLUSIONLFRATIO;
	eax_.source.flOcclusionRoomRatio = ::EAXSOURCE_DEFAULTOCCLUSIONROOMRATIO;
	eax_.source.flOcclusionDirectRatio = ::EAXSOURCE_DEFAULTOCCLUSIONDIRECTRATIO;
	eax_.source.lExclusion = ::EAXSOURCE_DEFAULTEXCLUSION;
	eax_.source.flExclusionLFRatio = ::EAXSOURCE_DEFAULTEXCLUSIONLFRATIO;
	eax_.source.lOutsideVolumeHF = ::EAXSOURCE_DEFAULTOUTSIDEVOLUMEHF;
	eax_.source.flDopplerFactor = ::EAXSOURCE_DEFAULTDOPPLERFACTOR;
	eax_.source.flRolloffFactor = ::EAXSOURCE_DEFAULTROLLOFFFACTOR;
	eax_.source.flRoomRolloffFactor = ::EAXSOURCE_DEFAULTROOMROLLOFFFACTOR;
	eax_.source.flAirAbsorptionFactor = ::EAXSOURCE_DEFAULTAIRABSORPTIONFACTOR;
	eax_.source.ulFlags = ::EAXSOURCE_DEFAULTFLAGS;
}

void EaxxSource::set_eax_active_fx_slots_defaults()
{
	eax_.active_fx_slots = ::EAX50SOURCE_3DDEFAULTACTIVEFXSLOTID;
}

void EaxxSource::set_eax_send_defaults(
	::EAXSOURCEALLSENDPROPERTIES& eax_send)
{
	eax_send.guidReceivingFXSlotID = ::EAX_NULL_GUID;
	eax_send.lSend = ::EAXSOURCE_DEFAULTSEND;
	eax_send.lSendHF = ::EAXSOURCE_DEFAULTSENDHF;
	eax_send.lOcclusion = ::EAXSOURCE_DEFAULTOCCLUSION;
	eax_send.flOcclusionLFRatio = ::EAXSOURCE_DEFAULTOCCLUSIONLFRATIO;
	eax_send.flOcclusionRoomRatio = ::EAXSOURCE_DEFAULTOCCLUSIONROOMRATIO;
	eax_send.flOcclusionDirectRatio = ::EAXSOURCE_DEFAULTOCCLUSIONDIRECTRATIO;
	eax_send.lExclusion = ::EAXSOURCE_DEFAULTEXCLUSION;
	eax_send.flExclusionLFRatio = ::EAXSOURCE_DEFAULTEXCLUSIONLFRATIO;
}

void EaxxSource::set_eax_sends_defaults()
{
	for (auto& eax_send : eax_.sends)
	{
		set_eax_send_defaults(eax_send);
	}
}

void EaxxSource::set_eax_speaker_levels_defaults()
{
	std::fill(eax_.speaker_levels.begin(), eax_.speaker_levels.end(), ::EAXSOURCE_DEFAULTSPEAKERLEVEL);
}

void EaxxSource::set_eax_defaults()
{
	set_eax_source_defaults();
	set_eax_active_fx_slots_defaults();
	set_eax_sends_defaults();
	set_eax_speaker_levels_defaults();
}

float EaxxSource::calculate_dst_occlusion_mb(
	long src_occlusion_mb,
	float path_ratio,
	float lf_ratio) noexcept
{
	const auto ratio_1 = path_ratio + lf_ratio - 1.0F;
	const auto ratio_2 = path_ratio * lf_ratio;
	const auto ratio = (ratio_2 > ratio_1) ? ratio_2 : ratio_1;
	const auto dst_occlustion_mb = static_cast<float>(src_occlusion_mb) * ratio;

	return dst_occlustion_mb;
}

AlLowPassParam EaxxSource::make_direct_filter_param() const noexcept
{
	auto gain_mb =
		static_cast<float>(eax_.source.lDirect) +

		(static_cast<float>(eax_.source.lObstruction) * eax_.source.flObstructionLFRatio) +

		calculate_dst_occlusion_mb(
			eax_.source.lOcclusion,
			eax_.source.flOcclusionDirectRatio,
			eax_.source.flOcclusionLFRatio
		)
	;

	auto gain_hf_mb =
		static_cast<float>(eax_.source.lDirectHF) +

		static_cast<float>(eax_.source.lObstruction) +

		(static_cast<float>(eax_.source.lOcclusion) * eax_.source.flOcclusionDirectRatio)
	;

	for (auto i = 0; i < ::EAX_MAX_FXSLOTS; ++i)
	{
		if (active_fx_slots_[i])
		{
			const auto& send = eax_.sends[i];

			gain_mb += calculate_dst_occlusion_mb(
				send.lOcclusion,
				send.flOcclusionDirectRatio,
				send.flOcclusionLFRatio
			);

			gain_hf_mb += static_cast<float>(send.lOcclusion) * send.flOcclusionDirectRatio;
		}
	}

	const auto al_low_pass_param = AlLowPassParam
	{
		clamp(level_mb_to_gain(gain_mb), 0.0F, context_shared_->max_filter_gain),
		clamp(level_mb_to_gain(gain_hf_mb), 0.0F, context_shared_->max_filter_gain)
	};

	return al_low_pass_param;
}

AlLowPassParam EaxxSource::make_room_filter_param(
	const EaxxFxSlot& fx_slot,
	const ::EAXSOURCEALLSENDPROPERTIES& send) const noexcept
{
	const auto& fx_slot_eax = fx_slot.get_eax_fx_slot();

	const auto gain_mb =
		static_cast<float>(
			eax_.source.lRoom +
			send.lSend) +

		calculate_dst_occlusion_mb(
			eax_.source.lOcclusion,
			eax_.source.flOcclusionRoomRatio,
			eax_.source.flOcclusionLFRatio
		) +

		calculate_dst_occlusion_mb(
			send.lOcclusion,
			send.flOcclusionRoomRatio,
			send.flOcclusionLFRatio
		) +

		(static_cast<float>(eax_.source.lExclusion) * eax_.source.flExclusionLFRatio) +
		(static_cast<float>(send.lExclusion) * send.flExclusionLFRatio) +

		0.0F
	;

	const auto gain_hf_mb =
		static_cast<float>(
			eax_.source.lRoomHF +
			send.lSendHF) +

		(static_cast<float>(fx_slot_eax.lOcclusion + eax_.source.lOcclusion) * eax_.source.flOcclusionRoomRatio) +
		(static_cast<float>(send.lOcclusion) * send.flOcclusionRoomRatio) +

		static_cast<float>(
			eax_.source.lExclusion +
			send.lExclusion) +

		0.0F
	;

	const auto al_low_pass_param = AlLowPassParam
	{
		clamp(level_mb_to_gain(gain_mb), 0.0F, context_shared_->max_filter_gain),
		clamp(level_mb_to_gain(gain_hf_mb), 0.0F, context_shared_->max_filter_gain)
	};

	return al_low_pass_param;
}

void EaxxSource::set_al_filter_parameters(
	const AlLowPassParam& al_low_pass_param) const noexcept
{
	::alFilterf(al_.filter, AL_LOWPASS_GAIN, al_low_pass_param.gain);
	::alFilterf(al_.filter, AL_LOWPASS_GAINHF, al_low_pass_param.gain_hf);
}

void EaxxSource::set_fx_slots()
{
	uses_primary_id_ = false;
	has_active_fx_slots_ = false;

	for (auto i = 0; i < ::EAX_MAX_FXSLOTS; ++i)
	{
		const auto& eax_active_fx_slot_id = eax_.active_fx_slots.guidActiveFXSlots[i];

		auto fx_slot_index = EaxxFxSlotIndex{};

		if (eax_active_fx_slot_id == ::EAX_PrimaryFXSlotID)
		{
			uses_primary_id_ = true;
			fx_slot_index = context_shared_->primary_fx_slot_index;
		}
		else
		{
			fx_slot_index = eax_active_fx_slot_id;
		}

		if (fx_slot_index.has_value())
		{
			has_active_fx_slots_ = true;
			active_fx_slots_[fx_slot_index] = true;
		}
	}

	for (auto i = 0; i < ::EAX_MAX_FXSLOTS; ++i)
	{
		if (!active_fx_slots_[i])
		{
			::alSource3i(
				al_.source,
				AL_AUXILIARY_SEND_FILTER,
				AL_EFFECTSLOT_NULL,
				i,
				AL_FILTER_NULL
			);
		}
	}
}

void EaxxSource::initialize_fx_slots()
{
	set_fx_slots();
	update_filters_internal();
}

void EaxxSource::initialize(
	const EaxxSourceInitParam& param)
{
	validate_init_param(param);
	copy_init_param(param);
	set_eax_defaults();

	if (al_.filter != AL_NONE)
	{
		initialize_fx_slots();
	}

	eax_d_ = eax_;
}

void EaxxSource::update_direct_filter_internal()
{
	const auto& direct_param = make_direct_filter_param();
	set_al_filter_parameters(direct_param);

	::alSourcei(
		al_.source,
		AL_DIRECT_FILTER,
		static_cast<::ALint>(al_.filter)
	);
}

void EaxxSource::update_room_filters_internal()
{
	if (!has_active_fx_slots_)
	{
		return;
	}

	for (auto i = 0; i < ::EAX_MAX_FXSLOTS; ++i)
	{
		if (active_fx_slots_[i])
		{
			const auto& fx_slot = context_shared_->fx_slots.get(i);
			const auto& send = eax_.sends[i];
			const auto& room_param = make_room_filter_param(fx_slot, send);
			const auto efx_effect_slot = fx_slot.get_efx_effect_slot();
			set_al_filter_parameters(room_param);

			::alSource3i(
				static_cast<::ALint>(al_.source),
				AL_AUXILIARY_SEND_FILTER,
				static_cast<::ALint>(efx_effect_slot),
				i,
				static_cast<::ALint>(al_.filter)
			);
		}
	}
}

void EaxxSource::update_filters_internal()
{
	update_direct_filter_internal();
	update_room_filters_internal();
}

void EaxxSource::update_primary_fx_slot_id()
{
	const auto& previous_primary_fx_slot_index = context_shared_->previous_primary_fx_slot_index;
	const auto& primary_fx_slot_index = context_shared_->primary_fx_slot_index;

	if (previous_primary_fx_slot_index == primary_fx_slot_index)
	{
		return;
	}

	if (previous_primary_fx_slot_index.has_value())
	{
		const auto fx_slot_index = previous_primary_fx_slot_index.get();
		active_fx_slots_[fx_slot_index] = false;

		::alSource3i(
			al_.source,
			AL_AUXILIARY_SEND_FILTER,
			AL_EFFECTSLOT_NULL,
			fx_slot_index,
			static_cast<::ALint>(AL_FILTER_NULL)
		);
	}

	if (primary_fx_slot_index.has_value())
	{
		const auto fx_slot_index = primary_fx_slot_index.get();
		active_fx_slots_[fx_slot_index] = true;

		const auto& fx_slot = context_shared_->fx_slots.get(fx_slot_index);
		const auto& send = eax_.sends[fx_slot_index];
		const auto& room_param = make_room_filter_param(fx_slot, send);
		const auto efx_effect_slot = fx_slot.get_efx_effect_slot();
		set_al_filter_parameters(room_param);

		::alSource3i(
			static_cast<::ALint>(al_.source),
			AL_AUXILIARY_SEND_FILTER,
			static_cast<::ALint>(efx_effect_slot),
			fx_slot_index,
			static_cast<::ALint>(al_.filter)
		);
	}

	has_active_fx_slots_ = std::any_of(
		active_fx_slots_.cbegin(),
		active_fx_slots_.cend(),
		[](const auto& item)
		{
			return item;
		}
	);
}

void EaxxSource::defer_active_fx_slots(
	const EaxxEaxCall& eax_call)
{
	const auto active_fx_slots_span =
		eax_call.get_values<EaxxSourceActiveFxSlotsException, const ::GUID>();

	const auto fx_slot_count = active_fx_slots_span.size;

	if (fx_slot_count <= 0 || fx_slot_count > ::EAX_MAX_FXSLOTS)
	{
		throw EaxxSourceActiveFxSlotsException{"Count out of range."};
	}

	for (auto i = 0; i < fx_slot_count; ++i)
	{
		const auto& fx_slot_guid = active_fx_slots_span.values[i];

		if (fx_slot_guid != ::EAX_NULL_GUID &&
			fx_slot_guid != ::EAX_PrimaryFXSlotID &&
			fx_slot_guid != ::EAXPROPERTYID_EAX40_FXSlot0 &&
			fx_slot_guid != ::EAXPROPERTYID_EAX50_FXSlot0 &&
			fx_slot_guid != ::EAXPROPERTYID_EAX40_FXSlot1 &&
			fx_slot_guid != ::EAXPROPERTYID_EAX50_FXSlot1 &&
			fx_slot_guid != ::EAXPROPERTYID_EAX40_FXSlot2 &&
			fx_slot_guid != ::EAXPROPERTYID_EAX50_FXSlot2 &&
			fx_slot_guid != ::EAXPROPERTYID_EAX40_FXSlot3 &&
			fx_slot_guid != ::EAXPROPERTYID_EAX50_FXSlot3)
		{
			throw EaxxSourceActiveFxSlotsException{"Unsupported GUID."};
		}
	}

	for (auto i = 0; i < fx_slot_count; ++i)
	{
		eax_d_.active_fx_slots.guidActiveFXSlots[i] = active_fx_slots_span.values[i];
	}

	for (auto i = fx_slot_count; i < ::EAX_MAX_FXSLOTS; ++i)
	{
		eax_d_.active_fx_slots.guidActiveFXSlots[i] = ::EAX_NULL_GUID;
	}

	are_active_fx_slots_dirty_ = (eax_d_.active_fx_slots != eax_.active_fx_slots);
}

// ----------------------------------------------------------------------
// Common

const char* EaxxSource::get_exclusion_name() noexcept
{
	return "Exclusion";
}

const char* EaxxSource::get_exclusion_lf_ratio_name() noexcept
{
	return "Exclusion LF Ratio";
}

const char* EaxxSource::get_occlusion_name() noexcept
{
	return "Occlusion";
}

const char* EaxxSource::get_occlusion_lf_ratio_name() noexcept
{
	return "Occlusion LF Ratio";
}

const char* EaxxSource::get_occlusion_direct_ratio_name() noexcept
{
	return "Occlusion Direct Ratio";
}

const char* EaxxSource::get_occlusion_room_ratio_name() noexcept
{
	return "Occlusion Room Ratio";
}

// Common
// ----------------------------------------------------------------------


// ----------------------------------------------------------------------
// Send

void EaxxSource::validate_send_receiving_fx_slot_guid(
	const ::GUID& guidReceivingFXSlotID)
{
	if (guidReceivingFXSlotID != ::EAXPROPERTYID_EAX40_FXSlot0 &&
		guidReceivingFXSlotID != ::EAXPROPERTYID_EAX50_FXSlot0 &&
		guidReceivingFXSlotID != ::EAXPROPERTYID_EAX40_FXSlot1 &&
		guidReceivingFXSlotID != ::EAXPROPERTYID_EAX50_FXSlot1 &&
		guidReceivingFXSlotID != ::EAXPROPERTYID_EAX40_FXSlot2 &&
		guidReceivingFXSlotID != ::EAXPROPERTYID_EAX50_FXSlot2 &&
		guidReceivingFXSlotID != ::EAXPROPERTYID_EAX40_FXSlot3 &&
		guidReceivingFXSlotID != ::EAXPROPERTYID_EAX50_FXSlot3)
	{
		throw EaxxSourceSendException{"Unsupported receiving FX slot GUID."};
	}
}

void EaxxSource::validate_send_send(
	long lSend)
{
	eaxx_validate_range<EaxxSourceSendException>(
		"Send",
		lSend,
		::EAXSOURCE_MINSEND,
		::EAXSOURCE_MAXSEND
	);
}

void EaxxSource::validate_send_send_hf(
	long lSendHF)
{
	eaxx_validate_range<EaxxSourceSendException>(
		"Send HF",
		lSendHF,
		::EAXSOURCE_MINSENDHF,
		::EAXSOURCE_MAXSENDHF
	);
}

void EaxxSource::validate_send_occlusion(
	long lOcclusion)
{
	eaxx_validate_range<EaxxSourceSendException>(
		get_occlusion_name(),
		lOcclusion,
		::EAXSOURCE_MINOCCLUSION,
		::EAXSOURCE_MAXOCCLUSION
	);
}

void EaxxSource::validate_send_occlusion_lf_ratio(
	float flOcclusionLFRatio)
{
	eaxx_validate_range<EaxxSourceSendException>(
		get_occlusion_lf_ratio_name(),
		flOcclusionLFRatio,
		::EAXSOURCE_MINOCCLUSIONLFRATIO,
		::EAXSOURCE_MAXOCCLUSIONLFRATIO
	);
}

void EaxxSource::validate_send_occlusion_room_ratio(
	float flOcclusionRoomRatio)
{
	eaxx_validate_range<EaxxSourceSendException>(
		get_occlusion_room_ratio_name(),
		flOcclusionRoomRatio,
		::EAXSOURCE_MINOCCLUSIONROOMRATIO,
		::EAXSOURCE_MAXOCCLUSIONROOMRATIO
	);
}

void EaxxSource::validate_send_occlusion_direct_ratio(
	float flOcclusionDirectRatio)
{
	eaxx_validate_range<EaxxSourceSendException>(
		get_occlusion_direct_ratio_name(),
		flOcclusionDirectRatio,
		::EAXSOURCE_MINOCCLUSIONDIRECTRATIO,
		::EAXSOURCE_MAXOCCLUSIONDIRECTRATIO
	);
}

void EaxxSource::validate_send_exclusion(
	long lExclusion)
{
	eaxx_validate_range<EaxxSourceSendException>(
		get_exclusion_name(),
		lExclusion,
		::EAXSOURCE_MINEXCLUSION,
		::EAXSOURCE_MAXEXCLUSION
	);
}

void EaxxSource::validate_send_exclusion_lf_ratio(
	float flExclusionLFRatio)
{
	eaxx_validate_range<EaxxSourceSendException>(
		get_exclusion_lf_ratio_name(),
		flExclusionLFRatio,
		::EAXSOURCE_MINEXCLUSIONLFRATIO,
		::EAXSOURCE_MAXEXCLUSIONLFRATIO
	);
}

void EaxxSource::validate_send(
	const ::EAXSOURCESENDPROPERTIES& all)
{
	validate_send_receiving_fx_slot_guid(all.guidReceivingFXSlotID);
	validate_send_send(all.lSend);
	validate_send_send_hf(all.lSendHF);
}

void EaxxSource::validate_send_exclusion_all(
	const ::EAXSOURCEEXCLUSIONSENDPROPERTIES& all)
{
	validate_send_receiving_fx_slot_guid(all.guidReceivingFXSlotID);
	validate_send_exclusion(all.lExclusion);
	validate_send_exclusion_lf_ratio(all.flExclusionLFRatio);
}

void EaxxSource::validate_send_occlusion_all(
	const ::EAXSOURCEOCCLUSIONSENDPROPERTIES& all)
{
	validate_send_receiving_fx_slot_guid(all.guidReceivingFXSlotID);
	validate_send_occlusion(all.lOcclusion);
	validate_send_occlusion_lf_ratio(all.flOcclusionLFRatio);
	validate_send_occlusion_room_ratio(all.flOcclusionRoomRatio);
	validate_send_occlusion_direct_ratio(all.flOcclusionDirectRatio);
}

void EaxxSource::validate_send_all(
	const ::EAXSOURCEALLSENDPROPERTIES& all)
{
	validate_send_receiving_fx_slot_guid(all.guidReceivingFXSlotID);
	validate_send_send(all.lSend);
	validate_send_send_hf(all.lSendHF);
	validate_send_occlusion(all.lOcclusion);
	validate_send_occlusion_lf_ratio(all.flOcclusionLFRatio);
	validate_send_occlusion_room_ratio(all.flOcclusionRoomRatio);
	validate_send_occlusion_direct_ratio(all.flOcclusionDirectRatio);
	validate_send_exclusion(all.lExclusion);
	validate_send_exclusion_lf_ratio(all.flExclusionLFRatio);
}

int EaxxSource::get_send_index(
	const ::GUID& send_guid)
{
	if (false)
	{
	}
	else if (send_guid == ::EAXPROPERTYID_EAX40_FXSlot0 || send_guid == ::EAXPROPERTYID_EAX50_FXSlot0)
	{
		return 0;
	}
	else if (send_guid == ::EAXPROPERTYID_EAX40_FXSlot1 || send_guid == ::EAXPROPERTYID_EAX50_FXSlot1)
	{
		return 1;
	}
	else if (send_guid == ::EAXPROPERTYID_EAX40_FXSlot2 || send_guid == ::EAXPROPERTYID_EAX50_FXSlot2)
	{
		return 2;
	}
	else if (send_guid == ::EAXPROPERTYID_EAX40_FXSlot3 || send_guid == ::EAXPROPERTYID_EAX50_FXSlot3)
	{
		return 3;
	}
	else
	{
		throw EaxxSourceSendException{"Unsupported receiving FX slot GUID."};
	}
}

void EaxxSource::defer_send_send(
	long lSend,
	int index)
{
	eax_d_.sends[index].lSend = lSend;

	sends_dirty_flags_.sends[index].lSend =
		(eax_.sends[index].lSend != eax_d_.sends[index].lSend);
}

void EaxxSource::defer_send_send_hf(
	long lSendHF,
	int index)
{
	eax_d_.sends[index].lSendHF = lSendHF;

	sends_dirty_flags_.sends[index].lSendHF =
		(eax_.sends[index].lSendHF != eax_d_.sends[index].lSendHF);
}

void EaxxSource::defer_send_occlusion(
	long lOcclusion,
	int index)
{
	eax_d_.sends[index].lOcclusion = lOcclusion;

	sends_dirty_flags_.sends[index].lOcclusion =
		(eax_.sends[index].lOcclusion != eax_d_.sends[index].lOcclusion);
}

void EaxxSource::defer_send_occlusion_lf_ratio(
	float flOcclusionLFRatio,
	int index)
{
	eax_d_.sends[index].flOcclusionLFRatio = flOcclusionLFRatio;

	sends_dirty_flags_.sends[index].flOcclusionLFRatio =
		(eax_.sends[index].flOcclusionLFRatio != eax_d_.sends[index].flOcclusionLFRatio);
}

void EaxxSource::defer_send_occlusion_room_ratio(
	float flOcclusionRoomRatio,
	int index)
{
	eax_d_.sends[index].flOcclusionRoomRatio = flOcclusionRoomRatio;

	sends_dirty_flags_.sends[index].flOcclusionRoomRatio =
		(eax_.sends[index].flOcclusionRoomRatio != eax_d_.sends[index].flOcclusionRoomRatio);
}

void EaxxSource::defer_send_occlusion_direct_ratio(
	float flOcclusionDirectRatio,
	int index)
{
	eax_d_.sends[index].flOcclusionDirectRatio = flOcclusionDirectRatio;

	sends_dirty_flags_.sends[index].flOcclusionDirectRatio =
		(eax_.sends[index].flOcclusionDirectRatio != eax_d_.sends[index].flOcclusionDirectRatio);
}

void EaxxSource::defer_send_exclusion(
	long lExclusion,
	int index)
{
	eax_d_.sends[index].lExclusion = lExclusion;

	sends_dirty_flags_.sends[index].lExclusion =
		(eax_.sends[index].lExclusion != eax_d_.sends[index].lExclusion);
}

void EaxxSource::defer_send_exclusion_lf_ratio(
	float flExclusionLFRatio,
	int index)
{
	eax_d_.sends[index].flExclusionLFRatio = flExclusionLFRatio;

	sends_dirty_flags_.sends[index].flExclusionLFRatio =
		(eax_.sends[index].flExclusionLFRatio != eax_d_.sends[index].flExclusionLFRatio);
}

void EaxxSource::defer_send(
	const ::EAXSOURCESENDPROPERTIES& all,
	int index)
{
	defer_send_send(all.lSend, index);
	defer_send_send_hf(all.lSendHF, index);
}

void EaxxSource::defer_send_exclusion_all(
	const ::EAXSOURCEEXCLUSIONSENDPROPERTIES& all,
	int index)
{
	defer_send_exclusion(all.lExclusion, index);
	defer_send_exclusion_lf_ratio(all.flExclusionLFRatio, index);
}

void EaxxSource::defer_send_occlusion_all(
	const ::EAXSOURCEOCCLUSIONSENDPROPERTIES& all,
	int index)
{
	defer_send_occlusion(all.lOcclusion, index);
	defer_send_occlusion_lf_ratio(all.flOcclusionLFRatio, index);
	defer_send_occlusion_room_ratio(all.flOcclusionRoomRatio, index);
	defer_send_occlusion_direct_ratio(all.flOcclusionDirectRatio, index);
}

void EaxxSource::defer_send_all(
	const ::EAXSOURCEALLSENDPROPERTIES& all,
	int index)
{
	defer_send_send(all.lSend, index);
	defer_send_send_hf(all.lSendHF, index);
	defer_send_occlusion(all.lOcclusion, index);
	defer_send_occlusion_lf_ratio(all.flOcclusionLFRatio, index);
	defer_send_occlusion_room_ratio(all.flOcclusionRoomRatio, index);
	defer_send_occlusion_direct_ratio(all.flOcclusionDirectRatio, index);
	defer_send_exclusion(all.lExclusion, index);
	defer_send_exclusion_lf_ratio(all.flExclusionLFRatio, index);
}

void EaxxSource::defer_send(
	const EaxxEaxCall& eax_call)
{
	const auto eax_all_span =
		eax_call.get_values<EaxxSourceException, const ::EAXSOURCESENDPROPERTIES>();

	const auto count = eax_all_span.size;

	if (count <= 0 || count > ::EAX_MAX_FXSLOTS)
	{
		throw EaxxSourceSendException{"Send count out of range."};
	}

	for (auto i = 0; i < count; ++i)
	{
		const auto& all = eax_all_span.values[i];
		validate_send(all);
	}

	for (auto i = 0; i < count; ++i)
	{
		const auto& all = eax_all_span.values[i];
		const auto send_index = get_send_index(all.guidReceivingFXSlotID);
		defer_send(all, send_index);
	}
}

void EaxxSource::defer_send_exclusion_all(
	const EaxxEaxCall& eax_call)
{
	const auto eax_all_span =
		eax_call.get_values<EaxxSourceException, const ::EAXSOURCEEXCLUSIONSENDPROPERTIES>();

	const auto count = eax_all_span.size;

	if (count <= 0 || count > ::EAX_MAX_FXSLOTS)
	{
		throw EaxxSourceSendException{"Send exclusion all count out of range."};
	}

	for (auto i = 0; i < count; ++i)
	{
		const auto& all = eax_all_span.values[i];
		validate_send_exclusion_all(all);
	}

	for (auto i = 0; i < count; ++i)
	{
		const auto& all = eax_all_span.values[i];
		const auto send_index = get_send_index(all.guidReceivingFXSlotID);
		defer_send_exclusion_all(all, send_index);
	}
}

void EaxxSource::defer_send_occlusion_all(
	const EaxxEaxCall& eax_call)
{
	const auto eax_all_span =
		eax_call.get_values<EaxxSourceException, const ::EAXSOURCEOCCLUSIONSENDPROPERTIES>();

	const auto count = eax_all_span.size;

	if (count <= 0 || count > ::EAX_MAX_FXSLOTS)
	{
		throw EaxxSourceSendException{"Send occlusion all count out of range."};
	}

	for (auto i = 0; i < count; ++i)
	{
		const auto& all = eax_all_span.values[i];
		validate_send_occlusion_all(all);
	}

	for (auto i = 0; i < count; ++i)
	{
		const auto& all = eax_all_span.values[i];
		const auto send_index = get_send_index(all.guidReceivingFXSlotID);
		defer_send_occlusion_all(all, send_index);
	}
}

void EaxxSource::defer_send_all(
	const EaxxEaxCall& eax_call)
{
	const auto eax_all_span =
		eax_call.get_values<EaxxSourceException, const ::EAXSOURCEALLSENDPROPERTIES>();

	const auto count = eax_all_span.size;

	if (count <= 0 || count > ::EAX_MAX_FXSLOTS)
	{
		throw EaxxSourceSendException{"Send all count out of range."};
	}

	for (auto i = 0; i < count; ++i)
	{
		const auto& all = eax_all_span.values[i];
		validate_send_all(all);
	}

	for (auto i = 0; i < count; ++i)
	{
		const auto& all = eax_all_span.values[i];
		const auto send_index = get_send_index(all.guidReceivingFXSlotID);
		defer_send_all(all, send_index);
	}
}

// Send
// ----------------------------------------------------------------------

void EaxxSource::validate_source_direct(
	long direct)
{
	eaxx_validate_range<EaxxSourceException>(
		"Direct",
		direct,
		::EAXSOURCE_MINDIRECT,
		::EAXSOURCE_MAXDIRECT
	);
}

void EaxxSource::validate_source_direct_hf(
	long direct_hf)
{
	eaxx_validate_range<EaxxSourceException>(
		"Direct HF",
		direct_hf,
		::EAXSOURCE_MINDIRECTHF,
		::EAXSOURCE_MAXDIRECTHF
	);
}

void EaxxSource::validate_source_room(
	long room)
{
	eaxx_validate_range<EaxxSourceException>(
		"Room",
		room,
		::EAXSOURCE_MINROOM,
		::EAXSOURCE_MAXROOM
	);
}

void EaxxSource::validate_source_room_hf(
	long room_hf)
{
	eaxx_validate_range<EaxxSourceException>(
		"Room HF",
		room_hf,
		::EAXSOURCE_MINROOMHF,
		::EAXSOURCE_MAXROOMHF
	);
}

void EaxxSource::validate_source_obstruction(
	long obstruction)
{
	eaxx_validate_range<EaxxSourceException>(
		"Obstruction",
		obstruction,
		::EAXSOURCE_MINOBSTRUCTION,
		::EAXSOURCE_MAXOBSTRUCTION
	);
}

void EaxxSource::validate_source_obstruction_lf_ratio(
	float obstruction_lf_ratio)
{
	eaxx_validate_range<EaxxSourceException>(
		"Obstruction LF Ratio",
		obstruction_lf_ratio,
		::EAXSOURCE_MINOBSTRUCTIONLFRATIO,
		::EAXSOURCE_MAXOBSTRUCTIONLFRATIO
	);
}

void EaxxSource::validate_source_occlusion(
	long occlusion)
{
	eaxx_validate_range<EaxxSourceException>(
		get_occlusion_name(),
		occlusion,
		::EAXSOURCE_MINOCCLUSION,
		::EAXSOURCE_MAXOCCLUSION
	);
}

void EaxxSource::validate_source_occlusion_lf_ratio(
	float occlusion_lf_ratio)
{
	eaxx_validate_range<EaxxSourceException>(
		get_occlusion_lf_ratio_name(),
		occlusion_lf_ratio,
		::EAXSOURCE_MINOCCLUSIONLFRATIO,
		::EAXSOURCE_MAXOCCLUSIONLFRATIO
	);
}

void EaxxSource::validate_source_occlusion_room_ratio(
	float occlusion_room_ratio)
{
	eaxx_validate_range<EaxxSourceException>(
		get_occlusion_room_ratio_name(),
		occlusion_room_ratio,
		::EAXSOURCE_MINOCCLUSIONROOMRATIO,
		::EAXSOURCE_MAXOCCLUSIONROOMRATIO
	);
}

void EaxxSource::validate_source_occlusion_direct_ratio(
	float occlusion_direct_ratio)
{
	eaxx_validate_range<EaxxSourceException>(
		get_occlusion_direct_ratio_name(),
		occlusion_direct_ratio,
		::EAXSOURCE_MINOCCLUSIONDIRECTRATIO,
		::EAXSOURCE_MAXOCCLUSIONDIRECTRATIO
	);
}

void EaxxSource::validate_source_exclusion(
	long exclusion)
{
	eaxx_validate_range<EaxxSourceException>(
		get_exclusion_name(),
		exclusion,
		::EAXSOURCE_MINEXCLUSION,
		::EAXSOURCE_MAXEXCLUSION
	);
}

void EaxxSource::validate_source_exclusion_lf_ratio(
	float exclusion_lf_ratio)
{
	eaxx_validate_range<EaxxSourceException>(
		get_exclusion_lf_ratio_name(),
		exclusion_lf_ratio,
		::EAXSOURCE_MINEXCLUSIONLFRATIO,
		::EAXSOURCE_MAXEXCLUSIONLFRATIO
	);
}

void EaxxSource::validate_source_outside_volume_hf(
	long outside_volume_hf)
{
	eaxx_validate_range<EaxxSourceException>(
		"Outside Volume HF",
		outside_volume_hf,
		::EAXSOURCE_MINOUTSIDEVOLUMEHF,
		::EAXSOURCE_MAXOUTSIDEVOLUMEHF
	);
}

void EaxxSource::validate_source_doppler_factor(
	float doppler_factor)
{
	eaxx_validate_range<EaxxSourceException>(
		"Doppler Factor",
		doppler_factor,
		::EAXSOURCE_MINDOPPLERFACTOR,
		::EAXSOURCE_MAXDOPPLERFACTOR
	);
}

void EaxxSource::validate_source_rolloff_factor(
	float rolloff_factor)
{
	eaxx_validate_range<EaxxSourceException>(
		"Rolloff Factor",
		rolloff_factor,
		::EAXSOURCE_MINROLLOFFFACTOR,
		::EAXSOURCE_MAXROLLOFFFACTOR
	);
}

void EaxxSource::validate_source_room_rolloff_factor(
	float room_rolloff_factor)
{
	eaxx_validate_range<EaxxSourceException>(
		"Room Rolloff Factor",
		room_rolloff_factor,
		::EAXSOURCE_MINROOMROLLOFFFACTOR,
		::EAXSOURCE_MAXROOMROLLOFFFACTOR
	);
}

void EaxxSource::validate_source_air_absorption_factor(
	float air_absorption_factor)
{
	eaxx_validate_range<EaxxSourceException>(
		"Air Absorption Factor",
		air_absorption_factor,
		::EAXSOURCE_MINAIRABSORPTIONFACTOR,
		::EAXSOURCE_MAXAIRABSORPTIONFACTOR
	);
}

void EaxxSource::validate_source_flags(
	unsigned long flags,
	int eax_version)
{
	eaxx_validate_range<EaxxSourceException>(
		"Flags",
		flags,
		0UL,
		~((eax_version == 5) ? ::EAX50SOURCEFLAGS_RESERVED : ::EAX20SOURCEFLAGS_RESERVED)
	);
}

void EaxxSource::validate_source_macro_fx_factor(
	float macro_fx_factor)
{
	eaxx_validate_range<EaxxSourceException>(
		"Macro FX Factor",
		macro_fx_factor,
		::EAXSOURCE_MINMACROFXFACTOR,
		::EAXSOURCE_MAXMACROFXFACTOR
	);
}

void EaxxSource::validate_source_2d_all(
	const ::EAXSOURCE2DPROPERTIES& all,
	int eax_version)
{
	validate_source_direct(all.lDirect);
	validate_source_direct_hf(all.lDirectHF);
	validate_source_room(all.lRoom);
	validate_source_room_hf(all.lRoomHF);
	validate_source_flags(all.ulFlags, eax_version);
}

void EaxxSource::validate_source_obstruction_all(
	const ::EAXOBSTRUCTIONPROPERTIES& all)
{
	validate_source_obstruction(all.lObstruction);
	validate_source_obstruction_lf_ratio(all.flObstructionLFRatio);
}

void EaxxSource::validate_source_exclusion_all(
	const ::EAXEXCLUSIONPROPERTIES& all)
{
	validate_source_exclusion(all.lExclusion);
	validate_source_exclusion_lf_ratio(all.flExclusionLFRatio);
}

void EaxxSource::validate_source_occlusion_all(
	const ::EAXOCCLUSIONPROPERTIES& all)
{
	validate_source_occlusion(all.lOcclusion);
	validate_source_occlusion_lf_ratio(all.flOcclusionLFRatio);
	validate_source_occlusion_room_ratio(all.flOcclusionRoomRatio);
	validate_source_occlusion_direct_ratio(all.flOcclusionDirectRatio);
}

void EaxxSource::validate_source_all(
	const ::EAX20BUFFERPROPERTIES& all,
	int eax_version)
{
	validate_source_direct(all.lDirect);
	validate_source_direct_hf(all.lDirectHF);
	validate_source_room(all.lRoom);
	validate_source_room_hf(all.lRoomHF);
	validate_source_obstruction(all.lObstruction);
	validate_source_obstruction_lf_ratio(all.flObstructionLFRatio);
	validate_source_occlusion(all.lOcclusion);
	validate_source_occlusion_lf_ratio(all.flOcclusionLFRatio);
	validate_source_occlusion_room_ratio(all.flOcclusionRoomRatio);
	validate_source_outside_volume_hf(all.lOutsideVolumeHF);
	validate_source_room_rolloff_factor(all.flRoomRolloffFactor);
	validate_source_air_absorption_factor(all.flAirAbsorptionFactor);
	validate_source_flags(all.dwFlags, eax_version);
}

void EaxxSource::validate_source_all(
	const ::EAX30SOURCEPROPERTIES& all,
	int eax_version)
{
	validate_source_direct(all.lDirect);
	validate_source_direct_hf(all.lDirectHF);
	validate_source_room(all.lRoom);
	validate_source_room_hf(all.lRoomHF);
	validate_source_obstruction(all.lObstruction);
	validate_source_obstruction_lf_ratio(all.flObstructionLFRatio);
	validate_source_occlusion(all.lOcclusion);
	validate_source_occlusion_lf_ratio(all.flOcclusionLFRatio);
	validate_source_occlusion_room_ratio(all.flOcclusionRoomRatio);
	validate_source_occlusion_direct_ratio(all.flOcclusionDirectRatio);
	validate_source_exclusion(all.lExclusion);
	validate_source_exclusion_lf_ratio(all.flExclusionLFRatio);
	validate_source_outside_volume_hf(all.lOutsideVolumeHF);
	validate_source_doppler_factor(all.flDopplerFactor);
	validate_source_rolloff_factor(all.flRolloffFactor);
	validate_source_room_rolloff_factor(all.flRoomRolloffFactor);
	validate_source_air_absorption_factor(all.flAirAbsorptionFactor);
	validate_source_flags(all.ulFlags, eax_version);
}

void EaxxSource::validate_source_all(
	const ::EAX50SOURCEPROPERTIES& all,
	int eax_version)
{
	validate_source_all(static_cast<::EAX30SOURCEPROPERTIES>(all), eax_version);
	validate_source_macro_fx_factor(all.flMacroFXFactor);
}

void EaxxSource::validate_source_speaker_id(
	long speaker_id)
{
	eaxx_validate_range<EaxxSourceException>(
		"Speaker Id",
		speaker_id,
		static_cast<long>(::EAXSPEAKER_FRONT_LEFT),
		static_cast<long>(::EAXSPEAKER_LOW_FREQUENCY)
	);
}

void EaxxSource::validate_source_speaker_level(
	long speaker_level)
{
	eaxx_validate_range<EaxxSourceException>(
		"Speaker Level",
		speaker_level,
		::EAXSOURCE_MINSPEAKERLEVEL,
		::EAXSOURCE_MAXSPEAKERLEVEL
	);
}

void EaxxSource::validate_source_speaker_level_all(
	const ::EAXSPEAKERLEVELPROPERTIES& all)
{
	validate_source_speaker_id(all.lSpeakerID);
	validate_source_speaker_level(all.lLevel);
}

void EaxxSource::defer_source_direct(
	long lDirect)
{
	eax_d_.source.lDirect = lDirect;
	source_dirty_filter_flags_.lDirect = (eax_.source.lDirect != eax_d_.source.lDirect);
}

void EaxxSource::defer_source_direct_hf(
	long lDirectHF)
{
	eax_d_.source.lDirectHF = lDirectHF;
	source_dirty_filter_flags_.lDirectHF = (eax_.source.lDirectHF != eax_d_.source.lDirectHF);
}

void EaxxSource::defer_source_room(
	long lRoom)
{
	eax_d_.source.lRoom = lRoom;
	source_dirty_filter_flags_.lRoom = (eax_.source.lRoom != eax_d_.source.lRoom);
}

void EaxxSource::defer_source_room_hf(
	long lRoomHF)
{
	eax_d_.source.lRoomHF = lRoomHF;
	source_dirty_filter_flags_.lRoomHF = (eax_.source.lRoomHF != eax_d_.source.lRoomHF);
}

void EaxxSource::defer_source_obstruction(
	long lObstruction)
{
	eax_d_.source.lObstruction = lObstruction;
	source_dirty_filter_flags_.lObstruction = (eax_.source.lObstruction != eax_d_.source.lObstruction);
}

void EaxxSource::defer_source_obstruction_lf_ratio(
	float flObstructionLFRatio)
{
	eax_d_.source.flObstructionLFRatio = flObstructionLFRatio;
	source_dirty_filter_flags_.flObstructionLFRatio = (eax_.source.flObstructionLFRatio != eax_d_.source.flObstructionLFRatio);
}

void EaxxSource::defer_source_occlusion(
	long lOcclusion)
{
	eax_d_.source.lOcclusion = lOcclusion;
	source_dirty_filter_flags_.lOcclusion = (eax_.source.lOcclusion != eax_d_.source.lOcclusion);
}

void EaxxSource::defer_source_occlusion_lf_ratio(
	float flOcclusionLFRatio)
{
	eax_d_.source.flOcclusionLFRatio = flOcclusionLFRatio;
	source_dirty_filter_flags_.flOcclusionLFRatio = (eax_.source.flOcclusionLFRatio != eax_d_.source.flOcclusionLFRatio);
}

void EaxxSource::defer_source_occlusion_room_ratio(
	float flOcclusionRoomRatio)
{
	eax_d_.source.flOcclusionRoomRatio = flOcclusionRoomRatio;
	source_dirty_filter_flags_.flOcclusionRoomRatio = (eax_.source.flOcclusionRoomRatio != eax_d_.source.flOcclusionRoomRatio);
}

void EaxxSource::defer_source_occlusion_direct_ratio(
	float flOcclusionDirectRatio)
{
	eax_d_.source.flOcclusionDirectRatio = flOcclusionDirectRatio;
	source_dirty_filter_flags_.flOcclusionDirectRatio = (eax_.source.flOcclusionDirectRatio != eax_d_.source.flOcclusionDirectRatio);
}

void EaxxSource::defer_source_exclusion(
	long lExclusion)
{
	eax_d_.source.lExclusion = lExclusion;
	source_dirty_filter_flags_.lExclusion = (eax_.source.lExclusion != eax_d_.source.lExclusion);
}

void EaxxSource::defer_source_exclusion_lf_ratio(
	float flExclusionLFRatio)
{
	eax_d_.source.flExclusionLFRatio = flExclusionLFRatio;
	source_dirty_filter_flags_.flExclusionLFRatio = (eax_.source.flExclusionLFRatio != eax_d_.source.flExclusionLFRatio);
}

void EaxxSource::defer_source_outside_volume_hf(
	long lOutsideVolumeHF)
{
	eax_d_.source.lOutsideVolumeHF = lOutsideVolumeHF;
	source_dirty_misc_flags_.lOutsideVolumeHF = (eax_.source.lOutsideVolumeHF != eax_d_.source.lOutsideVolumeHF);
}

void EaxxSource::defer_source_doppler_factor(
	float flDopplerFactor)
{
	eax_d_.source.flDopplerFactor = flDopplerFactor;
	source_dirty_misc_flags_.flDopplerFactor = (eax_.source.flDopplerFactor != eax_d_.source.flDopplerFactor);
}

void EaxxSource::defer_source_rolloff_factor(
	float flRolloffFactor)
{
	eax_d_.source.flRolloffFactor = flRolloffFactor;
	source_dirty_misc_flags_.flRolloffFactor = (eax_.source.flRolloffFactor != eax_d_.source.flRolloffFactor);
}

void EaxxSource::defer_source_room_rolloff_factor(
	float flRoomRolloffFactor)
{
	eax_d_.source.flRoomRolloffFactor = flRoomRolloffFactor;
	source_dirty_misc_flags_.flRoomRolloffFactor = (eax_.source.flRoomRolloffFactor != eax_d_.source.flRoomRolloffFactor);
}

void EaxxSource::defer_source_air_absorption_factor(
	float flAirAbsorptionFactor)
{
	eax_d_.source.flAirAbsorptionFactor = flAirAbsorptionFactor;
	source_dirty_misc_flags_.flAirAbsorptionFactor = (eax_.source.flAirAbsorptionFactor != eax_d_.source.flAirAbsorptionFactor);
}

void EaxxSource::defer_source_flags(
	unsigned long ulFlags)
{
	eax_d_.source.ulFlags = ulFlags;
	source_dirty_misc_flags_.ulFlags = (eax_.source.ulFlags != eax_d_.source.ulFlags);
}

void EaxxSource::defer_source_macro_fx_factor(
	float flMacroFXFactor)
{
	eax_d_.source.flMacroFXFactor = flMacroFXFactor;
	source_dirty_misc_flags_.flMacroFXFactor = (eax_.source.flMacroFXFactor != eax_d_.source.flMacroFXFactor);
}

void EaxxSource::defer_source_2d_all(
	const ::EAXSOURCE2DPROPERTIES& all)
{
	defer_source_direct(all.lDirect);
	defer_source_direct_hf(all.lDirectHF);
	defer_source_room(all.lRoom);
	defer_source_room_hf(all.lRoomHF);
	defer_source_flags(all.ulFlags);
}

void EaxxSource::defer_source_obstruction_all(
	const ::EAXOBSTRUCTIONPROPERTIES& all)
{
	defer_source_obstruction(all.lObstruction);
	defer_source_obstruction_lf_ratio(all.flObstructionLFRatio);
}

void EaxxSource::defer_source_exclusion_all(
	const ::EAXEXCLUSIONPROPERTIES& all)
{
	defer_source_exclusion(all.lExclusion);
	defer_source_exclusion_lf_ratio(all.flExclusionLFRatio);
}

void EaxxSource::defer_source_occlusion_all(
	const ::EAXOCCLUSIONPROPERTIES& all)
{
	defer_source_occlusion(all.lOcclusion);
	defer_source_occlusion_lf_ratio(all.flOcclusionLFRatio);
	defer_source_occlusion_room_ratio(all.flOcclusionRoomRatio);
	defer_source_occlusion_direct_ratio(all.flOcclusionDirectRatio);
}

void EaxxSource::defer_source_all(
	const ::EAX20BUFFERPROPERTIES& all)
{
	defer_source_direct(all.lDirect);
	defer_source_direct_hf(all.lDirectHF);
	defer_source_room(all.lRoom);
	defer_source_room_hf(all.lRoomHF);
	defer_source_obstruction(all.lObstruction);
	defer_source_obstruction_lf_ratio(all.flObstructionLFRatio);
	defer_source_occlusion(all.lOcclusion);
	defer_source_occlusion_lf_ratio(all.flOcclusionLFRatio);
	defer_source_occlusion_room_ratio(all.flOcclusionRoomRatio);
	defer_source_outside_volume_hf(all.lOutsideVolumeHF);
	defer_source_room_rolloff_factor(all.flRoomRolloffFactor);
	defer_source_air_absorption_factor(all.flAirAbsorptionFactor);
	defer_source_flags(all.dwFlags);
}

void EaxxSource::defer_source_all(
	const ::EAX30SOURCEPROPERTIES& all)
{
	defer_source_direct(all.lDirect);
	defer_source_direct_hf(all.lDirectHF);
	defer_source_room(all.lRoom);
	defer_source_room_hf(all.lRoomHF);
	defer_source_obstruction(all.lObstruction);
	defer_source_obstruction_lf_ratio(all.flObstructionLFRatio);
	defer_source_occlusion(all.lOcclusion);
	defer_source_occlusion_lf_ratio(all.flOcclusionLFRatio);
	defer_source_occlusion_room_ratio(all.flOcclusionRoomRatio);
	defer_source_occlusion_direct_ratio(all.flOcclusionDirectRatio);
	defer_source_exclusion(all.lExclusion);
	defer_source_exclusion_lf_ratio(all.flExclusionLFRatio);
	defer_source_outside_volume_hf(all.lOutsideVolumeHF);
	defer_source_doppler_factor(all.flDopplerFactor);
	defer_source_rolloff_factor(all.flRolloffFactor);
	defer_source_room_rolloff_factor(all.flRoomRolloffFactor);
	defer_source_air_absorption_factor(all.flAirAbsorptionFactor);
	defer_source_flags(all.ulFlags);
}

void EaxxSource::defer_source_all(
	const ::EAX50SOURCEPROPERTIES& all)
{
	defer_source_all(static_cast<const ::EAX30SOURCEPROPERTIES&>(all));
	defer_source_macro_fx_factor(all.flMacroFXFactor);
}

void EaxxSource::defer_source_speaker_level_all(
	const ::EAXSPEAKERLEVELPROPERTIES& all)
{
	const auto speaker_index = static_cast<std::size_t>(all.lSpeakerID - 1);
	auto& speaker_level_d = eax_d_.speaker_levels[speaker_index];
	const auto& speaker_level = eax_.speaker_levels[speaker_index];
	source_dirty_misc_flags_.speaker_levels |= (speaker_level != speaker_level_d);
}

void EaxxSource::defer_source_direct(
	const EaxxEaxCall& eax_call)
{
	const auto direct =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::lDirect)>();

	validate_source_direct(direct);
	defer_source_direct(direct);
}

void EaxxSource::defer_source_direct_hf(
	const EaxxEaxCall& eax_call)
{
	const auto direct_hf =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::lDirectHF)>();

	validate_source_direct_hf(direct_hf);
	defer_source_direct_hf(direct_hf);
}

void EaxxSource::defer_source_room(
	const EaxxEaxCall& eax_call)
{
	const auto room =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::lRoom)>();

	validate_source_room(room);
	defer_source_room(room);
}

void EaxxSource::defer_source_room_hf(
	const EaxxEaxCall& eax_call)
{
	const auto room_hf =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::lRoomHF)>();

	validate_source_room_hf(room_hf);
	defer_source_room_hf(room_hf);
}

void EaxxSource::defer_source_obstruction(
	const EaxxEaxCall& eax_call)
{
	const auto obstruction =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::lObstruction)>();

	validate_source_obstruction(obstruction);
	defer_source_obstruction(obstruction);
}

void EaxxSource::defer_source_obstruction_lf_ratio(
	const EaxxEaxCall& eax_call)
{
	const auto obstruction_lf_ratio =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::flObstructionLFRatio)>();

	validate_source_obstruction_lf_ratio(obstruction_lf_ratio);
	defer_source_obstruction_lf_ratio(obstruction_lf_ratio);
}

void EaxxSource::defer_source_occlusion(
	const EaxxEaxCall& eax_call)
{
	const auto occlusion =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::lOcclusion)>();

	validate_source_occlusion(occlusion);
	defer_source_occlusion(occlusion);
}

void EaxxSource::defer_source_occlusion_lf_ratio(
	const EaxxEaxCall& eax_call)
{
	const auto occlusion_lf_ratio =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::flOcclusionLFRatio)>();

	validate_source_occlusion_lf_ratio(occlusion_lf_ratio);
	defer_source_occlusion_lf_ratio(occlusion_lf_ratio);
}

void EaxxSource::defer_source_occlusion_room_ratio(
	const EaxxEaxCall& eax_call)
{
	const auto occlusion_room_ratio =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::flOcclusionRoomRatio)>();

	validate_source_occlusion_room_ratio(occlusion_room_ratio);
	defer_source_occlusion_room_ratio(occlusion_room_ratio);
}

void EaxxSource::defer_source_occlusion_direct_ratio(
	const EaxxEaxCall& eax_call)
{
	const auto occlusion_direct_ratio =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::flOcclusionDirectRatio)>();

	validate_source_occlusion_direct_ratio(occlusion_direct_ratio);
	defer_source_occlusion_direct_ratio(occlusion_direct_ratio);
}

void EaxxSource::defer_source_exclusion(
	const EaxxEaxCall& eax_call)
{
	const auto exclusion =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::lExclusion)>();

	validate_source_exclusion(exclusion);
	defer_source_exclusion(exclusion);
}

void EaxxSource::defer_source_exclusion_lf_ratio(
	const EaxxEaxCall& eax_call)
{
	const auto exclusion_lf_ratio =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::flExclusionLFRatio)>();

	validate_source_exclusion_lf_ratio(exclusion_lf_ratio);
	defer_source_exclusion_lf_ratio(exclusion_lf_ratio);
}

void EaxxSource::defer_source_outside_volume_hf(
	const EaxxEaxCall& eax_call)
{
	const auto outside_volume_hf =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::lOutsideVolumeHF)>();

	validate_source_outside_volume_hf(outside_volume_hf);
	defer_source_outside_volume_hf(outside_volume_hf);
}

void EaxxSource::defer_source_doppler_factor(
	const EaxxEaxCall& eax_call)
{
	const auto doppler_factor =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::flDopplerFactor)>();

	validate_source_doppler_factor(doppler_factor);
	defer_source_doppler_factor(doppler_factor);
}

void EaxxSource::defer_source_rolloff_factor(
	const EaxxEaxCall& eax_call)
{
	const auto rolloff_factor =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::flRolloffFactor)>();

	validate_source_rolloff_factor(rolloff_factor);
	defer_source_rolloff_factor(rolloff_factor);
}

void EaxxSource::defer_source_room_rolloff_factor(
	const EaxxEaxCall& eax_call)
{
	const auto room_rolloff_factor =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::flRoomRolloffFactor)>();

	validate_source_room_rolloff_factor(room_rolloff_factor);
	defer_source_room_rolloff_factor(room_rolloff_factor);
}

void EaxxSource::defer_source_air_absorption_factor(
	const EaxxEaxCall& eax_call)
{
	const auto air_absorption_factor =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::flAirAbsorptionFactor)>();

	validate_source_air_absorption_factor(air_absorption_factor);
	defer_source_air_absorption_factor(air_absorption_factor);
}

void EaxxSource::defer_source_flags(
	const EaxxEaxCall& eax_call)
{
	const auto flags =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX30SOURCEPROPERTIES::ulFlags)>();

	validate_source_flags(flags, eax_call.get_version());
	defer_source_flags(flags);
}

void EaxxSource::defer_source_macro_fx_factor(
	const EaxxEaxCall& eax_call)
{
	const auto macro_fx_factor =
		eax_call.get_value<EaxxSourceException, const decltype(::EAX50SOURCEPROPERTIES::flMacroFXFactor)>();

	validate_source_macro_fx_factor(macro_fx_factor);
	defer_source_macro_fx_factor(macro_fx_factor);
}

void EaxxSource::defer_source_2d_all(
	const EaxxEaxCall& eax_call)
{
	const auto all = eax_call.get_value<EaxxSourceException, const ::EAXSOURCE2DPROPERTIES>();

	validate_source_2d_all(all, eax_call.get_version());
	defer_source_2d_all(all);
}

void EaxxSource::defer_source_obstruction_all(
	const EaxxEaxCall& eax_call)
{
	const auto all = eax_call.get_value<EaxxSourceException, const ::EAXOBSTRUCTIONPROPERTIES>();

	validate_source_obstruction_all(all);
	defer_source_obstruction_all(all);
}

void EaxxSource::defer_source_exclusion_all(
	const EaxxEaxCall& eax_call)
{
	const auto all = eax_call.get_value<EaxxSourceException, const ::EAXEXCLUSIONPROPERTIES>();

	validate_source_exclusion_all(all);
	defer_source_exclusion_all(all);
}

void EaxxSource::defer_source_occlusion_all(
	const EaxxEaxCall& eax_call)
{
	const auto all = eax_call.get_value<EaxxSourceException, const ::EAXOCCLUSIONPROPERTIES>();

	validate_source_occlusion_all(all);
	defer_source_occlusion_all(all);
}

void EaxxSource::defer_source_all(
	const EaxxEaxCall& eax_call)
{
	const auto eax_version = eax_call.get_version();

	if (eax_version == 2)
	{
		const auto all = eax_call.get_value<EaxxSourceException, const ::EAX20BUFFERPROPERTIES>();

		validate_source_all(all, eax_version);
		defer_source_all(all);
	}
	else if (eax_version < 5)
	{
		const auto all = eax_call.get_value<EaxxSourceException, const ::EAX30SOURCEPROPERTIES>();

		validate_source_all(all, eax_version);
		defer_source_all(all);
	}
	else
	{
		const auto all = eax_call.get_value<EaxxSourceException, const ::EAX50SOURCEPROPERTIES>();

		validate_source_all(all, eax_version);
		defer_source_all(all);
	}
}

void EaxxSource::defer_source_speaker_level_all(
	const EaxxEaxCall& eax_call)
{
	const auto speaker_level_properties = eax_call.get_value<EaxxSourceException, const ::EAXSPEAKERLEVELPROPERTIES>();

	validate_source_speaker_level_all(speaker_level_properties);
	defer_source_speaker_level_all(speaker_level_properties);
}

void EaxxSource::set_outside_volume_hf()
{
	const auto efx_gain_hf = clamp(
		level_mb_to_gain(eax_.source.lOutsideVolumeHF),
		AL_MIN_CONE_OUTER_GAINHF,
		AL_MAX_CONE_OUTER_GAINHF
	);

	::alSourcef(al_.source, AL_CONE_OUTER_GAINHF, efx_gain_hf);
}

void EaxxSource::set_doppler_factor()
{
	::alSourcef(al_.source, AL_DOPPLER_FACTOR, eax_.source.flDopplerFactor);
}

void EaxxSource::set_rolloff_factor()
{
	::alSourcef(al_.source, AL_ROLLOFF_FACTOR, eax_.source.flRolloffFactor);
}

void EaxxSource::set_room_rolloff_factor()
{
	::alSourcef(al_.source, AL_ROOM_ROLLOFF_FACTOR, eax_.source.flRoomRolloffFactor);
}

void EaxxSource::set_air_absorption_factor()
{
	const auto air_absorption_factor = context_shared_->air_absorption_factor * eax_.source.flAirAbsorptionFactor;
	::alSourcef(al_.source, AL_AIR_ABSORPTION_FACTOR, air_absorption_factor);
}

void EaxxSource::set_direct_hf_auto_flag()
{
	const auto is_enable = (eax_.source.ulFlags & ::EAXSOURCEFLAGS_DIRECTHFAUTO) != 0;
	::alSourcei(al_.source, AL_DIRECT_FILTER_GAINHF_AUTO, is_enable);
}

void EaxxSource::set_room_auto_flag()
{
	const auto is_enable = (eax_.source.ulFlags & ::EAXSOURCEFLAGS_ROOMAUTO) != 0;
	::alSourcei(al_.source, AL_AUXILIARY_SEND_FILTER_GAIN_AUTO, is_enable);
}

void EaxxSource::set_room_hf_auto_flag()
{
	const auto is_enable = (eax_.source.ulFlags & ::EAXSOURCEFLAGS_ROOMHFAUTO) != 0;
	::alSourcei(al_.source, AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO, is_enable);
}

void EaxxSource::set_flags()
{
	set_direct_hf_auto_flag();
	set_room_auto_flag();
	set_room_hf_auto_flag();
	set_speaker_levels();
}

void EaxxSource::set_macro_fx_factor()
{
	// TODO
}

void EaxxSource::set_speaker_levels()
{
	// TODO
}

void EaxxSource::apply_deferred()
{
	if (
		!are_active_fx_slots_dirty_ &&
		sends_dirty_flags_ == EaxxSourceSendsDirtyFlags{} &&
		source_dirty_filter_flags_ == EaxxSourceSourceDirtyFilterFlags{} &&
		source_dirty_misc_flags_ == EaxxSourceSourceDirtyMiscFlags{})
	{
		return;
	}

	eax_ = eax_d_;

	if (are_active_fx_slots_dirty_)
	{
		are_active_fx_slots_dirty_ = false;
		set_fx_slots();
		update_filters_internal();
	}
	else if (has_active_fx_slots_)
	{
		if (source_dirty_filter_flags_ != EaxxSourceSourceDirtyFilterFlags{})
		{
			update_filters_internal();
		}
		else if (sends_dirty_flags_ != EaxxSourceSendsDirtyFlags{})
		{
			for (auto i = std::size_t{}; i < ::EAX_MAX_FXSLOTS; ++i)
			{
				if (active_fx_slots_[i])
				{
					if (sends_dirty_flags_.sends[i] != EaxxSourceSendDirtyFlags{})
					{
						update_filters_internal();
						break;
					}
				}
			}
		}
	}

	if (source_dirty_misc_flags_ != EaxxSourceSourceDirtyMiscFlags{})
	{
		if (source_dirty_misc_flags_.lOutsideVolumeHF)
		{
			set_outside_volume_hf();
		}

		if (source_dirty_misc_flags_.flDopplerFactor)
		{
			set_doppler_factor();
		}

		if (source_dirty_misc_flags_.flRolloffFactor)
		{
			set_rolloff_factor();
		}

		if (source_dirty_misc_flags_.flRoomRolloffFactor)
		{
			set_room_rolloff_factor();
		}

		if (source_dirty_misc_flags_.flAirAbsorptionFactor)
		{
			set_air_absorption_factor();
		}

		if (source_dirty_misc_flags_.ulFlags)
		{
			set_flags();
		}

		if (source_dirty_misc_flags_.flMacroFXFactor)
		{
			set_macro_fx_factor();
		}

		source_dirty_misc_flags_ = EaxxSourceSourceDirtyMiscFlags{};
	}

	sends_dirty_flags_ = EaxxSourceSendsDirtyFlags{};
	source_dirty_filter_flags_ = EaxxSourceSourceDirtyFilterFlags{};
}

void EaxxSource::set(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXSOURCE_NONE:
			break;

		case ::EAXSOURCE_ALLPARAMETERS:
			defer_source_all(eax_call);
			break;

		case ::EAXSOURCE_OBSTRUCTIONPARAMETERS:
			defer_source_obstruction_all(eax_call);
			break;

		case ::EAXSOURCE_OCCLUSIONPARAMETERS:
			defer_source_occlusion_all(eax_call);
			break;

		case ::EAXSOURCE_EXCLUSIONPARAMETERS:
			defer_source_exclusion_all(eax_call);
			break;

		case ::EAXSOURCE_DIRECT:
			defer_source_direct(eax_call);
			break;

		case ::EAXSOURCE_DIRECTHF:
			defer_source_direct_hf(eax_call);
			break;

		case ::EAXSOURCE_ROOM:
			defer_source_room(eax_call);
			break;

		case ::EAXSOURCE_ROOMHF:
			defer_source_room_hf(eax_call);
			break;

		case ::EAXSOURCE_OBSTRUCTION:
			defer_source_obstruction(eax_call);
			break;

		case ::EAXSOURCE_OBSTRUCTIONLFRATIO:
			defer_source_obstruction_lf_ratio(eax_call);
			break;

		case ::EAXSOURCE_OCCLUSION:
			defer_source_occlusion(eax_call);
			break;

		case ::EAXSOURCE_OCCLUSIONLFRATIO:
			defer_source_occlusion_lf_ratio(eax_call);
			break;

		case ::EAXSOURCE_OCCLUSIONROOMRATIO:
			defer_source_occlusion_room_ratio(eax_call);
			break;

		case ::EAXSOURCE_OCCLUSIONDIRECTRATIO:
			defer_source_occlusion_direct_ratio(eax_call);
			break;

		case ::EAXSOURCE_EXCLUSION:
			defer_source_exclusion(eax_call);
			break;

		case ::EAXSOURCE_EXCLUSIONLFRATIO:
			defer_source_exclusion_lf_ratio(eax_call);
			break;

		case ::EAXSOURCE_OUTSIDEVOLUMEHF:
			defer_source_outside_volume_hf(eax_call);
			break;

		case ::EAXSOURCE_DOPPLERFACTOR:
			defer_source_doppler_factor(eax_call);
			break;

		case ::EAXSOURCE_ROLLOFFFACTOR:
			defer_source_rolloff_factor(eax_call);
			break;

		case ::EAXSOURCE_ROOMROLLOFFFACTOR:
			defer_source_room_rolloff_factor(eax_call);
			break;

		case ::EAXSOURCE_AIRABSORPTIONFACTOR:
			defer_source_air_absorption_factor(eax_call);
			break;

		case ::EAXSOURCE_FLAGS:
			defer_source_flags(eax_call);
			break;

		case ::EAXSOURCE_SENDPARAMETERS:
			defer_send(eax_call);
			break;

		case ::EAXSOURCE_ALLSENDPARAMETERS:
			defer_send_all(eax_call);
			break;

		case ::EAXSOURCE_OCCLUSIONSENDPARAMETERS:
			defer_send_occlusion_all(eax_call);
			break;

		case ::EAXSOURCE_EXCLUSIONSENDPARAMETERS:
			defer_send_exclusion_all(eax_call);
			break;

		case ::EAXSOURCE_ACTIVEFXSLOTID:
			defer_active_fx_slots(eax_call);
			break;

		case ::EAXSOURCE_MACROFXFACTOR:
			defer_source_macro_fx_factor(eax_call);
			break;

		case ::EAXSOURCE_SPEAKERLEVELS:
			defer_source_speaker_level_all(eax_call);
			break;

		case ::EAXSOURCE_ALL2DPARAMETERS:
			defer_source_2d_all(eax_call);
			break;

		default:
			fail("Unsupported property id.");
	}

	if (!eax_call.is_deferred())
	{
		apply_deferred();
	}
}

const ::GUID& EaxxSource::get_send_fx_slot_guid(
	int eax_version,
	int fx_slot_index)
{
	switch (eax_version)
	{
		case 4:
			switch (fx_slot_index)
			{
				case 0:
					return ::EAXPROPERTYID_EAX40_FXSlot0;

				case 1:
					return ::EAXPROPERTYID_EAX40_FXSlot1;

				case 2:
					return ::EAXPROPERTYID_EAX40_FXSlot2;

				case 3:
					return ::EAXPROPERTYID_EAX40_FXSlot3;

				default:
					fail("FX slot index out of range.");
			}

		case 5:
			switch (fx_slot_index)
			{
				case 0:
					return ::EAXPROPERTYID_EAX50_FXSlot0;

				case 1:
					return ::EAXPROPERTYID_EAX50_FXSlot1;

				case 2:
					return ::EAXPROPERTYID_EAX50_FXSlot2;

				case 3:
					return ::EAXPROPERTYID_EAX50_FXSlot3;

				default:
					fail("FX slot index out of range.");
			}

		default:
			fail("Unsupported EAX version.");
	}
}

void EaxxSource::copy_send(
	const ::EAXSOURCEALLSENDPROPERTIES& src_send,
	::EAXSOURCESENDPROPERTIES& dst_send)
{
	dst_send.lSend = src_send.lSend;
	dst_send.lSendHF = src_send.lSendHF;
}

void EaxxSource::copy_send(
	const ::EAXSOURCEALLSENDPROPERTIES& src_send,
	::EAXSOURCEALLSENDPROPERTIES& dst_send)
{
	dst_send = src_send;
}

void EaxxSource::copy_send(
	const ::EAXSOURCEALLSENDPROPERTIES& src_send,
	::EAXSOURCEOCCLUSIONSENDPROPERTIES& dst_send)
{
	dst_send.lOcclusion = src_send.lOcclusion;
	dst_send.flOcclusionLFRatio = src_send.flOcclusionLFRatio;
	dst_send.flOcclusionRoomRatio = src_send.flOcclusionRoomRatio;
	dst_send.flOcclusionDirectRatio = src_send.flOcclusionDirectRatio;
}

void EaxxSource::copy_send(
	const ::EAXSOURCEALLSENDPROPERTIES& src_send,
	::EAXSOURCEEXCLUSIONSENDPROPERTIES& dst_send)
{
	dst_send.lExclusion = src_send.lExclusion;
	dst_send.flExclusionLFRatio = src_send.flExclusionLFRatio;
}

void EaxxSource::api_get_source_all_2(
	const EaxxEaxCall& eax_call)
{
	auto eax_2_all = ::EAX20BUFFERPROPERTIES{};
	eax_2_all.lDirect = eax_.source.lDirect;
	eax_2_all.lDirectHF = eax_.source.lDirectHF;
	eax_2_all.lRoom = eax_.source.lRoom;
	eax_2_all.lRoomHF = eax_.source.lRoomHF;
	eax_2_all.flRoomRolloffFactor = eax_.source.flRoomRolloffFactor;
	eax_2_all.lObstruction = eax_.source.lObstruction;
	eax_2_all.flObstructionLFRatio = eax_.source.flObstructionLFRatio;
	eax_2_all.lOcclusion = eax_.source.lOcclusion;
	eax_2_all.flOcclusionLFRatio = eax_.source.flOcclusionLFRatio;
	eax_2_all.flOcclusionRoomRatio = eax_.source.flOcclusionRoomRatio;
	eax_2_all.lOutsideVolumeHF = eax_.source.lOutsideVolumeHF;
	eax_2_all.flAirAbsorptionFactor = eax_.source.flAirAbsorptionFactor;
	eax_2_all.dwFlags = eax_.source.ulFlags;

	eax_call.set_value<EaxxSourceException>(eax_2_all);
}

void EaxxSource::api_get_source_all_3(
	const EaxxEaxCall& eax_call)
{
	eax_call.set_value<EaxxSourceException>(static_cast<const ::EAX30SOURCEPROPERTIES&>(eax_.source));
}

void EaxxSource::api_get_source_all_5(
	const EaxxEaxCall& eax_call)
{
	eax_call.set_value<EaxxSourceException>(eax_.source);
}

void EaxxSource::api_get_source_all(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_version())
	{
		case 2:
			api_get_source_all_2(eax_call);
			break;

		case 3:
		case 4:
			api_get_source_all_3(eax_call);
			break;

		case 5:
			api_get_source_all_5(eax_call);
			break;

		default:
			fail("Unsupported EAX version.");
	}
}

void EaxxSource::api_get_source_all_obstruction(
	const EaxxEaxCall& eax_call)
{
	static_assert(
		offsetof(::EAXOBSTRUCTIONPROPERTIES, flObstructionLFRatio) - offsetof(::EAXOBSTRUCTIONPROPERTIES, lObstruction) ==
			offsetof(::EAX30SOURCEPROPERTIES, flObstructionLFRatio) - offsetof(::EAX30SOURCEPROPERTIES, lObstruction),
		"Type size."
	);

	const auto eax_obstruction_all = *reinterpret_cast<const ::EAXOBSTRUCTIONPROPERTIES*>(&eax_.source.lObstruction);

	eax_call.set_value<EaxxSourceException>(eax_obstruction_all);
}

void EaxxSource::api_get_source_all_occlusion(
	const EaxxEaxCall& eax_call)
{
	static_assert(
		offsetof(::EAXOCCLUSIONPROPERTIES, flOcclusionLFRatio) - offsetof(::EAXOCCLUSIONPROPERTIES, lOcclusion) ==
			offsetof(::EAX30SOURCEPROPERTIES, flOcclusionLFRatio) - offsetof(::EAX30SOURCEPROPERTIES, lOcclusion) &&

		offsetof(::EAXOCCLUSIONPROPERTIES, flOcclusionRoomRatio) - offsetof(::EAXOCCLUSIONPROPERTIES, lOcclusion) ==
			offsetof(::EAX30SOURCEPROPERTIES, flOcclusionRoomRatio) - offsetof(::EAX30SOURCEPROPERTIES, lOcclusion) &&

		offsetof(::EAXOCCLUSIONPROPERTIES, flOcclusionDirectRatio) - offsetof(::EAXOCCLUSIONPROPERTIES, lOcclusion) ==
			offsetof(::EAX30SOURCEPROPERTIES, flOcclusionDirectRatio) - offsetof(::EAX30SOURCEPROPERTIES, lOcclusion),

		"Type size."
	);

	const auto eax_occlusion_all = *reinterpret_cast<const ::EAXOCCLUSIONPROPERTIES*>(&eax_.source.lOcclusion);

	eax_call.set_value<EaxxSourceException>(eax_occlusion_all);
}

void EaxxSource::api_get_source_all_exclusion(
	const EaxxEaxCall& eax_call)
{
	static_assert(
		offsetof(::EAXEXCLUSIONPROPERTIES, flExclusionLFRatio) - offsetof(::EAXEXCLUSIONPROPERTIES, lExclusion) ==
			offsetof(::EAX30SOURCEPROPERTIES, flExclusionLFRatio) - offsetof(::EAX30SOURCEPROPERTIES, lExclusion),

		"Type size."
	);

	const auto eax_exclusion_all = *reinterpret_cast<const ::EAXEXCLUSIONPROPERTIES*>(&eax_.source.lExclusion);

	eax_call.set_value<EaxxSourceException>(eax_exclusion_all);
}

void EaxxSource::api_get_source_active_fx_slot_id(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_version())
	{
		case 4:
			{
				const auto& active_fx_slots = reinterpret_cast<const ::EAX40ACTIVEFXSLOTS&>(eax_.active_fx_slots);
				eax_call.set_value<EaxxSourceException>(active_fx_slots);
			}
			break;

		case 5:
			{
				const auto& active_fx_slots = reinterpret_cast<const ::EAX50ACTIVEFXSLOTS&>(eax_.active_fx_slots);
				eax_call.set_value<EaxxSourceException>(active_fx_slots);
			}
			break;

		default:
			fail("Unsupported EAX version.");
	}
}

void EaxxSource::api_get_source_all_2d(
	const EaxxEaxCall& eax_call)
{
	auto eax_2d_all = ::EAXSOURCE2DPROPERTIES{};
	eax_2d_all.lDirect = eax_.source.lDirect;
	eax_2d_all.lDirectHF = eax_.source.lDirectHF;
	eax_2d_all.lRoom = eax_.source.lRoom;
	eax_2d_all.lRoomHF = eax_.source.lRoomHF;
	eax_2d_all.ulFlags = eax_.source.ulFlags;

	eax_call.set_value<EaxxSourceException>(eax_2d_all);
}

void EaxxSource::api_get_source_speaker_level_all(
	const EaxxEaxCall& eax_call)
{
	auto& all = eax_call.get_value<EaxxSourceException, ::EAXSPEAKERLEVELPROPERTIES>();

	validate_source_speaker_id(all.lSpeakerID);
	const auto speaker_index = static_cast<std::size_t>(all.lSpeakerID - 1);
	all.lLevel = eax_.speaker_levels[speaker_index];
}

void EaxxSource::get(
	const EaxxEaxCall& eax_call)
{
	switch (eax_call.get_property_id())
	{
		case ::EAXSOURCE_NONE:
			break;

		case ::EAXSOURCE_ALLPARAMETERS:
			api_get_source_all(eax_call);
			break;

		case ::EAXSOURCE_OBSTRUCTIONPARAMETERS:
			api_get_source_all_obstruction(eax_call);
			break;

		case ::EAXSOURCE_OCCLUSIONPARAMETERS:
			api_get_source_all_occlusion(eax_call);
			break;

		case ::EAXSOURCE_EXCLUSIONPARAMETERS:
			api_get_source_all_exclusion(eax_call);
			break;

		case ::EAXSOURCE_DIRECT:
			eax_call.set_value<EaxxSourceException>(eax_.source.lDirect);
			break;

		case ::EAXSOURCE_DIRECTHF:
			eax_call.set_value<EaxxSourceException>(eax_.source.lDirectHF);
			break;

		case ::EAXSOURCE_ROOM:
			eax_call.set_value<EaxxSourceException>(eax_.source.lRoom);
			break;

		case ::EAXSOURCE_ROOMHF:
			eax_call.set_value<EaxxSourceException>(eax_.source.lRoomHF);
			break;

		case ::EAXSOURCE_OBSTRUCTION:
			eax_call.set_value<EaxxSourceException>(eax_.source.lObstruction);
			break;

		case ::EAXSOURCE_OBSTRUCTIONLFRATIO:
			eax_call.set_value<EaxxSourceException>(eax_.source.flObstructionLFRatio);
			break;

		case ::EAXSOURCE_OCCLUSION:
			eax_call.set_value<EaxxSourceException>(eax_.source.lOcclusion);
			break;

		case ::EAXSOURCE_OCCLUSIONLFRATIO:
			eax_call.set_value<EaxxSourceException>(eax_.source.flOcclusionLFRatio);
			break;

		case ::EAXSOURCE_OCCLUSIONROOMRATIO:
			eax_call.set_value<EaxxSourceException>(eax_.source.flOcclusionRoomRatio);
			break;

		case ::EAXSOURCE_OCCLUSIONDIRECTRATIO:
			eax_call.set_value<EaxxSourceException>(eax_.source.flOcclusionDirectRatio);
			break;

		case ::EAXSOURCE_EXCLUSION:
			eax_call.set_value<EaxxSourceException>(eax_.source.lExclusion);
			break;

		case ::EAXSOURCE_EXCLUSIONLFRATIO:
			eax_call.set_value<EaxxSourceException>(eax_.source.flExclusionLFRatio);
			break;

		case ::EAXSOURCE_OUTSIDEVOLUMEHF:
			eax_call.set_value<EaxxSourceException>(eax_.source.lOutsideVolumeHF);
			break;

		case ::EAXSOURCE_DOPPLERFACTOR:
			eax_call.set_value<EaxxSourceException>(eax_.source.flDopplerFactor);
			break;

		case ::EAXSOURCE_ROLLOFFFACTOR:
			eax_call.set_value<EaxxSourceException>(eax_.source.flRolloffFactor);
			break;

		case ::EAXSOURCE_ROOMROLLOFFFACTOR:
			eax_call.set_value<EaxxSourceException>(eax_.source.flRoomRolloffFactor);
			break;

		case ::EAXSOURCE_AIRABSORPTIONFACTOR:
			eax_call.set_value<EaxxSourceException>(eax_.source.flAirAbsorptionFactor);
			break;

		case ::EAXSOURCE_FLAGS:
			eax_call.set_value<EaxxSourceException>(eax_.source.ulFlags);
			break;

		case ::EAXSOURCE_SENDPARAMETERS:
			api_get_send_properties<EaxxSourceException, ::EAXSOURCESENDPROPERTIES>(eax_call);
			break;

		case ::EAXSOURCE_ALLSENDPARAMETERS:
			api_get_send_properties<EaxxSourceException, ::EAXSOURCEALLSENDPROPERTIES>(eax_call);
			break;

		case ::EAXSOURCE_OCCLUSIONSENDPARAMETERS:
			api_get_send_properties<EaxxSourceException, ::EAXSOURCEOCCLUSIONSENDPROPERTIES>(eax_call);
			break;

		case ::EAXSOURCE_EXCLUSIONSENDPARAMETERS:
			api_get_send_properties<EaxxSourceException, ::EAXSOURCEEXCLUSIONSENDPROPERTIES>(eax_call);
			break;

		case ::EAXSOURCE_ACTIVEFXSLOTID:
			api_get_source_active_fx_slot_id(eax_call);
			break;

		case ::EAXSOURCE_MACROFXFACTOR:
			eax_call.set_value<EaxxSourceException>(eax_.source.flMacroFXFactor);
			break;

		case ::EAXSOURCE_SPEAKERLEVELS:
			api_get_source_speaker_level_all(eax_call);
			break;

		case ::EAXSOURCE_ALL2DPARAMETERS:
			api_get_source_all_2d(eax_call);
			break;

		default:
			fail("Unsupported property id.");
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
