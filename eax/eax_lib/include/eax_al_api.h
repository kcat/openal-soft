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


#ifndef EAX_AL_API_INCLUDED
#define EAX_AL_API_INCLUDED

#include "AL/alc.h"

#include "eax_al_api_context.h"
#include "eax_logger.h"
#include "eax_moveable_mutex_lock.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

struct AlApiInitParam
{
	Logger* logger{};
	::LPALCMAKECONTEXTCURRENT alcMakeContextCurrent_internal{};
}; // AlApiInitParam

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class AlApi
{
public:
	AlApi() noexcept = default;

	virtual ~AlApi() = default;


	virtual void initialize(
		const AlApiInitParam& param) = 0;

	virtual Logger& get_logger() noexcept = 0;

	virtual MoveableMutexLock get_lock() noexcept = 0;

	virtual AlApiContext& get_current_context() = 0;


	// =========================================================================
	// ALC v1.1

	virtual void on_alcCreateContext(
		::ALCdevice* alc_device,
		::ALCcontext* alc_context) noexcept = 0;

	virtual void on_alcMakeContextCurrent(
		::ALCcontext* alc_context) noexcept = 0;

	virtual void on_alcDestroyContext(
		::ALCcontext* alc_context) noexcept = 0;

	virtual void on_alcOpenDevice(
		::ALCdevice* alc_device) noexcept = 0;

	virtual void on_alcCloseDevice(
		::ALCdevice* alc_device) noexcept = 0;

	// ALC v1.1
	// =========================================================================


	// =========================================================================
	// AL v1.1

	virtual ::ALint on_alGetInteger(
		::ALenum al_param) noexcept = 0;

	virtual ::ALboolean on_alIsExtensionPresent(
		const ::ALchar* al_extension_name) noexcept = 0;

	virtual void* on_alGetProcAddress(
		const ::ALchar* al_name) noexcept = 0;

	virtual ::ALenum on_alGetEnumValue(
		const ::ALchar* al_name) noexcept = 0;

	virtual void on_alGenSources(
		::ALsizei count,
		::ALuint* al_sources) noexcept = 0;

	virtual void on_alDeleteSources(
		::ALsizei al_count,
		const ::ALuint* al_sources) noexcept = 0;

	virtual void on_alGenBuffers(
		::ALsizei al_count,
		::ALuint* al_buffers) noexcept = 0;

	virtual void on_alDeleteBuffers(
		::ALsizei al_count,
		const ::ALuint* al_buffers) noexcept = 0;

	virtual ::ALenum on_alBufferData_1(
		::ALuint al_buffer,
		::ALsizei size) noexcept = 0;

	virtual void on_alBufferData_2() noexcept = 0;

	// AL v1.1
	// =========================================================================


	// =========================================================================
	// X_RAM

	virtual ::ALboolean on_EAXSetBufferMode(
		::ALsizei n,
		const ::ALuint* buffers,
		::ALint value) = 0;

	virtual ::ALenum on_EAXGetBufferMode(
		::ALuint buffer,
		::ALint* value) = 0;

	// X_RAM
	// =========================================================================
}; // AlApi

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

extern AlApi& g_al_api;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_AL_API_INCLUDED
