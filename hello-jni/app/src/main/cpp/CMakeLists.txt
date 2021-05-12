cmake_minimum_required(VERSION 3.4.1)

# begin openal-soft
function(configure_target_outdir target)
    set_target_properties(${target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        FOLDER "External"
    )
endfunction(configure_lib_outdir)

set(ALSOFT_DLOPEN OFF CACHE BOOL "Check for the dlopen API for loading optional libs" FORCE)
set(ALSOFT_UTILS OFF CACHE BOOL "Build utility program" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "Install main library" FORCE)
add_subdirectory(../../../../../)
configure_target_outdir(common)
configure_target_outdir(OpenAL)
# end openal-soft

add_library(hello-jni SHARED
            hello-jni.c)

# Include libraries needed for hello-jni lib
target_link_libraries(hello-jni
                      android
                      log
                      OpenAL)
