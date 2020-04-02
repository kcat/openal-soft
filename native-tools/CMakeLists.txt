cmake_minimum_required(VERSION 3.0.2)

project(native-tools C)

include(CheckLibraryExists)

set(CPP_DEFS )
if(WIN32)
    set(CPP_DEFS ${CPP_DEFS} _WIN32)
endif(WIN32)

add_executable(bin2h bin2h.c)
# Enforce no dressing for executable names, so the main script can find it
set_target_properties(bin2h PROPERTIES OUTPUT_NAME bin2h)
# Avoid configuration-dependent subdirectories while building with Visual Studio
set_target_properties(bin2h PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set_target_properties(bin2h PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
target_compile_definitions(bin2h PRIVATE ${CPP_DEFS})
