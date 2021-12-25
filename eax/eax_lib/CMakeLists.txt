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
project (eax_lib VERSION 1.0.7 LANGUAGES CXX)


#
# Target.
#
add_library (${PROJECT_NAME} STATIC "")
add_library (eax::eax_lib ALIAS ${PROJECT_NAME})


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
		"${CMAKE_SOURCE_DIR}/include"
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	PRIVATE
		src
)


#
# Compile definitions.
#
target_compile_definitions (
	${PROJECT_NAME}
	PRIVATE
		"EAX_VERSION=\"${EAX_VERSION}\""
		_UNICODE
		UNICODE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
		AL_ALEXT_PROTOTYPES
		"AL_API=${EXPORT_DECL}"
		"ALC_API=${EXPORT_DECL}"
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
		$<$<CXX_COMPILER_ID:GNU>:-fPIC>
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
	PUBLIC
		eax::sys_lib
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
# Header files.
#
target_sources (
	${PROJECT_NAME}
	PRIVATE
		include/eax_globals.h
		include/eax_utils.h

		include/eax_al_api.h
		include/eax_al_api_context.h
		include/eax_al_low_pass_param.h
		include/eax_al_object.h

		include/eax_api.h

		include/eax_eaxx.h
		include/eax_eaxx_context.h
		include/eax_eaxx_context_shared.h
		include/eax_eaxx_eax_call.h
		include/eax_eaxx_fx_slot.h
		include/eax_eaxx_fx_slot_index.h
		include/eax_eaxx_fx_slots.h
		include/eax_eaxx_source.h
		include/eax_eaxx_validators.h

		include/eax_eaxx_effect.h
		include/eax_eaxx_null_effect.h
		include/eax_eaxx_auto_wah_effect.h
		include/eax_eaxx_chorus_effect.h
		include/eax_eaxx_compressor_effect.h
		include/eax_eaxx_distortion_effect.h
		include/eax_eaxx_eax_reverb_effect.h
		include/eax_eaxx_echo_effect.h
		include/eax_eaxx_equalizer_effect.h
		include/eax_eaxx_flanger_effect.h
		include/eax_eaxx_frequency_shifter_effect.h
		include/eax_eaxx_pitch_shifter_effect.h
		include/eax_eaxx_ring_modulator_effect.h
		include/eax_eaxx_vocal_morpher_effect.h
)


#
# Source files.
#
target_sources (
	${PROJECT_NAME}
	PRIVATE
		src/eax_globals.cpp
		src/eax_utils.cpp

		src/eax_al_api.cpp
		src/eax_al_api_context.cpp
		src/eax_al_low_pass_param.cpp
		src/eax_al_object.cpp

		src/eax_api.cpp

		src/eax_eaxx.cpp
		src/eax_eaxx_context.cpp
		src/eax_eaxx_context_shared.cpp
		src/eax_eaxx_eax_call.cpp
		src/eax_eaxx_fx_slot.cpp
		src/eax_eaxx_fx_slot_index.cpp
		src/eax_eaxx_fx_slots.cpp
		src/eax_eaxx_source.cpp

		src/eax_eaxx_effect.cpp
		src/eax_eaxx_null_effect.cpp
		src/eax_eaxx_auto_wah_effect.cpp
		src/eax_eaxx_chorus_effect.cpp
		src/eax_eaxx_compressor_effect.cpp
		src/eax_eaxx_distortion_effect.cpp
		src/eax_eaxx_eax_reverb_effect.cpp
		src/eax_eaxx_echo_effect.cpp
		src/eax_eaxx_equalizer_effect.cpp
		src/eax_eaxx_flanger_effect.cpp
		src/eax_eaxx_frequency_shifter_effect.cpp
		src/eax_eaxx_pitch_shifter_effect.cpp
		src/eax_eaxx_ring_modulator_effect.cpp
		src/eax_eaxx_vocal_morpher_effect.cpp
)
