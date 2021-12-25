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


#include "eax_al_api_context.h"

#include <cassert>
#include <cstddef>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <utility>
#include <vector>

#include "AL/alc.h"
#include "AL/efx.h"

#include "eax_api.h"
#include "eax_c_str.h"
#include "eax_exception.h"
#include "eax_utils.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class AlApiContextException :
	public Exception
{
public:
	explicit AlApiContextException(
		const char* message)
		:
		Exception{"AL_API_CONTEXT", message}
	{
	}
}; // AlApiContextException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class AlApiContextImpl final :
	public AlApiContext
{
public:
	AlApiContextImpl(
		const AlApiContextInitParam& param);

	AlApiContextImpl(
		AlApiContextImpl&& rhs) noexcept;


	void on_alcMakeContextCurrent() override;

	void on_alcDestroyContext() override;

	void* on_alGetProcAddress(
		const ::ALchar* symbol_name) const noexcept override;

	bool on_alIsExtensionPresent(
		const char* extension_name) const noexcept override;

	void on_alGenSources(
		::ALsizei n,
		::ALuint* sources) override;

	void on_alDeleteSources(
		::ALsizei n,
		const ::ALuint* sources) override;


	::ALCcontext* get_al_context() const noexcept override;

	Eaxx& get_eaxx() override;


private:
	::ALCdevice* alc_device_{};
	::LPALCMAKECONTEXTCURRENT alcMakeContextCurrent_internal_{};

	Logger* logger_{};

	bool is_made_current_{};
	::ALCcontext* alc_context_{};

	EaxxUPtr eaxx_{};


	[[noreturn]]
	static void fail(
		const char* message);


	static void validate_init_param(
		const AlApiContextInitParam& param);

	void make_eaxx(
		::ALCdevice* alc_device,
		::ALCcontext* alc_context);
}; // AlApiContextImpl

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

AlApiContextImpl::AlApiContextImpl(
	const AlApiContextInitParam& param)
{
	validate_init_param(param);

	logger_ = param.logger;
	alc_device_ = param.alc_device;
	alc_context_ = param.alc_context;
	alcMakeContextCurrent_internal_ = param.alcMakeContextCurrent_internal;
}

AlApiContextImpl::AlApiContextImpl(
	AlApiContextImpl&& rhs) noexcept
{
	std::swap(alc_device_, rhs.alc_device_);

	std::swap(logger_, rhs.logger_);

	std::swap(is_made_current_, rhs.is_made_current_);
	std::swap(alc_context_, rhs.alc_context_);

	std::swap(eaxx_, rhs.eaxx_);
}

void AlApiContextImpl::on_alcMakeContextCurrent()
{
	if (is_made_current_)
	{
		return;
	}

	is_made_current_ = true;

	try
	{
		make_eaxx(alc_device_, alc_context_);
	}
	catch (...)
	{
		utils::log_exception(*logger_);
	}
}

void AlApiContextImpl::on_alcDestroyContext()
{
	const auto old_alc_context = ::alcGetCurrentContext();

	auto is_made_our_context_curent = false;

	if (alcMakeContextCurrent_internal_(alc_context_) == ALC_TRUE)
	{
		is_made_our_context_curent = true;
	}

	eaxx_ = nullptr;

	alc_device_ = nullptr;

	is_made_current_ = false;
	alc_context_ = nullptr;

	if (is_made_our_context_curent)
	{
		alcMakeContextCurrent_internal_(old_alc_context);
	}
}

void* AlApiContextImpl::on_alGetProcAddress(
	const ::ALchar* symbol_name) const noexcept
{
	return eaxx_->on_alGetProcAddress(symbol_name);
}

bool AlApiContextImpl::on_alIsExtensionPresent(
	const char* extension_name) const noexcept
{
	assert(extension_name);

	if (!eaxx_)
	{
		return false;
	}

	if (c_str::ascii::are_equal_ci(extension_name, "EAX2.0") ||
		c_str::ascii::are_equal_ci(extension_name, "EAX3.0") ||
		c_str::ascii::are_equal_ci(extension_name, "EAX4.0") ||
		c_str::ascii::are_equal_ci(extension_name, "EAX5.0"))
	{
		return true;
	}
	else
	{
		return false;
	}
}

void AlApiContextImpl::on_alGenSources(
	::ALsizei n,
	::ALuint* sources)
{
	if (!eaxx_)
	{
		return;
	}

	eaxx_->on_alGenSources(n, sources);
}

void AlApiContextImpl::on_alDeleteSources(
	::ALsizei n,
	const ::ALuint* sources)
{
	if (!eaxx_)
	{
		return;
	}

	eaxx_->on_alDeleteSources(n, sources);
}

::ALCcontext* AlApiContextImpl::get_al_context() const noexcept
{
	return alc_context_;
}

Eaxx& AlApiContextImpl::get_eaxx()
{
	if (!eaxx_)
	{
		fail("Null EAXX.");
	}

	return *eaxx_;
}

[[noreturn]]
void AlApiContextImpl::fail(
	const char* message)
{
	throw AlApiContextException{message};
}

void AlApiContextImpl::validate_init_param(
	const AlApiContextInitParam& param)
{
	if (!param.logger)
	{
		fail("Null logger.");
	}

	if (!param.alc_device)
	{
		fail("Null AL device.");
	}

	if (!param.alc_context)
	{
		fail("Null AL context.");
	}

	if (!param.alcMakeContextCurrent_internal)
	{
		fail("Null alcMakeContextCurrent_internal.");
	}
}

void AlApiContextImpl::make_eaxx(
	::ALCdevice* alc_device,
	::ALCcontext* alc_context)
{
	eaxx_ = eax::make_eaxx(alc_device, alc_context);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

AlApiContextUPtr make_al_api_context(
	const AlApiContextInitParam& param)
{
	return std::make_unique<AlApiContextImpl>(param);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
