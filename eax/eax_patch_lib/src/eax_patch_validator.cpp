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


#ifndef EAX_FILE_PATCHER_INCLUDED
#define EAX_FILE_PATCHER_INCLUDED


#include "eax_patch_validator.h"

#include "eax_exception.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class PatchValidatorException :
	public Exception
{
public:
	explicit PatchValidatorException(
		const char* message)
		:
		Exception{"PATCH_VALIDATOR", message}
	{
	}
}; // PatchValidatorException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

void PatchValidator::validate_patch_block(
	const PatchBlock& patch_block)
{
	if (patch_block.offset < 0)
	{
		throw PatchValidatorException{"Patch block offset out of range."};
	}

	if (patch_block.unpatched_bytes.size() <= 0 ||
		patch_block.unpatched_bytes.size() > max_patch_block_size)
	{
		throw PatchValidatorException{"Unpatched bytes size out of range."};
	}

	if (patch_block.patched_bytes.size() <= 0 ||
		patch_block.patched_bytes.size() > max_patch_block_size)
	{
		throw PatchValidatorException{"Patched bytes size out of range."};
	}

	if (patch_block.unpatched_bytes.size() != patch_block.patched_bytes.size())
	{
		throw PatchValidatorException{"Patch block bytes size mismatch."};
	}
}

void PatchValidator::validate_patch_blocks(
	const PatchBlocks& patch_blocks)
{
	if (patch_blocks.size() <= 0)
	{
		throw PatchValidatorException{"Patch block count out of range."};
	}

	for (const auto& patch_block : patch_blocks)
	{
		validate_patch_block(patch_block);
	}
}

void PatchValidator::validate_patch(
	const Patch& patch)
{
	if (!patch.name || patch.name[0] == '\0')
	{
		throw PatchValidatorException{"Null or empty patch name"};
	}

	if (!patch.file_name || patch.file_name[0] == '\0')
	{
		throw PatchValidatorException{"Null or empty patch file name"};
	}

	if (!patch.description || patch.description[0] == '\0')
	{
		throw PatchValidatorException{"Null or empty patch description"};
	}

	validate_patch_blocks(patch.patch_blocks);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_FILE_PATCHER_INCLUDED
