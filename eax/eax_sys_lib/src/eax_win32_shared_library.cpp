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


#include "eax_shared_library.h"


#ifdef _WIN32


#include <cassert>

#include <memory>
#include <tuple>

#include <windows.h>

#include "eax_exception.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class Win32SharedLibraryException :
	public Exception
{
public:
	explicit Win32SharedLibraryException(
		const char* message)
		:
		Exception{"WIN32_SHARED_LIBRARY", message}
	{
	}
}; // Win32SharedLibraryException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class Win32SharedLibrary :
	public SharedLibrary
{
public:
	Win32SharedLibrary(
		const char* path);

	~Win32SharedLibrary() override;


	void* resolve(
		const char* symbol_name) noexcept override;


private:
	::HMODULE win32_module_;
}; // Win32SharedLibrary

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

Win32SharedLibrary::Win32SharedLibrary(
	const char* path)
	:
	win32_module_{}
{
	win32_module_ = ::LoadLibraryA(path);

	if (!win32_module_)
	{
		throw Win32SharedLibraryException{"::LoadLibrary failed."};
	}
}

Win32SharedLibrary::~Win32SharedLibrary()
{
	if (win32_module_)
	{
		const auto win32_result = ::FreeLibrary(win32_module_);

#ifdef NDEBUG
		std::ignore = win32_result;
#else
		assert(win32_result == TRUE);
#endif // NDEBUG
	}
}

void* Win32SharedLibrary::resolve(
	const char* symbol_name) noexcept
{
	return reinterpret_cast<void*>(::GetProcAddress(win32_module_, symbol_name));
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

SharedLibraryUPtr make_shared_library(
	const char* path)
{
	return std::make_unique<Win32SharedLibrary>(path);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // _WIN32
