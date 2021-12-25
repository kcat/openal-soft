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


#ifndef EAX_C_STR_UTILS_INCLUDED
#define EAX_C_STR_UTILS_INCLUDED


#include <cassert>
#include <cstddef>

#include <algorithm>


namespace eax
{
namespace c_str
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

template<
	typename TChar
>
inline constexpr std::size_t get_size(
	const TChar* c_string) noexcept
{
	assert(c_string);

	auto size = std::size_t{};

	while (c_string[size] != TChar{})
	{
		size += 1;
	}

	return size;
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


namespace ascii
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

inline constexpr int to_upper(
	int ch) noexcept
{
	if (ch >= 'a' && ch <= 'z')
	{
		return ch - 'a' + 'A';
	}
	else
	{
		return ch;
	}
}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

template<
	typename TChar
>
inline constexpr bool are_equal_ci(
	const TChar* lhs_c_string,
	const TChar* rhs_c_string) noexcept
{
	assert(lhs_c_string);
	assert(rhs_c_string);

	auto index = std::size_t{};

	while (lhs_c_string[index] != '\0' && rhs_c_string[index] != '\0')
	{
		const auto lhs_char_u = to_upper(lhs_c_string[index]);
		const auto rhs_char_u = to_upper(rhs_c_string[index]);

		if (lhs_char_u != rhs_char_u)
		{
			return false;
		}

		index += 1;
	}

	return lhs_c_string[index] == '\0' && rhs_c_string[index] == '\0';
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace ascii


} // namespace c_str
} // namespace eax


#endif // !EAX_C_STR_UTILS_INCLUDED
