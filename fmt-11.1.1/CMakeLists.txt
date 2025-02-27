cmake_minimum_required(VERSION 3.8...3.28)

# [CR] Heavily modified for OpenAL Soft to avoid clashes if used as a sub-
# project with other uses of fmt.

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

include(CMakeParseArguments)

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

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/support/cmake")

include(CheckCXXCompilerFlag)
include(JoinPaths)

function(add_headers VAR)
  set(headers ${${VAR}})
  foreach (header ${ARGN})
    set(headers ${headers} include/fmt/${header})
  endforeach()
  set(${VAR} ${headers} PARENT_SCOPE)
endfunction()

set(ALSOFT_FMT_DEBUG_POSTFIX d CACHE STRING "Debug library postfix.")

# Define the fmt library, its includes and the needed defines.
add_headers(FMT_HEADERS args.h base.h chrono.h color.h compile.h core.h format.h
                        format-inl.h os.h ostream.h printf.h ranges.h std.h
                        xchar.h)
set(FMT_SOURCES src/format.cc src/os.cc)

add_library(alsoft.fmt OBJECT ${FMT_SOURCES} ${FMT_HEADERS} README.md ChangeLog.md)
add_library(alsoft::fmt ALIAS alsoft.fmt)

if (cxx_std_11 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  target_compile_features(alsoft.fmt PUBLIC cxx_std_11)
else ()
  message(WARNING "Feature cxx_std_11 is unknown for the CXX compiler")
endif ()

target_include_directories(alsoft.fmt PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

set_target_properties(alsoft.fmt PROPERTIES ${ALSOFT_STD_VERSION_PROPS}
  VERSION ${FMT_VERSION} SOVERSION ${CPACK_PACKAGE_VERSION_MAJOR}
  DEBUG_POSTFIX "${ALSOFT_FMT_DEBUG_POSTFIX}"
  POSITION_INDEPENDENT_CODE TRUE
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  EXCLUDE_FROM_ALL TRUE)

if (MSVC)
  # Unicode support requires compiling with /utf-8.
  target_compile_options(alsoft.fmt PUBLIC $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif ()
