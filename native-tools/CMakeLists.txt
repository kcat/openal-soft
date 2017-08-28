cmake_minimum_required(VERSION 3.0.2)

project(native-tools)

include(CheckLibraryExists)

check_library_exists(m pow "" HAVE_LIBM)

add_executable(bin2h bin2h.c)
# Enforce no dressing for executable names, so the main script can find it
set_target_properties(bin2h PROPERTIES OUTPUT_NAME bin2h)
# Avoid configuration-dependent subdirectories while building with Visual Studio
set_target_properties(bin2h PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set_target_properties(bin2h PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")

add_executable(bsincgen bsincgen.c)
set_target_properties(bsincgen PROPERTIES OUTPUT_NAME bsincgen)
set_target_properties(bsincgen PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set_target_properties(bsincgen PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
if(HAVE_LIBM)
    target_link_libraries(bsincgen m)
endif(HAVE_LIBM)
if(WIN32 AND CMAKE_COMPILER_IS_GNUCC)
    set_property(TARGET bsincgen APPEND_STRING PROPERTY LINK_FLAGS " -municode")
endif(WIN32 AND CMAKE_COMPILER_IS_GNUCC)
