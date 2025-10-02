cmake_minimum_required(VERSION 3.8...3.28)

# Fallback for using newer policies on CMake <3.12.
if (${CMAKE_VERSION} VERSION_LESS 3.12)
  cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif ()

# Joins arguments and places the results in ${result_var}.
function(join result_var)
  set(result "")
  foreach (arg ${ARGN})
    set(result "${result}${arg}")
  endforeach ()
  set(${result_var} "${result}" PARENT_SCOPE)
endfunction()

# DEPRECATED! Should be merged into add_module_library.
function(enable_module target)
  if (MSVC)
    if(CMAKE_GENERATOR STREQUAL "Ninja")
      # Ninja dyndep expects the .ifc output to be located in a specific relative path
      file(RELATIVE_PATH BMI_DIR "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}.dir")
    else()
      set(BMI_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    file(TO_NATIVE_PATH "${BMI_DIR}/${target}.ifc" BMI)
    target_compile_options(${target}
      PRIVATE /interface /ifcOutput ${BMI}
      INTERFACE /reference fmt=${BMI})
    set_target_properties(${target} PROPERTIES ADDITIONAL_CLEAN_FILES ${BMI})
    set_source_files_properties(${BMI} PROPERTIES GENERATED ON)
  endif ()
endfunction()

# Adds a library compiled with C++20 module support.
# `enabled` is a CMake variables that specifies if modules are enabled.
# If modules are disabled `add_module_library` falls back to creating a
# non-modular library.
#
# Usage:
#   add_module_library(<name> [sources...] FALLBACK [sources...] [IF enabled])
function(add_module_library name)
  cmake_parse_arguments(AML "" "IF" "FALLBACK" ${ARGN})
  set(sources ${AML_UNPARSED_ARGUMENTS})

  add_library(${name} OBJECT)
  set_target_properties(${name} PROPERTIES LINKER_LANGUAGE CXX)

  if (NOT ${${AML_IF}})
    # Create a non-modular library.
    target_sources(${name} PRIVATE ${AML_FALLBACK})
    set_target_properties(${name} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    return()
  endif ()

  # Modules require C++20.
  target_compile_features(${name} PUBLIC cxx_std_20)
  target_sources(${name} PUBLIC FILE_SET CXX_MODULES FILES ${sources})
endfunction()

include(CMakeParseArguments)

# Sets a cache variable with a docstring joined from multiple arguments:
#   set(<variable> <value>... CACHE <type> <docstring>...)
# This allows splitting a long docstring for readability.
function(set_verbose)
  # cmake_parse_arguments is broken in CMake 3.4 (cannot parse CACHE) so use
  # list instead.
  list(GET ARGN 0 var)
  list(REMOVE_AT ARGN 0)
  list(GET ARGN 0 val)
  list(REMOVE_AT ARGN 0)
  list(REMOVE_AT ARGN 0)
  list(GET ARGN 0 type)
  list(REMOVE_AT ARGN 0)
  join(doc ${ARGN})
  set(${var} ${val} CACHE ${type} ${doc})
endfunction()

project(ALSOFT_FMT CXX)

# Get version from base.h
file(READ include/fmt/base.h base_h)
if (NOT base_h MATCHES "FMT_VERSION ([0-9]+)([0-9][0-9])([0-9][0-9])")
  message(FATAL_ERROR "Cannot get FMT_VERSION from base.h.")
endif ()
# Use math to skip leading zeros if any.
math(EXPR CPACK_PACKAGE_VERSION_MAJOR ${CMAKE_MATCH_1})
math(EXPR CPACK_PACKAGE_VERSION_MINOR ${CMAKE_MATCH_2})
math(EXPR CPACK_PACKAGE_VERSION_PATCH ${CMAKE_MATCH_3})
join(FMT_VERSION ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.
                 ${CPACK_PACKAGE_VERSION_PATCH})
message(STATUS "{fmt} version: ${FMT_VERSION}")

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/support/cmake")

include(CheckCXXCompilerFlag)
include(JoinPaths)

function(add_headers VAR)
  set(headers ${${VAR}})
  foreach (header ${ARGN})
    set(headers ${headers} include/fmt/${header})
  endforeach()
  set(${VAR} ${headers} PARENT_SCOPE)
endfunction()

# Define the fmt library, its includes and the needed defines.
set(FMT_HEADERS)
add_headers(FMT_HEADERS args.h base.h chrono.h color.h compile.h core.h format.h
                        format-inl.h os.h ostream.h printf.h ranges.h std.h
                        xchar.h)
set(FMT_SOURCES src/format.cc)

set(ALSOFT_FMT_MODULE OFF)
add_module_library(alsoft.fmt src/fmt.cc FALLBACK
                   ${FMT_SOURCES} ${FMT_HEADERS} README.md ChangeLog.md
                   IF ALSOFT_FMT_MODULE)
target_sources(alsoft.fmt PRIVATE src/os.cc)
add_library(alsoft::fmt ALIAS alsoft.fmt)

target_include_directories(alsoft.fmt PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

set_target_properties(alsoft.fmt PROPERTIES ${ALSOFT_STD_VERSION_PROPS}
  VERSION ${FMT_VERSION} SOVERSION ${CPACK_PACKAGE_VERSION_MAJOR}
  POSITION_INDEPENDENT_CODE TRUE
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  EXCLUDE_FROM_ALL TRUE)

if (MSVC)
  # Unicode support requires compiling with /utf-8.
  target_compile_options(alsoft.fmt PUBLIC $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/utf-8>)
endif ()
