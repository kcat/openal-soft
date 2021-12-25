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


#ifndef EAX_EAXX_SOURCE_INCLUDED
#define EAX_EAXX_SOURCE_INCLUDED


#include <array>

#include "AL/al.h"

#include "eax_al_low_pass_param.h"

#include "eax_eaxx_context_shared.h"
#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_fx_slots.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

struct EaxxSourceInitParam
{
	::ALuint al_source{};
	::ALuint al_filter{};
	EaxxContextShared* context_shared{};
}; // EaxxSourceInitParam

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using EaxxSourceSourceDirtyFilterFlagsValue = unsigned int;

struct EaxxSourceSourceDirtyFilterFlags
{
	EaxxSourceSourceDirtyFilterFlagsValue lDirect : 1;
	EaxxSourceSourceDirtyFilterFlagsValue lDirectHF : 1;
	EaxxSourceSourceDirtyFilterFlagsValue lRoom : 1;
	EaxxSourceSourceDirtyFilterFlagsValue lRoomHF : 1;
	EaxxSourceSourceDirtyFilterFlagsValue lObstruction : 1;
	EaxxSourceSourceDirtyFilterFlagsValue flObstructionLFRatio : 1;
	EaxxSourceSourceDirtyFilterFlagsValue lOcclusion : 1;
	EaxxSourceSourceDirtyFilterFlagsValue flOcclusionLFRatio : 1;
	EaxxSourceSourceDirtyFilterFlagsValue flOcclusionRoomRatio : 1;
	EaxxSourceSourceDirtyFilterFlagsValue flOcclusionDirectRatio : 1;
	EaxxSourceSourceDirtyFilterFlagsValue lExclusion : 1;
	EaxxSourceSourceDirtyFilterFlagsValue flExclusionLFRatio : 1;
}; // EaxxSourceSourceDirtyFlags

static_assert(sizeof(EaxxSourceSourceDirtyFilterFlags) == sizeof(EaxxSourceSourceDirtyFilterFlagsValue), "Type size.");

bool operator==(
	const EaxxSourceSourceDirtyFilterFlags& lhs,
	const EaxxSourceSourceDirtyFilterFlags& rhs) noexcept;

bool operator!=(
	const EaxxSourceSourceDirtyFilterFlags& lhs,
	const EaxxSourceSourceDirtyFilterFlags& rhs) noexcept;


using EaxxSourceSourceDirtyMiscFlagsValue = unsigned int;

struct EaxxSourceSourceDirtyMiscFlags
{
	EaxxSourceSourceDirtyMiscFlagsValue lOutsideVolumeHF : 1;
	EaxxSourceSourceDirtyMiscFlagsValue flDopplerFactor : 1;
	EaxxSourceSourceDirtyMiscFlagsValue flRolloffFactor : 1;
	EaxxSourceSourceDirtyMiscFlagsValue flRoomRolloffFactor : 1;
	EaxxSourceSourceDirtyMiscFlagsValue flAirAbsorptionFactor : 1;
	EaxxSourceSourceDirtyMiscFlagsValue ulFlags : 1;
	EaxxSourceSourceDirtyMiscFlagsValue flMacroFXFactor : 1;
	EaxxSourceSourceDirtyMiscFlagsValue speaker_levels : 1;
}; // EaxxSourceSourceMiscDirtyFlags

static_assert(sizeof(EaxxSourceSourceDirtyMiscFlags) == sizeof(EaxxSourceSourceDirtyMiscFlagsValue), "Type size.");

bool operator==(
	const EaxxSourceSourceDirtyMiscFlags& lhs,
	const EaxxSourceSourceDirtyMiscFlags& rhs) noexcept;

bool operator!=(
	const EaxxSourceSourceDirtyMiscFlags& lhs,
	const EaxxSourceSourceDirtyMiscFlags& rhs) noexcept;


using EaxxSourceSendDirtyFlagsValue = unsigned char;

struct EaxxSourceSendDirtyFlags
{
	EaxxSourceSendDirtyFlagsValue lSend : 1;
	EaxxSourceSendDirtyFlagsValue lSendHF : 1;
	EaxxSourceSendDirtyFlagsValue lOcclusion : 1;
	EaxxSourceSendDirtyFlagsValue flOcclusionLFRatio : 1;
	EaxxSourceSendDirtyFlagsValue flOcclusionRoomRatio : 1;
	EaxxSourceSendDirtyFlagsValue flOcclusionDirectRatio : 1;
	EaxxSourceSendDirtyFlagsValue lExclusion : 1;
	EaxxSourceSendDirtyFlagsValue flExclusionLFRatio : 1;
}; // EaxxSourceSendDirtyFlags

