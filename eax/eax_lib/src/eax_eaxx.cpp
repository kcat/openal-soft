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


#include "eax_eaxx.h"

#include <cassert>
#include <cmath>

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/efx.h"

#include "eax_c_str.h"
#include "eax_exception.h"
#include "eax_utils.h"

#include "eax_al_api.h"
#include "eax_al_low_pass_param.h"
#include "eax_al_object.h"
#include "eax_api.h"

#include "eax_eaxx_context.h"
#include "eax_eaxx_eax_call.h"
#include "eax_eaxx_fx_slot_index.h"
#include "eax_eaxx_fx_slot.h"
#include "eax_eaxx_fx_slots.h"
#include "eax_eaxx_source.h"
#include "eax_eaxx_validators.h"


namespace eax
{


namespace
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

::ALenum AL_APIENTRY EAXSet(
	const ::GUID* property_set_guid,
	::ALuint property_id,
	::ALuint property_al_name,
	::ALvoid* property_buffer,
	::ALuint property_size)
try
{
	const auto mutex_lock = g_al_api.get_lock();
	auto& al_api_context = g_al_api.get_current_context();
	auto& eaxx = al_api_context.get_eaxx();

	try
	{
		return eaxx.EAXSet(
			property_set_guid,
			property_id,
			property_al_name,
			property_buffer,
			property_size
		);
	}
	catch (...)
	{
		eaxx.set_last_error();
		throw;
	}
}
catch (...)
{
	utils::log_exception(g_al_api.get_logger(), __func__);
	return AL_INVALID_OPERATION;
}

::ALenum AL_APIENTRY EAXGet(
	const ::GUID* property_set_guid,
	::ALuint property_id,
	::ALuint property_al_name,
	::ALvoid* property_buffer,
	::ALuint property_size)
try
{
	const auto mutex_lock = g_al_api.get_lock();
	auto& al_api_context = g_al_api.get_current_context();
	auto& eaxx = al_api_context.get_eaxx();

	try
	{
		return eaxx.EAXGet(
			property_set_guid,
			property_id,
			property_al_name,
			property_buffer,
			property_size
		);
	}
	catch (...)
	{
		eaxx.set_last_error();
		throw;
	}
}
catch (...)
{
	utils::log_exception(g_al_api.get_logger(), __func__);
	return AL_INVALID_OPERATION;
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxImplException :
	public Exception
{
public:
	explicit EaxxImplException(
		const char* message)
		:
		Exception{"EAXX", message}
	{
	}
}; // EaxxImplException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxImpl :
	public Eaxx
{
public:
	EaxxImpl(
		::ALCdevice* alc_device,
		::ALCcontext* alc_context);


	void set_last_error() noexcept override;


	void* on_alGetProcAddress(
		const ::ALchar* symbol_name) override;

	void on_alGenSources(
		::ALsizei n,
		::ALuint* sources) override;

	void on_alDeleteSources(
		::ALsizei n,
		const ::ALuint* sources) override;


	::ALenum EAXSet(
		const ::GUID* property_set_guid,
		::ALuint property_id,
		::ALuint property_al_name,
		::ALvoid* property_buffer,
		::ALuint property_size) override;

	::ALenum EAXGet(
		const ::GUID* property_set_guid,
		::ALuint property_id,
		::ALuint property_al_name,
		::ALvoid* property_buffer,
		::ALuint property_size) override;


private:
	static constexpr auto al_exts_buffer_reserve = 2048;
	static constexpr auto al_context_attrs_reserve = 32;
	static constexpr auto al_devices_reserve = 4;
	static constexpr auto al_contexts_reserve = al_devices_reserve;


	using AlcAttrCache = std::vector<::ALCint>;
	using AlExtsBuffer = std::string;
	using EaxxContextUPtr = std::unique_ptr<EaxxContext>;


	struct Device
	{
		::ALCdevice* al_device;
	}; // Device

	using DeviceMap = std::unordered_map<::ALCdevice*, Device>;
	using ContextMap = std::unordered_map<::ALCcontext*, EaxxContext>;


	Logger* logger_{};
	EaxxContextUPtr eaxx_context_{};
	bool is_default_reverb_effect_activated_{};


	[[noreturn]]
	static void fail(
		const char* message);


	void activate_default_reverb_effect();


	void dispatch_context(
		const EaxxEaxCall& eax_call);

	void dispatch_fxslot(
		const EaxxEaxCall& eax_call);

	void dispatch_source(
		const EaxxEaxCall& eax_call);
}; // EaxxImpl

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxImpl::EaxxImpl(
	::ALCdevice* alc_device,
	::ALCcontext* alc_context)
{
	assert(alc_device);
	assert(alc_context);

	logger_ = &g_al_api.get_logger();

	if (!logger_)
	{
		fail("Null logger.");
	}

	eaxx_context_ = std::make_unique<EaxxContext>(alc_device, alc_context);
}

void EaxxImpl::set_last_error() noexcept
{
	if (eaxx_context_)
	{
		eaxx_context_->set_last_error();
	}
}

void* EaxxImpl::on_alGetProcAddress(
	const ::ALchar* symbol_name)
{
	constexpr auto eax_get_name = "EAXGet";
	constexpr auto eax_set_name = "EAXSet";

	if (false)
	{
	}
	else if (c_str::ascii::are_equal_ci(symbol_name, eax_get_name))
	{
		return reinterpret_cast<void*>(eax::EAXGet);
	}
	else if (c_str::ascii::are_equal_ci(symbol_name, eax_set_name))
	{
		return reinterpret_cast<void*>(eax::EAXSet);
	}
	else
	{
		return nullptr;
	}
}

void EaxxImpl::on_alGenSources(
	::ALsizei n,
	::ALuint* sources)
{
	eaxx_context_->initialize_sources(n, sources);
}

void EaxxImpl::on_alDeleteSources(
	::ALsizei n,
	const ::ALuint* sources)
{
	eaxx_context_->uninitialize_sources(n, sources);
}

::ALenum EaxxImpl::EAXSet(
	const ::GUID* property_set_guid,
	::ALuint property_id,
	::ALuint property_al_name,
	::ALvoid* property_buffer,
	::ALuint property_size)
{
	activate_default_reverb_effect();

	const auto eax_call = make_eax_call(
		false,
		property_set_guid,
		property_id,
		property_al_name,
		property_buffer,
		property_size
	);

	switch (eax_call.get_property_set_id())
	{
		case EaxxEaxCallPropertySetId::context:
			dispatch_context(eax_call);
			break;

		case EaxxEaxCallPropertySetId::fx_slot:
		case EaxxEaxCallPropertySetId::fx_slot_effect:
			dispatch_fxslot(eax_call);
			break;

		case EaxxEaxCallPropertySetId::source:
			dispatch_source(eax_call);
			break;

		default:
			fail("Unsupported property set id.");
	}

	return AL_NO_ERROR;
}

::ALenum EaxxImpl::EAXGet(
	const ::GUID* property_set_guid,
	::ALuint property_id,
	::ALuint property_al_name,
	::ALvoid* property_buffer,
	::ALuint property_size)
{
	activate_default_reverb_effect();

	const auto eax_call = make_eax_call(
		true,
		property_set_guid,
		property_id,
		property_al_name,
		property_buffer,
		property_size
	);

	switch (eax_call.get_property_set_id())
	{
		case EaxxEaxCallPropertySetId::context:
			dispatch_context(eax_call);
			break;

		case EaxxEaxCallPropertySetId::fx_slot:
		case EaxxEaxCallPropertySetId::fx_slot_effect:
			dispatch_fxslot(eax_call);
			break;

		case EaxxEaxCallPropertySetId::source:
			dispatch_source(eax_call);
			break;

		default:
			fail("Unsupported property set id.");
	}

	return AL_NO_ERROR;
}

[[noreturn]]
void EaxxImpl::fail(
	const char* message)
{
	throw EaxxImplException{message};
}

void EaxxImpl::activate_default_reverb_effect()
{
	if (is_default_reverb_effect_activated_)
	{
		return;
	}

	is_default_reverb_effect_activated_ = true;

	eaxx_context_->activate_default_reverb_effect();
}

void EaxxImpl::dispatch_context(
	const EaxxEaxCall& eax_call)
{
	eaxx_context_->dispatch(eax_call);
}

void EaxxImpl::dispatch_fxslot(
	const EaxxEaxCall& eax_call)
{
	auto& fx_slot = eaxx_context_->get_slot(eax_call.get_fx_slot_index());

	if (fx_slot.dispatch(eax_call))
	{
		eaxx_context_->update_filters();
	}
}

void EaxxImpl::dispatch_source(
	const EaxxEaxCall& eax_call)
{
	auto source = eaxx_context_->find_source(eax_call.get_property_al_name());

	if (!source)
	{
		fail("Source not found.");
	}

	source->dispatch(eax_call);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxUPtr make_eaxx(
	::ALCdevice* alc_device,
	::ALCcontext* alc_context)
{
	return std::make_unique<EaxxImpl>(alc_device, alc_context);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
