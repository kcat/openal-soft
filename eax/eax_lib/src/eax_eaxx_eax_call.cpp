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


#include "eax_eaxx_eax_call.h"

#include "eax_exception.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEaxCallException :
	public Exception
{
public:
	explicit EaxxEaxCallException(
		const char* message)
		:
		Exception{"EAXX_EAX_CALL", message}
	{
	}
}; // EaxxEaxCallException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


EaxxEaxCall::EaxxEaxCall(
	bool is_get,
	const ::GUID* property_set_guid,
	::ALuint property_id,
	::ALuint property_al_name,
	::ALvoid* property_buffer,
	::ALuint property_size)
{
	if (!property_set_guid)
	{
		fail("Null property set GUID.");
	}

	is_get_ = is_get;

	constexpr auto deferred_flag = 0x80000000U;
	is_deferred_ = (property_id & 0x80000000U) != 0;

	version_ = 0;
	fx_slot_index_.reset();
	property_set_id_ = EaxxEaxCallPropertySetId::none;

	property_set_guid_ = *property_set_guid;
	property_id_ = property_id & (~deferred_flag);
	property_al_name_ = property_al_name;
	property_buffer_ = property_buffer;
	property_size_ = property_size;

	if (false)
	{
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX40_Context)
	{
		version_ = 4;
		property_set_id_ = EaxxEaxCallPropertySetId::context;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX50_Context)
	{
		version_ = 5;
		property_set_id_ = EaxxEaxCallPropertySetId::context;
	}
	else if (property_set_guid_ == DSPROPSETID_EAX20_ListenerProperties)
	{
		version_ = 2;
		fx_slot_index_ = 0;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot_effect;
		property_id_ = convert_eax_2_listener_property_id(property_id_);
	}
	else if (property_set_guid_ == DSPROPSETID_EAX30_ListenerProperties)
	{
		version_ = 3;
		fx_slot_index_ = 0;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot_effect;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX40_FXSlot0)
	{
		version_ = 4;
		fx_slot_index_ = 0;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX50_FXSlot0)
	{
		version_ = 5;
		fx_slot_index_ = 0;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX40_FXSlot1)
	{
		version_ = 4;
		fx_slot_index_ = 1;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX50_FXSlot1)
	{
		version_ = 5;
		fx_slot_index_ = 1;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX40_FXSlot2)
	{
		version_ = 4;
		fx_slot_index_ = 2;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX50_FXSlot2)
	{
		version_ = 5;
		fx_slot_index_ = 2;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX40_FXSlot3)
	{
		version_ = 4;
		fx_slot_index_ = 3;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX50_FXSlot3)
	{
		version_ = 5;
		fx_slot_index_ = 3;
		property_set_id_ = EaxxEaxCallPropertySetId::fx_slot;
	}
	else if (property_set_guid_ == ::DSPROPSETID_EAX20_BufferProperties)
	{
		version_ = 2;
		property_set_id_ = EaxxEaxCallPropertySetId::source;
		property_id_ = convert_eax_2_buffer_property_id(property_id_);
	}
	else if (property_set_guid_ == DSPROPSETID_EAX30_BufferProperties)
	{
		version_ = 3;
		property_set_id_ = EaxxEaxCallPropertySetId::source;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX40_Source)
	{
		version_ = 4;
		property_set_id_ = EaxxEaxCallPropertySetId::source;
	}
	else if (property_set_guid_ == ::EAXPROPERTYID_EAX50_Source)
	{
		version_ = 5;
		property_set_id_ = EaxxEaxCallPropertySetId::source;
	}
	else
	{
		fail("Unsupported property set GUID.");
	}

	if (version_ < 2 || version_ > 5)
	{
		fail("EAX version out of range.");
	}

	if (is_deferred_)
	{
		if (is_get_)
		{
			fail("Deferred properties not supported for EAXGet.");
		}
	}
	else
	{
		if (property_set_id_ != EaxxEaxCallPropertySetId::fx_slot &&
			property_id_ != 0)
		{
			if (!property_buffer)
			{
				fail("Null property buffer.");
			}

			if (property_size <= 0)
			{
				fail("Empty property.");
			}
		}
	}

	if (property_set_id_ == EaxxEaxCallPropertySetId::source &&
		property_al_name_ == 0)
	{
		fail("Null AL object name.");
	}

	if (property_set_id_ == EaxxEaxCallPropertySetId::fx_slot)
	{
		if (property_id_ < ::EAXFXSLOT_NONE)
		{
			property_set_id_ = EaxxEaxCallPropertySetId::fx_slot_effect;
		}
	}
}

bool EaxxEaxCall::is_get() const noexcept
{
	return is_get_;
}

bool EaxxEaxCall::is_deferred() const noexcept
{
	return is_deferred_;
}

int EaxxEaxCall::get_version() const noexcept
{
	return version_;
}

EaxxEaxCallPropertySetId EaxxEaxCall::get_property_set_id() const noexcept
{
	return property_set_id_;
}

