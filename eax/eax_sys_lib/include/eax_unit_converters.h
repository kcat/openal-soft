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


#ifndef EAX_UNIT_CONVERTERS_INCLUDED
#define EAX_UNIT_CONVERTERS_INCLUDED


#include <cmath>


namespace eax
{


template<
	typename T
>
inline float level_mb_to_gain(
	T x) noexcept
{
	if (x <= static_cast<T>(-10'000))
	{
		return 0.0F;
	}
	else
	{
		return std::pow(10.0F, static_cast<float>(x) / 2'000.0F);
	}
}

template<
	typename T
>
inline float gain_to_level_mb(
	T x) noexcept
{
	if (static_cast<float>(x) <= 0.0F)
	{
		return -10'000.0F;
	}
	else
	{
		return std::log10(static_cast<float>(x) * 2'000.0F);
	}
}


} // namespace eax


#endif // !EAX_UNIT_CONVERTERS_INCLUDED
