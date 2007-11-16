# CMake build file list for OpenAL

INCLUDE(CheckLibraryExists)
INCLUDE(CheckIncludeFile)
INCLUDE(CheckCCompilerFlag)
INCLUDE(CheckCSourceCompiles)
INCLUDE(CheckTypeSize)


PROJECT(OpenAL)


SET(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS TRUE)


OPTION(ALSA    "ALSA backend"         ON)
OPTION(OSS     "OSS backend"          ON)
OPTION(DSOUND  "DirectSound backend"  ON)

OPTION(DLOPEN  "Use the dlopen API for loading optional libs"  ON)

OPTION(DEBUG    "Build lib in debug mode"                   OFF)
OPTION(NODEBUG  "Disable all debug info for optimizations"  OFF)
OPTION(WERROR   "Treat compile warnings as errors"          OFF)


SET(LIB_MAJOR_VERSION "1")
SET(LIB_MINOR_VERSION "0")
SET(LIB_BUILD_VERSION "0")
SET(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_BUILD_VERSION}")


IF("${WIN32}")
    SET(LIBNAME openal32)
ELSE()
    SET(LIBNAME openal)
ENDIF()


CHECK_TYPE_SIZE("long" SIZEOF_LONG)
CHECK_TYPE_SIZE("long long" SIZEOF_LONG_LONG)
CHECK_TYPE_SIZE("unsigned int" SIZEOF_UINT)
CHECK_TYPE_SIZE("void*" SIZEOF_VOIDP)


# Add definitions, compiler switches, etc.
INCLUDE_DIRECTORIES(OpenAL32/Include include "${OpenAL_BINARY_DIR}")
SET(EXTRA_LIBS m ${EXTRA_LIBS})

IF("${MSVC}")
    # ???