::ALuint EaxxEaxCall::get_property_id() const noexcept
{
	return property_id_;
}

::ALuint EaxxEaxCall::get_property_al_name() const noexcept
{
	return property_al_name_;
}

EaxxFxSlotIndex EaxxEaxCall::get_fx_slot_index() const noexcept
{
	return fx_slot_index_;
}

[[noreturn]]
void EaxxEaxCall::fail(
	const char* message)
{
	throw EaxxEaxCallException{message};
}

::ALuint EaxxEaxCall::convert_eax_2_listener_property_id(
	::ALuint property_id)
{
	switch (property_id)
	{
		case ::DSPROPERTY_EAX20LISTENER_NONE:
			return ::EAXREVERB_NONE;

		case ::DSPROPERTY_EAX20LISTENER_ALLPARAMETERS:
			return ::EAXREVERB_ALLPARAMETERS;

		case ::DSPROPERTY_EAX20LISTENER_ROOM:
			return ::EAXREVERB_ROOM;

		case ::DSPROPERTY_EAX20LISTENER_ROOMHF:
			return ::EAXREVERB_ROOMHF;

		case ::DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR:
			return ::EAXREVERB_ROOMROLLOFFFACTOR;

		case ::DSPROPERTY_EAX20LISTENER_DECAYTIME:
			return ::EAXREVERB_DECAYTIME;

		case ::DSPROPERTY_EAX20LISTENER_DECAYHFRATIO:
			return ::EAXREVERB_DECAYHFRATIO;

		case ::DSPROPERTY_EAX20LISTENER_REFLECTIONS:
			return ::EAXREVERB_REFLECTIONS;

		case ::DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY:
			return ::EAXREVERB_REFLECTIONSDELAY;

		case ::DSPROPERTY_EAX20LISTENER_REVERB:
			return ::EAXREVERB_REVERB;

		case ::DSPROPERTY_EAX20LISTENER_REVERBDELAY:
			return ::EAXREVERB_REVERBDELAY;

		case ::DSPROPERTY_EAX20LISTENER_ENVIRONMENT:
			return ::EAXREVERB_ENVIRONMENT;

		case ::DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE:
			return ::EAXREVERB_ENVIRONMENTSIZE;

		case ::DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION:
			return ::EAXREVERB_ENVIRONMENTDIFFUSION;

		case ::DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF:
			return ::EAXREVERB_AIRABSORPTIONHF;

		case ::DSPROPERTY_EAX20LISTENER_FLAGS:
			return ::EAXREVERB_FLAGS;

		default:
			fail("Unsupported EAX 2.0 listener property id.");
	}
}

::ALuint EaxxEaxCall::convert_eax_2_buffer_property_id(
	::ALuint property_id)
{
	switch (property_id)
	{
		case ::DSPROPERTY_EAX20BUFFER_NONE:
			return ::DSPROPERTY_EAX20BUFFER_NONE;

		case ::DSPROPERTY_EAX20BUFFER_ALLPARAMETERS:
			return ::EAXSOURCE_ALLPARAMETERS;

		case ::DSPROPERTY_EAX20BUFFER_DIRECT:
			return ::EAXSOURCE_DIRECT;

		case ::DSPROPERTY_EAX20BUFFER_DIRECTHF:
			return ::EAXSOURCE_DIRECTHF;

		case ::DSPROPERTY_EAX20BUFFER_ROOM:
			return ::EAXSOURCE_ROOM;

		case ::DSPROPERTY_EAX20BUFFER_ROOMHF:
			return ::EAXSOURCE_ROOMHF;

		case ::DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
			return ::EAXSOURCE_ROOMROLLOFFFACTOR;

		case ::DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
			return ::EAXSOURCE_OBSTRUCTION;

		case ::DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
			return ::EAXSOURCE_OBSTRUCTIONLFRATIO;

		case ::DSPROPERTY_EAX20BUFFER_OCCLUSION:
			return ::EAXSOURCE_OCCLUSION;

		case ::DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
			return ::EAXSOURCE_OCCLUSIONLFRATIO;

		case ::DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
			return ::EAXSOURCE_OCCLUSIONROOMRATIO;

		case ::DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
			return ::EAXSOURCE_OUTSIDEVOLUMEHF;

		case ::DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
			return ::EAXSOURCE_AIRABSORPTIONFACTOR;

		case ::DSPROPERTY_EAX20BUFFER_FLAGS:
			return ::EAXSOURCE_FLAGS;

		default:
			fail("Unsupported EAX 2.0 buffer property id.");
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxEaxCall make_eax_call(
	bool is_get,
	const ::GUID* property_set_id,
	::ALuint property_id,
	::ALuint property_al_name,
	::ALvoid* property_buffer,
	::ALuint property_size)
{
	return EaxxEaxCall{
		is_get,
		property_set_id,
		property_id,
		property_al_name,
		property_buffer,
		property_size
	};
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
