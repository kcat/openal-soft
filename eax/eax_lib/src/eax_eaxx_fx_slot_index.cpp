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


#include "eax_eaxx_fx_slot_index.h"

#include "eax_exception.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxFxSlotIndexException :
	public Exception
{
public:
	explicit EaxxFxSlotIndexException(
		const char* message)
		:
		Exception{"EAXX_FX_SLOT_INDEX", message}
	{
	}
}; // EaxxFxSlotIndexException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxFxSlotIndex::EaxxFxSlotIndex() noexcept
	:
	has_value_{}
{
}

EaxxFxSlotIndex::EaxxFxSlotIndex(
	EaxxFxSlotIndexValue index)
	:
	EaxxFxSlotIndex{}
{
	set(index);
}

EaxxFxSlotIndex::EaxxFxSlotIndex(
	const EaxxFxSlotIndex& rhs) noexcept
	:
	has_value_{rhs.has_value_},
	value_{rhs.value_}
{
}

void EaxxFxSlotIndex::operator=(
	EaxxFxSlotIndexValue index)
{
	set(index);
}

void EaxxFxSlotIndex::operator=(
	const ::GUID& guid)
{
	set(guid);
}

void EaxxFxSlotIndex::operator=(
	const EaxxFxSlotIndex& rhs) noexcept
{
	has_value_ = rhs.has_value_;
	value_ = rhs.value_;
}

bool EaxxFxSlotIndex::has_value() const noexcept
{
	return has_value_;
}

EaxxFxSlotIndexValue EaxxFxSlotIndex::get() const
{
	if (!has_value_)
	{
		throw EaxxFxSlotIndexException{"No value."};
	}

	return value_;
}

void EaxxFxSlotIndex::reset() noexcept
{
	has_value_ = false;
}

void EaxxFxSlotIndex::set(
	EaxxFxSlotIndexValue index)
{
	if (index >= ::EAX_MAX_FXSLOTS)
	{
		throw EaxxFxSlotIndexException{"Index out of range."};
	}

	has_value_ = true;
	value_ = index;
}

void EaxxFxSlotIndex::set(
	const ::GUID& guid)
{
	if (false)
	{
	}
	else if (guid == ::EAX_NULL_GUID)
	{
		has_value_ = false;
	}
	else if (guid == ::EAXPROPERTYID_EAX40_FXSlot0 || guid == ::EAXPROPERTYID_EAX50_FXSlot0)
	{
		has_value_ = true;
		value_ = 0;
	}
	else if (guid == ::EAXPROPERTYID_EAX40_FXSlot1 || guid == ::EAXPROPERTYID_EAX50_FXSlot1)
	{
		has_value_ = true;
		value_ = 1;
	}
	else if (guid == ::EAXPROPERTYID_EAX40_FXSlot2 || guid == ::EAXPROPERTYID_EAX50_FXSlot2)
	{
		has_value_ = true;
		value_ = 2;
	}
	else if (guid == ::EAXPROPERTYID_EAX40_FXSlot3 || guid == ::EAXPROPERTYID_EAX50_FXSlot3)
	{
		has_value_ = true;
		value_ = 3;
	}
	else
	{
		throw EaxxFxSlotIndexException{"Unsupported GUID."};
	}
}

EaxxFxSlotIndex::operator EaxxFxSlotIndexValue() const
{
	return get();
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxFxSlotIndex& lhs,
	const EaxxFxSlotIndex& rhs) noexcept
{
	if (lhs.has_value() != rhs.has_value())
	{
		return false;
	}

	if (lhs.has_value())
	{
		return lhs.get() == rhs.get();
	}
	else
	{
		return true;
	}
}

bool operator!=(
	const EaxxFxSlotIndex& lhs,
	const EaxxFxSlotIndex& rhs) noexcept
{
	return !(lhs == rhs);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
