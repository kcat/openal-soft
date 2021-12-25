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


#ifndef EAX_WIN32_PROCESS_PATCHER_INCLUDED
#define EAX_WIN32_PROCESS_PATCHER_INCLUDED


#ifdef _WIN32


#include "eax_patch.h"

#include <utility>

#include <windows.h>


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class Win32ProcessPatcherImpl final :
	public ProcessPatcher
{
public:
	Win32ProcessPatcherImpl(
		const Patch& patch);


	// ======================================================================
	// ProcessPatcher

	PatchStatus get_status() const noexcept override;

	void apply() override;

	// ProcessPatcher
	// ======================================================================


private:
	enum class Win32VirtualProtectorFlushType
	{
		none,

		normal,
	}; // Win32VirtualProtectorFlushType

	class Win32VirtualProtector
	{
	public:
		Win32VirtualProtector(
			::LPVOID address,
			::SIZE_T size,
			Win32VirtualProtectorFlushType flush_type);

		Win32VirtualProtector(
			const Win32VirtualProtector& rhs) = delete;

		Win32VirtualProtector(
			Win32VirtualProtector&& rhs)
		{
			std::swap(is_initialized_, rhs.is_initialized_);
			std::swap(address_, rhs.address_);
			std::swap(size_, rhs.size_);
			std::swap(old_protection_mode_, rhs.old_protection_mode_);
			std::swap(is_flush_, rhs.is_flush_);
		}

		Win32VirtualProtector& operator=(
			const Win32VirtualProtector& rhs) = delete;

		~Win32VirtualProtector();


	private:
		bool is_initialized_{};
		::LPVOID address_{};
		::SIZE_T size_{};
		::DWORD old_protection_mode_{};
		bool is_flush_{};
	}; // Win32VirtualProtector


	PatchStatus status_{};
	::BYTE* image_base_{};
	const Patch& patch_{};


	bool has_patch_block(
		const PatchBlock& patch_block,
		PatchBytes PatchBlock::*patch_bytes_selector);

	bool has_patch_blocks(
		const PatchBlocks& patch_blocks,
		PatchBytes PatchBlock::*patch_bytes_selector) noexcept;


	void apply_patch_block(
		const PatchBlock& patch_block,
		PatchBytes PatchBlock::*patch_bytes_selector);

	void apply_patch_blocks(
		const PatchBlocks& patch_blocks,
		PatchBytes PatchBlock::*patch_bytes_selector);
}; // Win32ProcessPatcherImpl

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // _WIN32


#endif // !EAX_WIN32_PROCESS_PATCHER_INCLUDED
