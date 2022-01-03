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


#include "eax_al_api.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/efx.h"

#include "eax_al_api_context.h"
#include "eax_api.h"
#include "eax_c_str.h"
#include "eax_exception.h"
#include "eax_logger.h"
#include "eax_moveable_mutex_lock.h"
#include "eax_utils.h"

#include "eax_eaxx.h"


namespace eax
{


namespace
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

constexpr auto max_x_ram_size = static_cast<::ALsizei>(64 * 1'024 * 1'024);

constexpr auto x_ram_ram_size_enum = static_cast<::ALenum>(0x1);
constexpr auto x_ram_ram_free_enum = static_cast<::ALenum>(0x2);
constexpr auto x_ram_al_storage_automatic_enum = static_cast<::ALenum>(0x3);
constexpr auto x_ram_al_storage_hardware_enum = static_cast<::ALenum>(0x4);
constexpr auto x_ram_al_storage_accessible_enum = static_cast<::ALenum>(0x5);

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

::ALboolean AL_APIENTRY EAXSetBufferMode(
	::ALsizei n,
	const ::ALuint* buffers,
	::ALint value)
try
{
	const auto mutex_lock = g_al_api.get_lock();

	return g_al_api.on_EAXSetBufferMode(n, buffers, value);
}
catch (...)
{
	utils::log_exception(g_al_api.get_logger(), __func__);
	return AL_FALSE;
}

::ALenum AL_APIENTRY EAXGetBufferMode(
	::ALuint buffer,
	::ALint* value)
try
{
	const auto mutex_lock = g_al_api.get_lock();

	return g_al_api.on_EAXGetBufferMode(buffer, value);
}
catch (...)
{
	utils::log_exception(g_al_api.get_logger(), __func__);
	return x_ram_al_storage_automatic_enum;
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class AlApiException :
	public Exception
{
public:
	explicit AlApiException(
		const char* message)
		:
		Exception{"AL_API", message}
	{
	}
}; // AlApiException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class AlApiImpl final :
	public AlApi
{
public:
	AlApiImpl() noexcept;


	Logger& get_logger() noexcept override;

	MoveableMutexLock get_lock() noexcept override;

	AlApiContext& get_current_context() override;


	// =========================================================================
	// ALC v1.1

	void on_alcCreateContext(
		::ALCdevice* alc_device,
		::ALCcontext* alc_context) noexcept override;

	void on_alcMakeContextCurrent(
		::ALCcontext* alc_context) noexcept override;

	void on_alcDestroyContext(
		::ALCcontext* alc_context) noexcept override;

	void on_alcOpenDevice(
		::ALCdevice* alc_device) noexcept override;

	void on_alcCloseDevice(
		::ALCdevice* alc_device) noexcept override;

	// ALC v1.1
	// =========================================================================


	// =========================================================================
	// AL v1.1

	::ALint on_alGetInteger(
		::ALenum al_param) noexcept override;

	::ALboolean on_alIsExtensionPresent(
		const ::ALchar* al_extension_name) noexcept override;

	void* on_alGetProcAddress(
		const ::ALchar* al_name) noexcept override;

	::ALenum on_alGetEnumValue(
		const ::ALchar* al_name) noexcept override;

	void on_alGenSources(
		::ALsizei count,
		::ALuint* al_sources) noexcept override;

	void on_alDeleteSources(
		::ALsizei al_count,
		const ::ALuint* al_sources) noexcept override;

	void on_alGenBuffers(
		::ALsizei al_count,
		::ALuint* al_buffers) noexcept override;

	void on_alDeleteBuffers(
		::ALsizei al_count,
		const ::ALuint* al_buffers) noexcept override;

	::ALenum on_alBufferData_1(
		::ALuint al_buffer,
		::ALsizei size) noexcept override;

	void on_alBufferData_2() noexcept override;

	// AL v1.1
	// =========================================================================

	// =========================================================================
	// X_RAM

	::ALboolean on_EAXSetBufferMode(
		::ALsizei al_count,
		const ::ALuint* al_buffers,
		::ALint al_value) override;

	::ALenum on_EAXGetBufferMode(
		::ALuint al_buffer,
		::ALint* al_value) override;

	// X_RAM
	// =========================================================================


private:
	struct Buffer
	{
		::ALsizei size{};
		::ALenum x_ram_mode{};
		bool x_ram_is_allocated{};
		bool x_ram_is_dirty{};
	}; // Buffer

	using BufferMap = std::unordered_map<::ALuint, Buffer>;

	using Contexts = std::list<AlApiContextUPtr>;


	struct Device
	{
		::ALCdevice* alc_device{};
		::ALsizei x_ram_free_size{};

		BufferMap buffers{};
		Contexts contexts{};
	}; // Device

	using Devices = std::list<Device>;


	struct XRamAlBufferDataContext
	{
		Buffer* buffer{};
		::ALsizei buffer_size{};
		bool buffer_x_ram_is_allocated{};

		Device* device{};
		::ALsizei device_x_ram_size_delta{};
	}; // XRamAlBufferDataContext


	using InitializeFunc = MoveableMutexLock (AlApiImpl::*)();
	using GetLockFunc = MoveableMutexLock (AlApiImpl::*)();


	using MutexLock = std::unique_lock<std::mutex>;
	using MutexUPtr = std::unique_ptr<std::mutex>;


	InitializeFunc initialize_func_{};
	GetLockFunc get_lock_func_{};
	MutexUPtr mutex_{};
	Logger* logger_{};
	Devices devices_{};
	AlApiContext* current_context_{};
	eax::EaxxUPtr eaxx_{};
	XRamAlBufferDataContext x_ram_al_buffer_data_context_{};
	::LPALCMAKECONTEXTCURRENT alcMakeContextCurrent_internal_{};


	[[noreturn]]
	static void fail(
		const char* message);


	void initialize(
		const AlApiInitParam& param) override;


	MoveableMutexLock get_lock_not_initialized();

	MoveableMutexLock get_lock_initialized();


	Device* find_device(
		::ALCdevice* alc_device) noexcept;

	Device& get_device(
		::ALCdevice* al_device);

	Device& get_current_device();

	Buffer& get_buffer(
		Device& device,
		::ALuint al_buffer_name);

	Buffer& get_current_buffer(
		::ALuint al_buffer_name);


	AlApiContext& get_context();

	AlApiContext& get_context(
		::ALCcontext* al_context);

	void remove_context(
		const AlApiContext& context);
}; // AlApiImpl

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

AlApiImpl::AlApiImpl() noexcept
	:
	get_lock_func_{&AlApiImpl::get_lock_not_initialized}
{
}

Logger& AlApiImpl::get_logger() noexcept
{
	assert(logger_);

	return *logger_;
}

MoveableMutexLock AlApiImpl::get_lock() noexcept
{
	assert(get_lock_func_);
	return (this->*get_lock_func_)();
}

AlApiContext& AlApiImpl::get_current_context()
{
	return get_context();
}

// ==========================================================================
// ALC v1.1

void AlApiImpl::on_alcCreateContext(
	::ALCdevice* alc_device,
	::ALCcontext* alc_context) noexcept
try
{
	auto& device = get_device(alc_device);

	auto context_init_param = AlApiContextInitParam{};
	context_init_param.logger = logger_;
	context_init_param.alc_device = alc_device;
	context_init_param.alc_context = alc_context;
	context_init_param.alcMakeContextCurrent_internal = alcMakeContextCurrent_internal_;

	auto al_api_context = make_al_api_context(context_init_param);

	device.contexts.emplace_back(std::move(al_api_context));
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
}

void AlApiImpl::on_alcMakeContextCurrent(
	::ALCcontext* alc_context) noexcept
try
{
	if (alc_context)
	{
		auto& context = get_context(alc_context);
		context.on_alcMakeContextCurrent();
		current_context_ = &context;
	}
	else
	{
		current_context_ = nullptr;
	}
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
}

void AlApiImpl::on_alcDestroyContext(
	::ALCcontext* alc_context) noexcept
try
{
	if (!alc_context)
	{
		assert(false && "Null context.");
		return;
	}

	auto& context = get_context(alc_context);
	context.on_alcDestroyContext();
	current_context_ = nullptr;
	remove_context(context);
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
}

 void AlApiImpl::on_alcOpenDevice(
	::ALCdevice* alc_device) noexcept
try
{
	devices_.emplace_back();

	auto& device = devices_.back();
	device.alc_device = alc_device;
	device.x_ram_free_size = max_x_ram_size;
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
}

void AlApiImpl::on_alcCloseDevice(
	::ALCdevice* alc_device) noexcept
try
{
	devices_.remove_if(
		[alc_device](
			const Device& device)
		{
			return device.alc_device == alc_device;
		}
	);
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
}

// ALC v1.1
// ==========================================================================

// =========================================================================
// AL v1.1

::ALint AlApiImpl::on_alGetInteger(
	::ALenum al_param) noexcept
try
{
	switch (al_param)
	{
		case x_ram_ram_size_enum:
			return max_x_ram_size;

		case x_ram_ram_free_enum:
			{
				const auto& device = get_current_device();
				return device.x_ram_free_size;
			}

		default:
			return AL_NONE;
	}
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
	return AL_NONE;
}

::ALboolean AlApiImpl::on_alIsExtensionPresent(
	const ::ALchar* al_extension_name) noexcept
try
{
	if (!current_context_)
	{
		return AL_FALSE;
	}

	if (c_str::ascii::are_equal_ci(al_extension_name, "EAX-RAM"))
	{
		return AL_TRUE;
	}

	return current_context_->on_alIsExtensionPresent(al_extension_name);
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
	return AL_FALSE;
}

void* AlApiImpl::on_alGetProcAddress(
	const ::ALchar* al_name) noexcept
try
{
	assert(al_name);

	constexpr auto x_ram_eax_set_buffer_mode_name = "EAXSetBufferMode";
	constexpr auto x_ram_eax_get_buffer_mode_name = "EAXGetBufferMode";

	if (false)
	{
	}
	else if (c_str::ascii::are_equal_ci(al_name, x_ram_eax_set_buffer_mode_name))
	{
		return reinterpret_cast<void*>(eax::EAXSetBufferMode);
	}
	else if (c_str::ascii::are_equal_ci(al_name, x_ram_eax_get_buffer_mode_name))
	{
		return reinterpret_cast<void*>(eax::EAXGetBufferMode);
	}

	if (!current_context_)
	{
		return nullptr;
	}

	return current_context_->on_alGetProcAddress(al_name);
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
	return nullptr;
}

::ALenum AlApiImpl::on_alGetEnumValue(
	const ::ALchar* al_name) noexcept
try
{
	assert(al_name);

	constexpr auto x_ram_ram_size_name = "AL_EAX_RAM_SIZE";
	constexpr auto x_ram_ram_free_name = "AL_EAX_RAM_FREE";
	constexpr auto x_ram_al_storage_automatic_name = "AL_STORAGE_AUTOMATIC";
	constexpr auto x_ram_al_storage_hardware_name = "AL_STORAGE_HARDWARE";
	constexpr auto x_ram_al_storage_accessible_name = "AL_STORAGE_ACCESSIBLE";

	if (false)
	{
	}
	else if (c_str::ascii::are_equal_ci(al_name, x_ram_ram_size_name))
	{
		return x_ram_ram_size_enum;
	}
	else if (c_str::ascii::are_equal_ci(al_name, x_ram_ram_free_name))
	{
		return x_ram_ram_free_enum;
	}
	else if (c_str::ascii::are_equal_ci(al_name, x_ram_al_storage_automatic_name))
	{
		return x_ram_al_storage_automatic_enum;
	}
	else if (c_str::ascii::are_equal_ci(al_name, x_ram_al_storage_hardware_name))
	{
		return x_ram_al_storage_hardware_enum;
	}
	else if (c_str::ascii::are_equal_ci(al_name, x_ram_al_storage_accessible_name))
	{
		return x_ram_al_storage_accessible_enum;
	}
	else
	{
		return AL_NONE;
	}
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
	return AL_NONE;
}

void AlApiImpl::on_alGenSources(
	::ALsizei al_count,
	::ALuint* al_sources) noexcept
try
{
	assert(al_count > 0);
	assert(al_sources);

	auto& context = get_current_context();
	context.on_alGenSources(al_count, al_sources);
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
}

void AlApiImpl::on_alDeleteSources(
	::ALsizei al_count,
	const ::ALuint* al_sources) noexcept
try
{
	assert(al_count > 0);
	assert(al_sources);

	auto& context = get_current_context();
	context.on_alDeleteSources(al_count, al_sources);
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
}

void AlApiImpl::on_alGenBuffers(
	::ALsizei al_count,
	::ALuint* al_buffers) noexcept
try
{
	assert(al_count > 0);
	assert(al_buffers);

	auto& device = get_current_device();
	auto& buffers = device.buffers;

	auto buffer = Buffer{};
	buffer.x_ram_mode = x_ram_al_storage_automatic_enum;

	for (auto i = decltype(al_count){}; i < al_count; ++i)
	{
		const auto al_buffer = al_buffers[i];
		assert(al_buffer != AL_NONE);
		buffers.emplace(al_buffer, buffer);
	}
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
}

void AlApiImpl::on_alDeleteBuffers(
	::ALsizei al_count,
	const ::ALuint* al_buffers) noexcept
try
{
	assert(al_count > 0);
	assert(al_buffers);

	auto& device = get_current_device();
	auto& buffers = device.buffers;

	for (auto i = decltype(al_count){}; i < al_count; ++i)
	{
		const auto al_buffer = al_buffers[i];

		if (al_buffer == 0)
		{
			continue;
		}

		const auto buffer_it = buffers.find(al_buffers[i]);

		if (buffer_it == buffers.cend())
		{
			assert(!"Unregistered buffer.");
			continue;
		}

		const auto& buffer = buffer_it->second;

		if (buffer.x_ram_is_allocated)
		{
			device.x_ram_free_size += buffer.size;
			assert(device.x_ram_free_size >= 0 && device.x_ram_free_size <= max_x_ram_size);
		}

		buffers.erase(buffer_it);
	}
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
}

::ALenum AlApiImpl::on_alBufferData_1(
	::ALuint al_buffer,
	::ALsizei size) noexcept
try
{
	auto& device = get_current_device();
	auto& buffer = get_buffer(device, al_buffer);

	auto x_ram_is_allocated = buffer.x_ram_is_allocated;
	auto x_ram_size_delta = decltype(size){};

	switch (buffer.x_ram_mode)
	{
		case x_ram_al_storage_automatic_enum:
			if (!buffer.x_ram_is_dirty)
			{
				// Never used before.

				if (device.x_ram_free_size >= size)
				{
					// Have enough X-RAM memory.

					x_ram_is_allocated = true;
					x_ram_size_delta = -size;
				}
			}
			else
			{
				// Used at least once.
				// From now on, use only system memory.

				x_ram_is_allocated = false;

				if (buffer.x_ram_is_allocated)
				{
					// First allocated was in X-RAM.
					// Free that block.

					x_ram_size_delta = size;
				}
			}

			break;

		case x_ram_al_storage_hardware_enum:
			if (device.x_ram_free_size >= size)
			{
				// Have enough X-RAM memory.

				x_ram_is_allocated = true;
				x_ram_size_delta = buffer.size - size;
			}
			else
			{
				// No free X-RAM memory - no buffer.
				return AL_OUT_OF_MEMORY;
			}
			break;

		case x_ram_al_storage_accessible_enum:
			// Always use system memory.
			x_ram_is_allocated = false;
			break;

		default:
			return ALC_INVALID_ENUM;
	}

	x_ram_al_buffer_data_context_.buffer = &buffer;
	x_ram_al_buffer_data_context_.buffer_size = size;
	x_ram_al_buffer_data_context_.buffer_x_ram_is_allocated = x_ram_is_allocated;

	x_ram_al_buffer_data_context_.device = &device;
	x_ram_al_buffer_data_context_.device_x_ram_size_delta = x_ram_size_delta;

	return AL_NO_ERROR;
}
catch (...)
{
	utils::log_exception(get_logger(), __func__);
	return AL_OUT_OF_MEMORY;
}

void AlApiImpl::on_alBufferData_2() noexcept
{
	auto& buffer = *x_ram_al_buffer_data_context_.buffer;

	buffer.size = x_ram_al_buffer_data_context_.buffer_size;
	buffer.x_ram_is_allocated = x_ram_al_buffer_data_context_.buffer_x_ram_is_allocated;
	buffer.x_ram_is_dirty = true;

	auto& device = *x_ram_al_buffer_data_context_.device;
	device.x_ram_free_size += x_ram_al_buffer_data_context_.device_x_ram_size_delta;

	assert(device.x_ram_free_size >= 0 && device.x_ram_free_size <= max_x_ram_size);
}

// AL v1.1
// =========================================================================

// =========================================================================
// X_RAM

::ALboolean AlApiImpl::on_EAXSetBufferMode(
	::ALsizei al_count,
	const ::ALuint* al_buffers,
	::ALint al_value)
{
	if (al_count <= 0)
	{
		fail("Buffer count out of range.");
	}

	if (!al_buffers)
	{
		fail("Null buffers.");
	}

	switch (al_value)
	{
		case x_ram_al_storage_automatic_enum:
		case x_ram_al_storage_hardware_enum:
		case x_ram_al_storage_accessible_enum:
			break;

		default:
			fail("Unknown X-RAM mode.");
	}

	auto& device = get_current_device();

	for (auto i = decltype(al_count){}; i < al_count; ++i)
	{
		const auto al_buffer_name = al_buffers[i];

		if (al_buffer_name == AL_NONE)
		{
			fail("Null AL buffer name.");
		}

		const auto& buffer = get_buffer(device, al_buffer_name);

		if (buffer.x_ram_is_dirty)
		{
			fail("Non-empty buffer.");
		}
	}

	for (auto i = decltype(al_count){}; i < al_count; ++i)
	{
		const auto al_buffer_name = al_buffers[i];
		auto& buffer = get_buffer(device, al_buffer_name);
		buffer.x_ram_mode = al_value;
	}

	return AL_TRUE;
}

::ALenum AlApiImpl::on_EAXGetBufferMode(
	::ALuint al_buffer,
	::ALint* al_value)
{
	if (al_buffer == AL_NONE)
	{
		fail("Null AL buffer name.");
	}

	if (!al_value)
	{
		fail("Null X-RAM mode.");
	}

	switch (*al_value)
	{
		case x_ram_al_storage_automatic_enum:
		case x_ram_al_storage_hardware_enum:
		case x_ram_al_storage_accessible_enum:
			break;

		default:
			fail("Unknown X-RAM mode.");
	}

	const auto& our_buffer = get_current_buffer(al_buffer);

	return our_buffer.x_ram_mode;
}

// X_RAM
// =========================================================================

[[noreturn]]
void AlApiImpl::fail(
	const char* message)
{
	throw AlApiException{message};
}

void AlApiImpl::initialize(
	const AlApiInitParam& param)
{
	assert(param.logger);
	assert(param.alcMakeContextCurrent_internal);

	logger_ = param.logger;
	alcMakeContextCurrent_internal_ = param.alcMakeContextCurrent_internal;

	mutex_ = std::make_unique<std::mutex>();

	get_lock_func_ = &AlApiImpl::get_lock_initialized;
}

MoveableMutexLock AlApiImpl::get_lock_not_initialized()
{
	fail("Not initialized.");
}

MoveableMutexLock AlApiImpl::get_lock_initialized()
{
	assert(mutex_);

	return MoveableMutexLock{*mutex_};
}

AlApiImpl::Device* AlApiImpl::find_device(
	::ALCdevice* alc_device) noexcept
{
	for (auto& device : devices_)
	{
		if (device.alc_device == alc_device)
		{
			return &device;
		}
	}

	return nullptr;
}

AlApiImpl::Device& AlApiImpl::get_device(
	::ALCdevice* al_device)
{
	auto device = find_device(al_device);

	if (!device)
	{
		fail("Device not found.");
	}

	return *device;
}

AlApiImpl::Device& AlApiImpl::get_current_device()
{
	if (!current_context_)
	{
		fail("No current context.");
	}

	for (auto& device : devices_)
	{
		for (auto& context : device.contexts)
		{
			if (context.get() == current_context_)
			{
				return device;
			}
		}
	}

	fail("Unregistered device.");
}

AlApiImpl::Buffer& AlApiImpl::get_buffer(
	Device& device,
	::ALuint al_buffer_name)
{
	auto& buffers = device.buffers;
	const auto buffer_it = buffers.find(al_buffer_name);

	if (buffer_it == buffers.cend())
	{
		fail("Unregistered buffer.");
	}

	return buffer_it->second;
}

AlApiImpl::Buffer& AlApiImpl::get_current_buffer(
	::ALuint al_buffer_name)
{
	auto& device = get_current_device();
	return get_buffer(device, al_buffer_name);
}

AlApiContext& AlApiImpl::get_context()
{
	if (!current_context_)
	{
		fail("No current context.");
	}

	return *current_context_;
}

AlApiContext& AlApiImpl::get_context(
	::ALCcontext* al_context)
{
	for (auto& device : devices_)
	{
		for (auto& context : device.contexts)
		{
			if (context->get_al_context() == al_context)
			{
				return *context;
			}
		}
	}

	fail("Unregistered context.");
}

void AlApiImpl::remove_context(
	const AlApiContext& context)
{
	for (auto& device : devices_)
	{
		auto& contexts = device.contexts;

		contexts.remove_if(
			[&context_to_remove = context](
				const AlApiContextUPtr& context)
			{
				return context.get() == &context_to_remove;
			}
		);
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

AlApiImpl g_al_api_impl{};
AlApi& g_al_api = g_al_api_impl;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
