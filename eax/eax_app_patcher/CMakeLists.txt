#[[

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

]]


cmake_minimum_required (VERSION 3.4.3 FATAL_ERROR)
project (eax_app_patcher VERSION 1.0.0 LANGUAGES CXX)


#
# Version.
#
set (EAX_APP_PATCHER_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

#
# Target.
#
add_executable (${PROJECT_NAME} "")


#
# Configure files.
#
if (WIN32)
	configure_file (
		"${PROJECT_SOURCE_DIR}/src/eax_app_patcher.rc.in"
		"${PROJECT_BINARY_DIR}/src/eax_app_patcher.rc"
	)
endif ()


#
# Properties.
#
set_target_properties (
	${PROJECT_NAME}
	PROPERTIES
		CXX_STANDARD 14
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)


#
# Include directories.
#
target_include_directories (
	${PROJECT_NAME}
	PRIVATE
		src
)


#
# Compile definitions.
#
target_compile_definitions (
	${PROJECT_NAME}
	PRIVATE
		"EAX_APP_PATCHER_VERSION=\"${EAX_APP_PATCHER_VERSION}\""
		_UNICODE
		UNICODE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
		$<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)


#
# Compile options.
#
target_compile_options (
	${PROJECT_NAME}
	PRIVATE
		$<$<CXX_COMPILER_ID:MSVC>:-W4> # Warning level 4.
		$<$<CXX_COMPILER_ID:MSVC>:-MP> # Multi-process compilation.
		$<$<CXX_COMPILER_ID:GNU>:-Wall>
		$<$<CXX_COMPILER_ID:GNU>:-Wextra>
		$<$<CXX_COMPILER_ID:GNU>:-pedantic>
)

if (FORCE_STATIC_VCRT)
	if (MSVC)
		target_compile_options (
			${PROJECT_NAME}
			PRIVATE
				$<$<CONFIG:DEBUG>:-MTd>
				$<$<NOT:$<CONFIG:DEBUG>>:-MT>
		)
	endif ()
endif ()


#
# Dependencies.
#
target_link_libraries (
	${PROJECT_NAME}
	PRIVATE
		eax::patch_lib
)

if (ALSOFT_STATIC_LIBGCC)
	target_link_libraries (
		${PROJECT_NAME}
		PRIVATE
			"-static-libgcc"
	)
endif ()

if (ALSOFT_STATIC_STDCXX)
	target_link_libraries (
		${PROJECT_NAME}
		PRIVATE
			"-Wl,--push-state,-Bstatic,-lstdc++,--pop-state"
	)
endif ()

if (ALSOFT_STATIC_WINPTHREAD)
	target_link_libraries (
		${PROJECT_NAME}
		PRIVATE
			"-Wl,--push-state,-Bstatic,-lwinpthread,--pop-state"
	)
endif ()


#
# Custom commands.
#
if (CMAKE_STRIP AND CMAKE_BUILD_TYPE STREQUAL Release)
	add_custom_command (
		TARGET
			${PROJECT_NAME}
		POST_BUILD
			COMMAND ${CMAKE_STRIP} $<TARGET_FILE:${PROJECT_NAME}>
	)
endif ()


#
# Header files.
#
if (WIN32)
	target_sources (
		${PROJECT_NAME}
		PRIVATE
			src/eax_app_patcher_rc.h
	)
endif ()


#
# Source files.
#
target_sources (
	${PROJECT_NAME}
	PRIVATE
		src/eax_app_patcher.cpp
)

if (WIN32)
	target_sources (
		${PROJECT_NAME}
		PRIVATE
			"${PROJECT_BINARY_DIR}/src/eax_app_patcher.rc"
	)
endif ()


#
# Install.
#
if (ALSOFT_INSTALL_UTILS)
	install (
		TARGETS
			${PROJECT_NAME}
		RUNTIME DESTINATION
			"bin"
	)
endif ()