static_assert(sizeof(EaxxSourceSendDirtyFlags) == sizeof(EaxxSourceSendDirtyFlagsValue), "Type size.");

bool operator==(
	const EaxxSourceSendDirtyFlags& lhs,
	const EaxxSourceSendDirtyFlags& rhs) noexcept;

bool operator!=(
	const EaxxSourceSendDirtyFlags& lhs,
	const EaxxSourceSendDirtyFlags& rhs) noexcept;


using EaxxSourceSendsDirtyFlagsValue = unsigned int;

struct EaxxSourceSendsDirtyFlags
{
	EaxxSourceSendDirtyFlags sends[::EAX_MAX_FXSLOTS];
}; // EaxxSourceSendsDirtyFlags

static_assert(sizeof(EaxxSourceSendsDirtyFlags) == sizeof(EaxxSourceSendsDirtyFlagsValue), "Type size.");

bool operator==(
	const EaxxSourceSendsDirtyFlags& lhs,
	const EaxxSourceSendsDirtyFlags& rhs) noexcept;

bool operator!=(
	const EaxxSourceSendsDirtyFlags& lhs,
	const EaxxSourceSendsDirtyFlags& rhs) noexcept;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxSource
{
public:
	EaxxSource(
		const EaxxSourceInitParam& param);


	void on_initialize_context(
		::ALuint al_filter);


	void dispatch(
		const EaxxEaxCall& eax_call);


	void update_filters();

	void update(
		EaxxContextSharedDirtyFlags dirty_flags);


private:
	static constexpr auto max_speakers = 9;


	using ActiveFxSlots = std::array<bool, ::EAX_MAX_FXSLOTS>;
	using SpeakerLevels = std::array<long, max_speakers>;


	struct Al
	{
		::ALuint source{};
		::ALuint filter{};
	}; // Al

	struct Eax
	{
		using Sends = std::array<::EAXSOURCEALLSENDPROPERTIES, ::EAX_MAX_FXSLOTS>;

		::EAX50ACTIVEFXSLOTS active_fx_slots{};
		::EAX50SOURCEPROPERTIES source{};
		Sends sends{};
		SpeakerLevels speaker_levels{};
	}; // Eax


	bool uses_primary_id_{};
	bool has_active_fx_slots_{};
	bool are_active_fx_slots_dirty_{};

	Al al_{};
	Eax eax_{};
	Eax eax_d_{};
	EaxxContextShared* context_shared_{};
	ActiveFxSlots active_fx_slots_{};

	EaxxSourceSendsDirtyFlags sends_dirty_flags_{};
	EaxxSourceSourceDirtyFilterFlags source_dirty_filter_flags_{};
	EaxxSourceSourceDirtyMiscFlags source_dirty_misc_flags_{};


	[[noreturn]]
	static void fail(
		const char* message);


	static void validate_init_param(
		const EaxxSourceInitParam& param);

	void copy_init_param(
		const EaxxSourceInitParam& param);


	void set_eax_source_defaults();

	void set_eax_active_fx_slots_defaults();

	void set_eax_send_defaults(
		::EAXSOURCEALLSENDPROPERTIES& eax_send);

	void set_eax_sends_defaults();

	void set_eax_speaker_levels_defaults();

	void set_eax_defaults();


	static float calculate_dst_occlusion_mb(
		long src_occlusion_mb,
		float path_ratio,
		float lf_ratio) noexcept;

	AlLowPassParam make_direct_filter_param() const noexcept;

	AlLowPassParam make_room_filter_param(
		const EaxxFxSlot& fx_slot,
		const ::EAXSOURCEALLSENDPROPERTIES& send) const noexcept;

	void set_al_filter_parameters(
		const AlLowPassParam& al_low_pass_param) const noexcept;

	void set_fx_slots();

	void initialize_fx_slots();

	void initialize(
		const EaxxSourceInitParam& param);


	void update_direct_filter_internal();

	void update_room_filters_internal();

	void update_filters_internal();

	void update_primary_fx_slot_id();


	void defer_active_fx_slots(
		const EaxxEaxCall& eax_call);


	// ----------------------------------------------------------------------
	// Common

	static const char* get_exclusion_name() noexcept;

	static const char* get_exclusion_lf_ratio_name() noexcept;


	static const char* get_occlusion_name() noexcept;

	static const char* get_occlusion_lf_ratio_name() noexcept;

	static const char* get_occlusion_direct_ratio_name() noexcept;

	static const char* get_occlusion_room_ratio_name() noexcept;

	// Common
	// ----------------------------------------------------------------------


	// ----------------------------------------------------------------------
	// Send

	static void validate_send_receiving_fx_slot_guid(
		const ::GUID& guidReceivingFXSlotID);

	static void validate_send_send(
		long lSend);

	static void validate_send_send_hf(
		long lSendHF);

	static void validate_send_occlusion(
		long lOcclusion);

	static void validate_send_occlusion_lf_ratio(
		float flOcclusionLFRatio);

	static void validate_send_occlusion_room_ratio(
		float flOcclusionRoomRatio);

	static void validate_send_occlusion_direct_ratio(
		float flOcclusionDirectRatio);

	static void validate_send_exclusion(
		long lExclusion);

	static void validate_send_exclusion_lf_ratio(
		float flExclusionLFRatio);

	static void validate_send(
		const ::EAXSOURCESENDPROPERTIES& all);

	static void validate_send_exclusion_all(
		const ::EAXSOURCEEXCLUSIONSENDPROPERTIES& all);

	static void validate_send_occlusion_all(
		const ::EAXSOURCEOCCLUSIONSENDPROPERTIES& all);

	static void validate_send_all(
		const ::EAXSOURCEALLSENDPROPERTIES& all);


	static int get_send_index(
		const ::GUID& send_guid);


	void defer_send_send(
		long lSend,
		int index);

	void defer_send_send_hf(
		long lSendHF,
		int index);

	void defer_send_occlusion(
		long lOcclusion,
		int index);

	void defer_send_occlusion_lf_ratio(
		float flOcclusionLFRatio,
		int index);

	void defer_send_occlusion_room_ratio(
		float flOcclusionRoomRatio,
		int index);

	void defer_send_occlusion_direct_ratio(
		float flOcclusionDirectRatio,
		int index);

	void defer_send_exclusion(
		long lExclusion,
		int index);

	void defer_send_exclusion_lf_ratio(
		float flExclusionLFRatio,
		int index);

	void defer_send(
		const ::EAXSOURCESENDPROPERTIES& all,
		int index);

	void defer_send_exclusion_all(
		const ::EAXSOURCEEXCLUSIONSENDPROPERTIES& all,
		int index);

	void defer_send_occlusion_all(
		const ::EAXSOURCEOCCLUSIONSENDPROPERTIES& all,
		int index);

	void defer_send_all(
		const ::EAXSOURCEALLSENDPROPERTIES& all,
		int index);


	void defer_send(
		const EaxxEaxCall& eax_call);

	void defer_send_exclusion_all(
		const EaxxEaxCall& eax_call);

	void defer_send_occlusion_all(
		const EaxxEaxCall& eax_call);

	void defer_send_all(
		const EaxxEaxCall& eax_call);

	// Send
	// ----------------------------------------------------------------------


	// ----------------------------------------------------------------------
	// Source

	static void validate_source_direct(
		long direct);

	static void validate_source_direct_hf(
		long direct_hf);

	static void validate_source_room(
		long room);

	static void validate_source_room_hf(
		long room_hf);

	static void validate_source_obstruction(
		long obstruction);

	static void validate_source_obstruction_lf_ratio(
		float obstruction_lf_ratio);

	static void validate_source_occlusion(
		long occlusion);

	static void validate_source_occlusion_lf_ratio(
		float occlusion_lf_ratio);

	static void validate_source_occlusion_room_ratio(
		float occlusion_room_ratio);

	static void validate_source_occlusion_direct_ratio(
		float occlusion_direct_ratio);

	static void validate_source_exclusion(
		long exclusion);

	static void validate_source_exclusion_lf_ratio(
		float exclusion_lf_ratio);

	static void validate_source_outside_volume_hf(
		long outside_volume_hf);

	static void validate_source_doppler_factor(
		float doppler_factor);

	static void validate_source_rolloff_factor(
		float rolloff_factor);

	static void validate_source_room_rolloff_factor(
		float room_rolloff_factor);

	static void validate_source_air_absorption_factor(
		float air_absorption_factor);

	static void validate_source_flags(
		unsigned long flags,
		int eax_version);

	static void validate_source_macro_fx_factor(
		float macro_fx_factor);

	static void validate_source_2d_all(
		const ::EAXSOURCE2DPROPERTIES& all,
		int eax_version);

	static void validate_source_obstruction_all(
		const ::EAXOBSTRUCTIONPROPERTIES& all);

	static void validate_source_exclusion_all(
		const ::EAXEXCLUSIONPROPERTIES& all);

	static void validate_source_occlusion_all(
		const ::EAXOCCLUSIONPROPERTIES& all);

	static void validate_source_all(
		const ::EAX20BUFFERPROPERTIES& all,
		int eax_version);

	static void validate_source_all(
		const ::EAX30SOURCEPROPERTIES& all,
		int eax_version);

	static void validate_source_all(
		const ::EAX50SOURCEPROPERTIES& all,
		int eax_version);

	static void validate_source_speaker_id(
		long speaker_id);

	static void validate_source_speaker_level(
		long speaker_level);

	static void validate_source_speaker_level_all(
		const ::EAXSPEAKERLEVELPROPERTIES& all);


	void defer_source_direct(
		long lDirect);

	void defer_source_direct_hf(
		long lDirectHF);

	void defer_source_room(
		long lRoom);

	void defer_source_room_hf(
		long lRoomHF);

	void defer_source_obstruction(
		long lObstruction);

	void defer_source_obstruction_lf_ratio(
		float flObstructionLFRatio);

	void defer_source_occlusion(
		long lOcclusion);

	void defer_source_occlusion_lf_ratio(
		float flOcclusionLFRatio);

	void defer_source_occlusion_room_ratio(
		float flOcclusionRoomRatio);

	void defer_source_occlusion_direct_ratio(
		float flOcclusionDirectRatio);

	void defer_source_exclusion(
		long lExclusion);

	void defer_source_exclusion_lf_ratio(
		float flExclusionLFRatio);

	void defer_source_outside_volume_hf(
		long lOutsideVolumeHF);

	void defer_source_doppler_factor(
		float flDopplerFactor);

	void defer_source_rolloff_factor(
		float flRolloffFactor);

	void defer_source_room_rolloff_factor(
		float flRoomRolloffFactor);

	void defer_source_air_absorption_factor(
		float flAirAbsorptionFactor);

	void defer_source_flags(
		unsigned long ulFlags);

	void defer_source_macro_fx_factor(
		float flMacroFXFactor);

	void defer_source_2d_all(
		const ::EAXSOURCE2DPROPERTIES& all);

	void defer_source_obstruction_all(
		const ::EAXOBSTRUCTIONPROPERTIES& all);

	void defer_source_exclusion_all(
		const ::EAXEXCLUSIONPROPERTIES& all);

	void defer_source_occlusion_all(
		const ::EAXOCCLUSIONPROPERTIES& all);

	void defer_source_all(
		const ::EAX20BUFFERPROPERTIES& all);

	void defer_source_all(
		const ::EAX30SOURCEPROPERTIES& all);

	void defer_source_all(
		const ::EAX50SOURCEPROPERTIES& all);

	void defer_source_speaker_level_all(
		const ::EAXSPEAKERLEVELPROPERTIES& all);


	void defer_source_direct(
		const EaxxEaxCall& eax_call);

	void defer_source_direct_hf(
		const EaxxEaxCall& eax_call);

	void defer_source_room(
		const EaxxEaxCall& eax_call);

	void defer_source_room_hf(
		const EaxxEaxCall& eax_call);

	void defer_source_obstruction(
		const EaxxEaxCall& eax_call);

	void defer_source_obstruction_lf_ratio(
		const EaxxEaxCall& eax_call);

	void defer_source_occlusion(
		const EaxxEaxCall& eax_call);

	void defer_source_occlusion_lf_ratio(
		const EaxxEaxCall& eax_call);

	void defer_source_occlusion_room_ratio(
		const EaxxEaxCall& eax_call);

	void defer_source_occlusion_direct_ratio(
		const EaxxEaxCall& eax_call);

	void defer_source_exclusion(
		const EaxxEaxCall& eax_call);

	void defer_source_exclusion_lf_ratio(
		const EaxxEaxCall& eax_call);

	void defer_source_outside_volume_hf(
		const EaxxEaxCall& eax_call);

	void defer_source_doppler_factor(
		const EaxxEaxCall& eax_call);

	void defer_source_rolloff_factor(
		const EaxxEaxCall& eax_call);

	void defer_source_room_rolloff_factor(
		const EaxxEaxCall& eax_call);

	void defer_source_air_absorption_factor(
		const EaxxEaxCall& eax_call);

	void defer_source_flags(
		const EaxxEaxCall& eax_call);

	void defer_source_macro_fx_factor(
		const EaxxEaxCall& eax_call);

	void defer_source_2d_all(
		const EaxxEaxCall& eax_call);

	void defer_source_obstruction_all(
		const EaxxEaxCall& eax_call);

	void defer_source_exclusion_all(
		const EaxxEaxCall& eax_call);

	void defer_source_occlusion_all(
		const EaxxEaxCall& eax_call);

	void defer_source_all(
		const EaxxEaxCall& eax_call);

	void defer_source_speaker_level_all(
		const EaxxEaxCall& eax_call);

	// Source
	// ----------------------------------------------------------------------


	void set_outside_volume_hf();

	void set_doppler_factor();

	void set_rolloff_factor();

	void set_room_rolloff_factor();

	void set_air_absorption_factor();


	void set_direct_hf_auto_flag();

	void set_room_auto_flag();

	void set_room_hf_auto_flag();

	void set_flags();


	void set_macro_fx_factor();

	void set_speaker_levels();


	void apply_deferred();

	void set(
		const EaxxEaxCall& eax_call);


	static const ::GUID& get_send_fx_slot_guid(
		int eax_version,
		int fx_slot_index);

	static void copy_send(
		const ::EAXSOURCEALLSENDPROPERTIES& src_send,
		::EAXSOURCESENDPROPERTIES& dst_send);

	static void copy_send(
		const ::EAXSOURCEALLSENDPROPERTIES& src_send,
		::EAXSOURCEALLSENDPROPERTIES& dst_send);

	static void copy_send(
		const ::EAXSOURCEALLSENDPROPERTIES& src_send,
		::EAXSOURCEOCCLUSIONSENDPROPERTIES& dst_send);

	static void copy_send(
		const ::EAXSOURCEALLSENDPROPERTIES& src_send,
		::EAXSOURCEEXCLUSIONSENDPROPERTIES& dst_send);

	template<
		typename TException,
		typename TSrcSend
	>
	void api_get_send_properties(
		const EaxxEaxCall& eax_call) const
	{
		const auto eax_version = eax_call.get_version();
		const auto dst_sends = eax_call.get_values<TException, TSrcSend>();

		for (auto fx_slot_index = 0; fx_slot_index < dst_sends.size; ++fx_slot_index)
		{
			auto& dst_send = dst_sends.values[fx_slot_index];
			const auto& src_send = eax_.sends[fx_slot_index];

			copy_send(src_send, dst_send);

			dst_send.guidReceivingFXSlotID = get_send_fx_slot_guid(eax_version, fx_slot_index);
		}
	}


	void api_get_source_all_2(
		const EaxxEaxCall& eax_call);

	void api_get_source_all_3(
		const EaxxEaxCall& eax_call);

	void api_get_source_all_5(
		const EaxxEaxCall& eax_call);

	void api_get_source_all(
		const EaxxEaxCall& eax_call);

	void api_get_source_all_obstruction(
		const EaxxEaxCall& eax_call);

	void api_get_source_all_occlusion(
		const EaxxEaxCall& eax_call);

	void api_get_source_all_exclusion(
		const EaxxEaxCall& eax_call);

	void api_get_source_active_fx_slot_id(
		const EaxxEaxCall& eax_call);

	void api_get_source_all_2d(
		const EaxxEaxCall& eax_call);

	void api_get_source_speaker_level_all(
		const EaxxEaxCall& eax_call);

	void get(
		const EaxxEaxCall& eax_call);
}; // EaxxSource

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_SOURCE_INCLUDED
