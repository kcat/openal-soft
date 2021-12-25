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


#include "eax_file_patcher.h"

#include <cassert>
#include <algorithm>

#include "eax_exception.h"
#include "eax_file.h"
#include "eax_patch_validator.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class FilePatcherException :
	public Exception
{
public:
	explicit FilePatcherException(
		const char* message)
		:
		Exception{"FILE_PATCHER", message}
	{
	}
}; // FilePatcherException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


/// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

FilePatcherImpl::FilePatcherImpl(
	const Patch& patch)
	:
	patch_{patch}
{
	PatchValidator::validate_patch(patch_);

	file_ = make_file(patch_.file_name, FileOpenMode::file_open_mode_read);
	buffer_.reserve(max_patch_block_size);

	if (false)
	{
	}
	else if (has_patch_blocks(patch.patch_blocks, &PatchBlock::unpatched_bytes))
	{
		status_ = PatchStatus::unpatched;
	}
	else if (has_patch_blocks(patch.patch_blocks, &PatchBlock::patched_bytes))
	{
		status_ = PatchStatus::patched;
	}

	file_ = nullptr;

	if (status_ != PatchStatus{})
	{
		file_ = make_file(patch_.file_name, FileOpenMode::file_open_mode_read_write);
	}
}

PatchStatus FilePatcherImpl::get_status() const noexcept
{
	return status_;
}

void FilePatcherImpl::apply()
{
	switch (status_)
	{
		case PatchStatus::patched:
			throw FilePatcherException{"Already patched."};

		case PatchStatus::unpatched:
			apply_patch_blocks(patch_.patch_blocks, &PatchBlock::patched_bytes);
			break;

		default:
			throw FilePatcherException{"Unsupported file data."};
	}
}

void FilePatcherImpl::revert()
{
	switch (status_)
	{
		case PatchStatus::patched:
			apply_patch_blocks(patch_.patch_blocks, &PatchBlock::unpatched_bytes);
			break;

		case PatchStatus::unpatched:
			throw FilePatcherException{"Already unpatched."};

		default:
			throw FilePatcherException{"Unsupported file data."};
	}
}

bool FilePatcherImpl::has_patch_block(
	const PatchBlock& patch_block,
	PatchBytes PatchBlock::*patch_bytes_selector)
{
	const auto& patch_block_bytes = patch_block.*patch_bytes_selector;
	const auto patch_block_bytes_size = static_cast<int>(patch_block_bytes.size());

	file_->set_position(patch_block.offset);
	buffer_.resize(patch_block_bytes_size);

	const auto read_result = file_->read(buffer_.data(), patch_block_bytes_size);

	if (read_result != patch_block_bytes_size)
	{
		return false;
	}

	return std::equal(
		buffer_.cbegin(),
		buffer_.cend(),
		patch_block_bytes.cbegin()
	);
}

bool FilePatcherImpl::has_patch_blocks(
	const PatchBlocks& patch_blocks,
	PatchBytes PatchBlock::*patch_bytes_selector) noexcept
try
{
	if (!patch_bytes_selector)
	{
		assert(!"Null selector.");
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

void FilePatcherImpl::apply_patch_block(
	const PatchBlock& patch_block,
	PatchBytes PatchBlock::*patch_bytes_selector)
{
	assert(patch_bytes_selector);

	file_->set_position(patch_block.offset);

	const auto& patch_block_bytes = patch_block.*patch_bytes_selector;
	const auto patch_block_bytes_size = static_cast<int>(patch_block_bytes.size());
	const auto write_result = file_->write(patch_block_bytes.data(), patch_block_bytes_size);

	if (write_result != patch_block_bytes_size)
	{
		throw FilePatcherException{"I/O write error."};
	}
}

void FilePatcherImpl::apply_patch_blocks(
	const PatchBlocks& patch_blocks,
	PatchBytes PatchBlock::*patch_bytes_selector)
{
	if (!patch_bytes_selector)
	{
		throw FilePatcherException{"Null patch bytes selector."};
	}

	for (const auto& patch_block : patch_blocks)
	{
		apply_patch_block(patch_block, patch_bytes_selector);
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

FilePatcherUPtr make_file_patcher(
	const Patch& patch)
{
	return std::make_unique<FilePatcherImpl>(patch);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
