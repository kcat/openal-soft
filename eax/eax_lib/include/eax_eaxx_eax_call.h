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


#ifndef EAX_EAXX_CALL_INCLUDED
#define EAX_EAXX_CALL_INCLUDED


#include "AL/al.h"

#include "eax_api.h"

#include "eax_eaxx_fx_slot_index.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

enum class EaxxEaxCallPropertySetId
{
	none,

	context,
	fx_slot,
	source,
	fx_slot_effect,
}; // EaxxEaxCallPropertySetId

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

template<
	typename T
>
struct EaxxEaxCallSpan
{
	int size{};
	T* values{};
}; // EaxxEaxCallSpan

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEaxCall
{
public:
	EaxxEaxCall(
		bool is_get,
		const ::GUID* property_set_guid,
		::ALuint property_id,
		::ALuint property_al_name,
		::ALvoid* property_buffer,
		::ALuint property_size);

	bool is_get() const noexcept;

	bool is_deferred() const noexcept;

	int get_version() const noexcept;

	EaxxEaxCallPropertySetId get_property_set_id() const noexcept;

	::ALuint get_property_id() const noexcept;

	::ALuint get_property_al_name() const noexcept;

	EaxxFxSlotIndex get_fx_slot_index() const noexcept;


	template<
		typename TException,
		typename TValue
	>
	TValue& get_value() const
	{
		if (property_size_ < static_cast<::ALuint>(sizeof(TValue)))
		{
			throw TException{"Property buffer too small."};
		}

		return *static_cast<TValue*>(property_buffer_);
	}

	template<
		typename TException,
		typename TValue
	>
	EaxxEaxCallSpan<TValue> get_values() const
	{
		if (property_size_ < static_cast<::ALuint>(sizeof(TValue)))
		{
			throw TException{"Property buffer too small."};
		}

		const auto count = static_cast<int>(property_size_ / sizeof(TValue));

		return EaxxEaxCallSpan<TValue>{count, static_cast<TValue*>(property_buffer_)};
	}

	template<
		typename TException,
		typename TValue>
	void set_value(
		const TValue& value) const
	{
		get_value<TException, TValue>() = value;
	}


private:
	bool is_get_;
	bool is_deferred_;
	int version_;
	EaxxFxSlotIndex fx_slot_index_;
	EaxxEaxCallPropertySetId property_set_id_;

	::GUID property_set_guid_;
	::ALuint property_id_;
	::ALuint property_al_name_;
	::ALvoid* property_buffer_;
	::ALuint property_size_;


	[[noreturn]]
	static void fail(
		const char* message);


	static ::ALuint convert_eax_2_listener_property_id(
		::ALuint property_id);

	static ::ALuint convert_eax_2_buffer_property_id(
		::ALuint property_id);
}; // EaxxEaxCall

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxEaxCall make_eax_call(
	bool is_get,
	const ::GUID* property_set_id,
	::ALuint property_id,
	::ALuint property_al_name,
	::ALvoid* property_buffer,
	::ALuint property_size);

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_CALL_INCLUDED
