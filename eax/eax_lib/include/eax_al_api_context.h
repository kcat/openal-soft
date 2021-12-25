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

#ifndef EAX_AL_API_CONTEXT_INCLUDED
#define EAX_AL_API_CONTEXT_INCLUDED


#include "AL/alc.h"

#include <memory>

#include "eax_logger.h"

#include "eax_eaxx.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

struct AlApiContextInitParam
{
	Logger* logger{};
	::ALCdevice* alc_device{};
	::ALCcontext* alc_context{};
	::LPALCMAKECONTEXTCURRENT alcMakeContextCurrent_internal{};
}; // AlApiContextInitParam

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class AlApiContext
{
public:
	AlApiContext() noexcept = default;

	virtual ~AlApiContext() = default;


	virtual void on_alcMakeContextCurrent() = 0;

	virtual void on_alcDestroyContext() = 0;

	virtual void* on_alGetProcAddress(
		const ::ALchar* symbol_name) const noexcept = 0;

	virtual bool on_alIsExtensionPresent(
		const char* extension_name) const noexcept = 0;

	virtual void on_alGenSources(
		::ALsizei n,
		::ALuint* sources) = 0;

	virtual void on_alDeleteSources(
		::ALsizei n,
		const ::ALuint* sources) = 0;


	virtual ::ALCcontext* get_al_context() const noexcept = 0;

	virtual Eaxx& get_eaxx() = 0;
}; // AlApiContext

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using AlApiContextUPtr = std::unique_ptr<AlApiContext>;

AlApiContextUPtr make_al_api_context(
	const AlApiContextInitParam& param);

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_AL_API_CONTEXT_INCLUDED

