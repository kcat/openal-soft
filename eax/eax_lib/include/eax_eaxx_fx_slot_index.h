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


#ifndef EAX_EAXX_FX_SLOT_INDEX_INCLUDED
#define EAX_EAXX_FX_SLOT_INDEX_INCLUDED


#include "eax_api.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

using EaxxFxSlotIndexValue = int;


class EaxxFxSlotIndex
{
public:
	EaxxFxSlotIndex() noexcept;

	EaxxFxSlotIndex(
		EaxxFxSlotIndexValue index);

	EaxxFxSlotIndex(
		const EaxxFxSlotIndex& rhs) noexcept;

	void operator=(
		EaxxFxSlotIndexValue index);

	void operator=(
		const ::GUID& guid);

	void operator=(
		const EaxxFxSlotIndex& rhs) noexcept;


	bool has_value() const noexcept;

	EaxxFxSlotIndexValue get() const;

	void reset() noexcept;

	void set(
		EaxxFxSlotIndexValue index);

	void set(
		const ::GUID& guid);

	operator EaxxFxSlotIndexValue() const;


private:
	bool has_value_;
	EaxxFxSlotIndexValue value_;
}; // EaxxFxSlotIndex

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

bool operator==(
	const EaxxFxSlotIndex& lhs,
	const EaxxFxSlotIndex& rhs) noexcept;

bool operator!=(
	const EaxxFxSlotIndex& lhs,
	const EaxxFxSlotIndex& rhs) noexcept;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_FX_SLOT_INDEX_INCLUDED
