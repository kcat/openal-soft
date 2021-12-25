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


#include "eax_win32_process_patcher.h"


#ifdef _WIN32


#include <cassert>

#include <algorithm>
#include <tuple>

#include "eax_exception.h"
#include "eax_patch_validator.h"
#include "eax_process.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class Win32ProcessPatcherVirtualProtectorException :
	public Exception
{
public:
	explicit Win32ProcessPatcherVirtualProtectorException(
		const char* message)
		:
		Exception{"WIN32_PROCESS_PATCHER_VIRTUAL_PROTECTOR", message}
	{
	}
}; // Win32ProcessPatcherVirtualProtectorException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

Win32ProcessPatcherImpl::Win32VirtualProtector::Win32VirtualProtector(
	::LPVOID address,
	::SIZE_T size,
	Win32VirtualProtectorFlushType flush_type)
{
	if (!address)
	{
		throw Win32ProcessPatcherVirtualProtectorException{"Null address."};
	}

	if (size == 0)
	{
		throw Win32ProcessPatcherVirtualProtectorException{"Size out of range."};
	}

	auto is_flush = false;

	switch (flush_type)
	{
		case Win32VirtualProtectorFlushType::none:
			break;

		case Win32VirtualProtectorFlushType::normal:
			is_flush = true;
			break;

		default:
			throw Win32ProcessPatcherVirtualProtectorException{"Unsupported flush type."};
	}

	auto old_protection_mode = ::DWORD{};

	const auto virtual_protect_result = ::VirtualProtect(
		address,
		size,
		PAGE_READWRITE,
		&old_protection_mode
	);

	if (virtual_protect_result == FALSE)
	{
		throw Win32ProcessPatcherVirtualProtectorException{"Failed to protect a region of committed pages."};
	}

	is_initialized_ = true;
	address_ = address;
	size_ = size;
	old_protection_mode_ = old_protection_mode;
	is_flush_ = is_flush;
}

Win32ProcessPatcherImpl::Win32VirtualProtector::~Win32VirtualProtector()
{
	if (!is_initialized_)
	{
		return;
	}

	auto old_protection_mode = ::DWORD{};

	const auto virtual_protect_result = ::VirtualProtect(
		address_,
		size_,
		old_protection_mode_,
		&old_protection_mode
	);

#ifdef NDEBUG
		std::ignore = virtual_protect_result;
#else
	assert(virtual_protect_result != FALSE);
#endif // NDEBUG

	if (is_flush_)
	{
		const auto process_handle = ::GetCurrentProcess();

		const auto flush_instruction_cache_result = ::FlushInstructionCache(
			process_handle,
			address_,
			size_
		);

#ifdef NDEBUG
		std::ignore = flush_instruction_cache_result;
#else
		assert(flush_instruction_cache_result != FALSE);
#endif // NDEBUG
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class Win32ProcessPatcherException :
	public Exception
{
public:
	explicit Win32ProcessPatcherException(
		const char* message)
		:
		Exception{"WIN32_PROCESS_PATCHER", message}
	{
	}
}; // Win32ProcessPatcherException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

Win32ProcessPatcherImpl::Win32ProcessPatcherImpl(
	const Patch& patch)
	:
	patch_{patch}
{
	PatchValidator::validate_patch(patch_);

	image_base_ = reinterpret_cast<::BYTE*>(process::get_module_address(patch.file_name));

	if (!image_base_)
	{
		return;
	}

	if (false)
	{
	}
	else if (has_patch_blocks(patch_.patch_blocks, &PatchBlock::unpatched_bytes))
	{
		status_ = PatchStatus::unpatched;
	}
	else if (has_patch_blocks(patch_.patch_blocks, &PatchBlock::patched_bytes))
	{
		status_ = PatchStatus::patched;
	}
}

PatchStatus Win32ProcessPatcherImpl::get_status() const noexcept
{
	return status_;
}

void Win32ProcessPatcherImpl::apply()
{
	switch (status_)
	{
		case PatchStatus::patched:
			throw Win32ProcessPatcherException{"Already patched."};

		case PatchStatus::unpatched:
			apply_patch_blocks(patch_.patch_blocks, &PatchBlock::patched_bytes);
			break;

		default:
			throw Win32ProcessPatcherException{"Unsupported process data."};
	}
}

bool Win32ProcessPatcherImpl::has_patch_block(
	const PatchBlock& patch_block,
	PatchBytes PatchBlock::*patch_bytes_selector)
{
	const auto patch_address = image_base_ + patch_block.offset;
	const auto& patch_block_bytes = patch_block.*patch_bytes_selector;

	const auto virtual_protector = Win32VirtualProtector
	{
		patch_address,
		patch_block_bytes.size(),
		Win32VirtualProtectorFlushType{}
	};

	return std::equal(
		patch_block_bytes.cbegin(),
		patch_block_bytes.cend(),
		patch_address
	);
}

bool Win32ProcessPatcherImpl::has_patch_blocks(
	const PatchBlocks& patch_blocks,
	PatchBytes PatchBlock::*patch_bytes_selector) noexcept
try
{
	if (!patch_bytes_selector)
	{
		assert(!"Null patch bytes selector.");
		return false;
	}

	for (const auto& patch_block : patch_blocks)
	{
		if (!has_patch_block(patch_block, patch_bytes_selector))
		{
			return false;
		}
	}

	return true;
}
catch (...)
{
	return false;
}

void Win32ProcessPatcherImpl::apply_patch_block(
	const PatchBlock& patch_block,
	PatchBytes PatchBlock::*patch_bytes_selector)
{
	assert(patch_bytes_selector);

	const auto patch_address = image_base_ + patch_block.offset;
	const auto& patch_block_bytes = patch_block.*patch_bytes_selector;

	const auto virtual_protector = Win32VirtualProtector
	{
		patch_address,
		patch_block_bytes.size(),
		Win32VirtualProtectorFlushType::normal
	};

	std::uninitialized_copy(
		patch_block_bytes.cbegin(),
		patch_block_bytes.cend(),
		patch_address
	);
}

void Win32ProcessPatcherImpl::apply_patch_blocks(
	const PatchBlocks& patch_blocks,
	PatchBytes PatchBlock::*patch_bytes_selector)
{
	if (!patch_bytes_selector)
	{
		throw Win32ProcessPatcherException{"Null patch bytes selector."};
	}

	for (const auto& patch_block : patch_blocks)
	{
		apply_patch_block(patch_block, patch_bytes_selector);
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

ProcessPatcherUPtr make_process_patcher(
	const Patch& patch)
{
	return std::make_unique<Win32ProcessPatcherImpl>(patch);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // _WIN32
