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


#ifndef EAX_EAXX_INCLUDED
#define EAX_EAXX_INCLUDED


#include <memory>

#include "AL/al.h"
#include "AL/alc.h"

#include "eax_api.h"
#include "eax_logger.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class Eaxx
{
public:
	Eaxx() = default;

	virtual ~Eaxx() = default;


	virtual void set_last_error() noexcept = 0;


	virtual void* on_alGetProcAddress(
		const ::ALchar* symbol_name) = 0;

	virtual void on_alGenSources(
		::ALsizei n,
		::ALuint* sources) = 0;

	virtual void on_alDeleteSources(
		::ALsizei n,
		const ::ALuint* sources) = 0;


	virtual ::ALenum EAXSet(
		const ::GUID* property_set_guid,
		::ALuint property_id,
		::ALuint property_al_name,
		::ALvoid* property_buffer,
		::ALuint property_size) = 0;

	virtual ::ALenum EAXGet(
		const ::GUID* property_set_guid,
		::ALuint property_id,
		::ALuint property_al_name,
		::ALvoid* property_buffer,
		::ALuint property_size) = 0;
}; // Eaxx

using EaxxUPtr = std::unique_ptr<Eaxx>;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxUPtr make_eaxx(
	::ALCdevice* alc_device,
	::ALCcontext* alc_context);

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_INCLUDED