ELSE()
    ADD_DEFINITIONS(-W -Wall)
    IF(WERROR)
        ADD_DEFINITIONS(-Werror)
    ENDIF()
    IF(DEBUG)
        ADD_DEFINITIONS(-g3)
    ELSEIF(NODEBUG)
        ADD_DEFINITIONS(-O2 -funroll-loops -fomit-frame-pointer)
    ELSE()
        ADD_DEFINITIONS(-g -O2 -funroll-loops)
    ENDIF()

    # The mixer doesn't like GCC's strict aliasing optimizations. Make sure
    # it's turned off
    CHECK_C_COMPILER_FLAG(-fstrict-aliasing HAVE_STRICT_ALIASING)
    IF("${HAVE_STRICT_ALIASING}")
        ADD_DEFINITIONS(-fno-strict-aliasing)
    ENDIF()

    # Set visibility options if available
    IF(NOT "${WIN32}")
        CHECK_C_SOURCE_COMPILES("int foo() __attribute__((destructor));
                                 int main() {return 0;}" HAVE_GCC_DESTRUCTOR)

        CHECK_C_SOURCE_COMPILES("int foo() __attribute__((visibility(\"default\")));
                                 int main() {return 0;}" HAVE_GCC_VISIBILITY)
        IF("${HAVE_GCC_VISIBILITY}")
            ADD_DEFINITIONS(-fvisibility=hidden -DHAVE_GCC_VISIBILITY)
        ENDIF()
    ENDIF()
ENDIF()

CHECK_LIBRARY_EXISTS(m  sqrtf  "" HAVE_SQRTF)
CHECK_LIBRARY_EXISTS("" strtof "" HAVE_STRTOF)

CHECK_LIBRARY_EXISTS("" strcasecmp "" HAVE_STRCASECMP)
IF(NOT "${HAVE_STRCASECMP}")
    CHECK_LIBRARY_EXISTS("" _stricmp "" HAVE__STRICMP)
    IF(NOT "${HAVE__STRICMP}")
        MESSAGE(FATAL_ERROR "No case-insensitive compare function found, please report!")
    ENDIF()

    ADD_DEFINITIONS(-Dstrcasecmp=_stricmp)
ENDIF()

# Check for the dlopen API (for dynamicly loading backend libs)
IF(DLOPEN)
    CHECK_INCLUDE_FILE(dlfcn.h HAVE_DLFCN_H)
    IF("${HAVE_DLFCN_H}")
        CHECK_LIBRARY_EXISTS(dl dlopen "" HAVE_LIBDL)
        IF("${HAVE_LIBDL}")
            SET(EXTRA_LIBS dl ${EXTRA_LIBS})
        ENDIF()
    ENDIF()
ENDIF()

# Check if we have Windows headers
CHECK_INCLUDE_FILE(windows.h HAVE_WINDOWS_H)
IF(NOT "${HAVE_WINDOWS_H}")
    # We need pthreads outside of Windows
    CHECK_INCLUDE_FILE(pthread.h HAVE_PTHREAD_H)
    IF(NOT "${HAVE_PTHREAD_H}")
        MESSAGE(FATAL_ERROR "PThreads is required for non-Windows builds!")
    ENDIF()

    # _GNU_SOURCE is needed on some systems for extra attributes, and
    # _REENTRANT is needed for libc thread-safety
    ADD_DEFINITIONS(-D_GNU_SOURCE=1 -D_REENTRANT)
    CHECK_LIBRARY_EXISTS(pthread pthread_create "" HAVE_LIBPTHREAD)
    IF("${HAVE_LIBPTHREAD}")
        SET(EXTRA_LIBS pthread ${EXTRA_LIBS})
    ENDIF()
ENDIF()

# Check for a 64-bit type
CHECK_INCLUDE_FILE(stdint.h HAVE_STDINT_H)
IF(NOT "${HAVE_STDINT_H}")
    IF("${HAVE_WINDOWS_H}")
        CHECK_C_SOURCE_COMPILES("\#include <windows.h>
                                 __int64 foo;
                                 int main() {return 0;}" HAVE___INT64)
    ENDIF()
    IF(NOT "${HAVE___INT64}")
        IF(NOT "${SIZEOF_LONG}" MATCHES "8")
            IF(NOT "${SIZEOF_LONG_LONG}" MATCHES "8")
                MESSAGE(FATAL_ERROR "No 64-bit types found, please report!")
            ENDIF()
        ENDIF()
    ENDIF()
ENDIF()


SET(OPENAL_OBJS  OpenAL32/alBuffer.c
                 OpenAL32/alError.c
                 OpenAL32/alExtension.c
                 OpenAL32/alListener.c
                 OpenAL32/alSource.c
                 OpenAL32/alState.c
                 OpenAL32/alThunk.c
                 OpenAL32/OpenAL32.c
)
SET(ALC_OBJS  Alc/ALc.c
              Alc/ALu.c
              Alc/alcConfig.c
              Alc/alcThread.c
)

SET(BACKENDS "")

# Check ALSA backend
IF(ALSA)
    CHECK_INCLUDE_FILE(alsa/asoundlib.h HAVE_ALSA_ASOUNDLIB_H)
    IF("${HAVE_ALSA_ASOUNDLIB_H}")
        CHECK_LIBRARY_EXISTS(asound snd_pcm_open "" HAVE_LIBASOUND)
        IF("${HAVE_DLFCN_H}" OR "${HAVE_LIBASOUND}")
            SET(HAVE_ALSA 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/alsa.c)
            IF("${HAVE_DLFCN_H}")
                SET(BACKENDS  ${BACKENDS} ALSA)
            ELSE()
                SET(BACKENDS  ${BACKENDS} ALSA\(linked\))
                SET(EXTRA_LIBS asound ${EXTRA_LIBS})
            ENDIF()
        ENDIF()
    ENDIF()
ENDIF()

# Check OSS backend
IF(OSS)
    CHECK_INCLUDE_FILE(sys/soundcard.h HAVE_SYS_SOUNDCARD_H)
    IF("${HAVE_SYS_SOUNDCARD_H}")
        SET(HAVE_OSS 1)
        SET(ALC_OBJS  ${ALC_OBJS} Alc/oss.c)
        SET(BACKENDS  ${BACKENDS} OSS)
    ENDIF()
ENDIF()

# Check DSound/MMSystem backend
IF(DSOUND)
    IF("${HAVE_WINDOWS_H}")
        SET(CMAKE_REQUIRED_LIBRARIES ole32)
        CHECK_C_SOURCE_COMPILES("int main() {return 0;}" HAVE_LIBOLE32)
        SET(CMAKE_REQUIRED_LIBRARIES "")
#        CHECK_LIBRARY_EXISTS(ole32 CoInitialize "" HAVE_LIBOLE32)
        IF("${HAVE_LIBOLE32}")
            SET(EXTRA_LIBS ole32 ${EXTRA_LIBS})
        ENDIF()

        CHECK_INCLUDE_FILE(dsound.h HAVE_DSOUND_H)
        IF("${HAVE_DSOUND_H}")
            SET(HAVE_DSOUND 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/dsound.c)
            SET(BACKENDS  ${BACKENDS} DirectSound)

            CHECK_LIBRARY_EXISTS(dxguid IID_IDirectSound "" HAVE_LIBDXGUID)
            IF("${HAVE_LIBDXGUID}")
                SET(EXTRA_LIBS dxguid ${EXTRA_LIBS})
            ENDIF()
        ENDIF()

        CHECK_C_SOURCE_COMPILES("\#include <windows.h>
                                 \#include <mmsystem.h>
                                 int main() {return 0;}" HAVE_MMSYSTEM_H)
        IF("${HAVE_MMSYSTEM_H}")
            SET(HAVE_WINMM 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/winmm.c)
            SET(BACKENDS  ${BACKENDS} WinMM)

            SET(CMAKE_REQUIRED_LIBRARIES winmm)
            CHECK_C_SOURCE_COMPILES("int main() {return 0;}" HAVE_LIBWINMM)
            SET(CMAKE_REQUIRED_LIBRARIES "")
#            CHECK_LIBRARY_EXISTS(winmm waveInOpen "" HAVE_LIBWINMM)
            IF("${HAVE_LIBWINMM}")
                SET(EXTRA_LIBS winmm ${EXTRA_LIBS})
            ENDIF()
        ENDIF()
    ENDIF()
ENDIF()

# End configuration
CONFIGURE_FILE(
    "${OpenAL_SOURCE_DIR}/config.h.in"
    "${OpenAL_BINARY_DIR}/config.h")

ADD_DEFINITIONS(-DAL_BUILD_LIBRARY)

# Build a shared library
ADD_LIBRARY(${LIBNAME} SHARED ${OPENAL_OBJS} ${ALC_OBJS})
SET_TARGET_PROPERTIES(${LIBNAME} PROPERTIES VERSION ${LIB_VERSION} SOVERSION ${LIB_MAJOR_VERSION})

TARGET_LINK_LIBRARIES(${LIBNAME} ${EXTRA_LIBS})

# Add an install target here
INSTALL(TARGETS ${LIBNAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)
INSTALL(FILES include/AL/al.h
              include/AL/alc.h
        DESTINATION include/AL
)

MESSAGE(STATUS "")
MESSAGE(STATUS "Building OpenAL with support for the following backends:")
MESSAGE(STATUS "    ${BACKENDS}")
MESSAGE(STATUS "")
