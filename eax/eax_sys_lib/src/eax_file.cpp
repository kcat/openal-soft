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


#include "eax_file.h"

#include <cassert>

#include <algorithm>
#include <fstream>

#include "eax_exception.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class FileException :
	public Exception
{
public:
	explicit FileException(
		const char* message)
		:
		Exception{"FILE", message}
	{
	}
}; // FileException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class FileImpl final :
	public File
{
public:
	FileImpl(
		const char* path,
		FileOpenMode open_mode);


	void set_position(
		int position) override;


	int read(
		void* buffer,
		int size) override;

	int write(
		const void* buffer,
		int size) override;


private:
	std::filebuf filebuf_{};


	bool is_open() const;

	void ensure_is_open();
}; // FileImpl

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

FileImpl::FileImpl(
	const char* path,
	FileOpenMode open_mode)
{
	if (!path || path[0] == '\0')
	{
		throw FileException{"Null or empty path."};
	}

	const auto is_readable = (open_mode & file_open_mode_read) != 0;
	const auto is_writable = (open_mode & file_open_mode_write) != 0;
	const auto is_truncate = (open_mode & file_open_mode_truncate) != 0;

	std::ios_base::openmode std_mode{};

	if (is_readable)
	{
		std_mode |= std::ios_base::in;
	}

	if (is_writable)
	{
		std_mode |= std::ios_base::out;
	}

	if (is_truncate)
	{
		std_mode |= std::ios_base::trunc;
	}

	filebuf_.open(path, std_mode);

	if (!is_open())
	{
		throw FileException{"Failed to open file."};
	}
}

void FileImpl::set_position(
	int position)
{
	ensure_is_open();

	const auto std_position = filebuf_.pubseekpos(position);

	if (std_position < 0)
	{
		throw FileException{"Failed to set position."};
	}
}

int FileImpl::read(
	void* buffer,
	int size)
{
	assert(buffer);
	assert(size >= 0);

	ensure_is_open();

	const auto std_read_size = filebuf_.sgetn(static_cast<char*>(buffer), size);

	if (std_read_size < 0)
	{
		throw FileException{"I/O read error."};
	}

	return static_cast<int>(std_read_size);
}

int FileImpl::write(
	const void* buffer,
	int size)
{
	assert(buffer);
	assert(size >= 0);

	ensure_is_open();

	const auto std_written_size = filebuf_.sputn(static_cast<const char*>(buffer), size);

	if (std_written_size < 0)
	{
		throw FileException{"I/O write error."};
	}

	return static_cast<int>(std_written_size);
}

bool FileImpl::is_open() const
{
	return filebuf_.is_open();
}

void FileImpl::ensure_is_open()
{
	if (!is_open())
	{
		throw FileException{"Not open."};
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

FileUPtr make_file(
	const char* path,
	FileOpenMode open_mode)
{
	return std::make_unique<FileImpl>(path, open_mode);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
