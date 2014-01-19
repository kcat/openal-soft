# CMake build file list for OpenAL

CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

PROJECT(OpenAL)

IF(COMMAND CMAKE_POLICY)
  CMAKE_POLICY(SET CMP0003 NEW)
  CMAKE_POLICY(SET CMP0005 NEW)
ENDIF(COMMAND CMAKE_POLICY)

SET(CMAKE_MODULE_PATH "${OpenAL_SOURCE_DIR}/cmake")

INCLUDE(CheckFunctionExists)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckSharedFunctionExists)
INCLUDE(CheckIncludeFile)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckSymbolExists)
INCLUDE(CheckCCompilerFlag)
INCLUDE(CheckCSourceCompiles)
INCLUDE(CheckTypeSize)


SET(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS TRUE)


OPTION(ALSOFT_CPUEXT_SSE  "Check for SSE/SSE2 support" ON)
OPTION(ALSOFT_CPUEXT_NEON "Check for ARM Neon support" ON)

OPTION(ALSOFT_REQUIRE_SSE  "Require SSE/SSE2 support" OFF)
OPTION(ALSOFT_REQUIRE_NEON "Require ARM Neon support" OFF)

IF(WIN32)
    # This option is mainly for static linking OpenAL Soft into another project
    # that already defines the IDs. It is up to that project to ensure all
    # required IDs are defined.
    OPTION(ALSOFT_NO_UID_DEFS "Do not define GUIDs, IIDs, CLSIDs, or PropertyKeys" OFF)
ENDIF()


OPTION(ALSOFT_BACKEND_ALSA    "Check for ALSA backend"                ON)
OPTION(ALSOFT_BACKEND_OSS     "Check for OSS backend"                 ON)
OPTION(ALSOFT_BACKEND_SOLARIS "Check for Solaris backend"             ON)
OPTION(ALSOFT_BACKEND_SNDIO   "Check for SndIO backend"               ON)
OPTION(ALSOFT_BACKEND_QSA     "Check for QSA backend"                 ON)
OPTION(ALSOFT_BACKEND_MMDEVAPI "Check for MMDevApi backend"           ON)
OPTION(ALSOFT_BACKEND_DSOUND   "Check for DirectSound backend"        ON)
OPTION(ALSOFT_BACKEND_WINMM    "Check for Windows Multimedia backend" ON)
OPTION(ALSOFT_BACKEND_PORTAUDIO  "Check for PortAudio backend"        ON)
OPTION(ALSOFT_BACKEND_PULSEAUDIO "Check for PulseAudio backend"       ON)
OPTION(ALSOFT_BACKEND_COREAUDIO  "Check for CoreAudio backend"        ON)
OPTION(ALSOFT_BACKEND_OPENSL     "Check for OpenSL backend"           ON)
OPTION(ALSOFT_BACKEND_WAVE "Enable Wave Writer backend"               ON)

OPTION(ALSOFT_MIDI_FLUIDSYNTH "Check for FluidSynth MIDI"             ON)

OPTION(ALSOFT_REQUIRE_ALSA       "Require ALSA backend"               OFF)
OPTION(ALSOFT_REQUIRE_OSS        "Require OSS backend"                OFF)
OPTION(ALSOFT_REQUIRE_SOLARIS    "Require Solaris backend"            OFF)
OPTION(ALSOFT_REQUIRE_SNDIO      "Require SndIO backend"              OFF)
OPTION(ALSOFT_REQUIRE_QSA        "Require QSA backend"                OFF)
OPTION(ALSOFT_REQUIRE_MMDEVAPI   "Require MMDevApi"                   OFF)
OPTION(ALSOFT_REQUIRE_DSOUND     "Require DirectSound backend"        OFF)
OPTION(ALSOFT_REQUIRE_WINMM      "Require Windows Multimedia backend" OFF)
OPTION(ALSOFT_REQUIRE_PORTAUDIO  "Require PortAudio backend"          OFF)
OPTION(ALSOFT_REQUIRE_PULSEAUDIO "Require PulseAudio backend"         OFF)
OPTION(ALSOFT_REQUIRE_COREAUDIO  "Require CoreAudio backend"          OFF)
OPTION(ALSOFT_REQUIRE_OPENSL     "Require OpenSL backend"             OFF)

OPTION(ALSOFT_REQUIRE_FLUIDSYNTH "Require FluidSynth MIDI"            OFF)

OPTION(ALSOFT_DLOPEN  "Check for the dlopen API for loading optional libs"  ON)

OPTION(ALSOFT_WERROR  "Treat compile warnings as errors"      OFF)

OPTION(ALSOFT_UTILS          "Build and install utility programs"         ON)
OPTION(ALSOFT_NO_CONFIG_UTIL "Disable building the alsoft-config utility" OFF)

OPTION(ALSOFT_EXAMPLES  "Build and install example programs"  ON)

OPTION(ALSOFT_CONFIG "Install alsoft.conf sample configuration file" ON)


IF(WIN32)
    SET(LIBNAME OpenAL32)
    ADD_DEFINITIONS("-D_WIN32 -D_WIN32_WINNT=0x0501")
ELSE()
    SET(LIBNAME openal)
ENDIF()

# QNX's gcc do not uses /usr/include and /usr/lib pathes by default
IF ("${CMAKE_C_PLATFORM_ID}" STREQUAL "QNX")
    ADD_DEFINITIONS("-I/usr/include")
    SET(EXTRA_LIBS ${EXTRA_LIBS} -L/usr/lib)
ENDIF()

IF(NOT LIBTYPE)
    SET(LIBTYPE SHARED)
ENDIF()

SET(LIB_MAJOR_VERSION "1")
SET(LIB_MINOR_VERSION "15")
SET(LIB_REVISION "1")
SET(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_REVISION}")

SET(EXPORT_DECL "")
SET(ALIGN_DECL "")


CHECK_TYPE_SIZE("long" SIZEOF_LONG)
CHECK_TYPE_SIZE("long long" SIZEOF_LONG_LONG)


CHECK_C_COMPILER_FLAG(-std=c99 HAVE_STD_C99)
IF(HAVE_STD_C99)
    SET(CMAKE_C_FLAGS "-std=c99 ${CMAKE_C_FLAGS}")
ENDIF()

# MSVC may need workarounds for C99 restrict and inline
IF(MSVC)
    # TODO: Once we truly require C99, these restrict and inline checks should go
    # away.
    CHECK_C_SOURCE_COMPILES("int *restrict foo;
                             int main() {return 0;}" HAVE_RESTRICT)
    IF(NOT HAVE_RESTRICT)
        # Slightly convoluted way to do this, because MSVC may barf if restrict is
        # defined to __restrict.
        CHECK_C_SOURCE_COMPILES("#define restrict __restrict
                                 #include <stdlib.h>
                                 int *restrict foo;
                                 int main() {return 0;}" HAVE___RESTRICT)
        IF(HAVE___RESTRICT)
            ADD_DEFINITIONS(-Drestrict=__restrict)
            SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Drestrict=__restrict")
        ELSE()
            ADD_DEFINITIONS("-Drestrict=")
            SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Drestrict=")
        ENDIF()
    ENDIF()

    CHECK_C_SOURCE_COMPILES("inline void foo(void) { }
                             int main() {return 0;}" HAVE_INLINE)
    IF(NOT HAVE_INLINE)
        CHECK_C_SOURCE_COMPILES("__inline void foo(void) { }
                                 int main() {return 0;}" HAVE___INLINE)
        IF(NOT HAVE___INLINE)
            MESSAGE(FATAL_ERROR "No inline keyword found, please report!")
        ENDIF()

        ADD_DEFINITIONS(-Dinline=__inline)
        SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Dinline=__inline")
    ENDIF()
ENDIF()

# Make sure we have C99-style inline semantics with GCC (4.3 or newer).
IF(CMAKE_COMPILER_IS_GNUCC)
    SET(OLD_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    # Force no inlining for the next test.
    SET(CMAKE_REQUIRED_FLAGS "${OLD_REQUIRED_FLAGS} -fno-inline")

    CHECK_C_SOURCE_COMPILES("extern inline int foo() { return 0; }
                             int main() {return foo();}" INLINE_IS_C99)
    IF(NOT INLINE_IS_C99)
        MESSAGE(FATAL_ERROR "Your compiler does not seem to have C99 inline semantics!
                             Please update your compiler for better C99 compliance.")
    ENDIF()

    SET(CMAKE_REQUIRED_FLAGS "${OLD_REQUIRED_FLAGS}")
ENDIF()

# Add definitions, compiler switches, etc.
INCLUDE_DIRECTORIES("${OpenAL_SOURCE_DIR}/include" "${OpenAL_BINARY_DIR}")
IF(CMAKE_VERSION VERSION_LESS "2.8.8")
    INCLUDE_DIRECTORIES("${OpenAL_SOURCE_DIR}/OpenAL32/Include" "${OpenAL_SOURCE_DIR}/Alc")
    IF(WIN32 AND ALSOFT_NO_UID_DEFS)
        ADD_DEFINITIONS("-DAL_NO_UID_DEFS")
    ENDIF()
ENDIF()

IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
ENDIF()
IF(NOT CMAKE_DEBUG_POSTFIX)
    SET(CMAKE_DEBUG_POSTFIX "" CACHE STRING
        "Library postfix for debug builds. Normally left blank."
        FORCE)
ENDIF()

IF(MSVC)
    # ???
    SET(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -D_DEBUG")
    SET(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -DNDEBUG")
    SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -DNDEBUG")
    SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG")
    ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
    ADD_DEFINITIONS(-D_CRT_NONSTDC_NO_DEPRECATE)
    ADD_DEFINITIONS("/wd4098")

    IF(NOT DXSDK_DIR)
        STRING(REGEX REPLACE "\\\\" "/" DXSDK_DIR "$ENV{DXSDK_DIR}")
    ELSE()
        STRING(REGEX REPLACE "\\\\" "/" DXSDK_DIR "${DXSDK_DIR}")
    ENDIF()
    IF(DXSDK_DIR)
        MESSAGE(STATUS "Using DirectX SDK directory: ${DXSDK_DIR}")
        SET(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES} "${DXSDK_DIR}/Include")
        INCLUDE_DIRECTORIES("${DXSDK_DIR}/Include")
        LINK_DIRECTORIES("${DXSDK_DIR}/Lib")
    ENDIF()

    OPTION(FORCE_STATIC_VCRT "Force /MT for static VC runtimes" OFF)
    IF(FORCE_STATIC_VCRT)
        FOREACH(flag_var
                CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
                CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
            IF(${flag_var} MATCHES "/MD")
                STRING(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
            ENDIF()
        ENDFOREACH(flag_var)
    ENDIF()
ELSE()
    ADD_DEFINITIONS(-Winline -Wall)
    CHECK_C_COMPILER_FLAG(-Wextra HAVE_W_EXTRA)
    IF(HAVE_W_EXTRA)
        ADD_DEFINITIONS(-Wextra)
    ENDIF()

    IF(ALSOFT_WERROR)
        ADD_DEFINITIONS(-Werror)
    ENDIF()

    SET(CMAKE_C_FLAGS_RELWITHDEBINFO "-g -O2 -D_DEBUG" CACHE STRING
        "Flags used by the compiler during Release with Debug Info builds."
        FORCE)
    SET(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG" CACHE STRING
        "Flags used by the compiler during release minsize builds."
        FORCE)
    SET(CMAKE_C_FLAGS_RELEASE "-O2 -fomit-frame-pointer -DNDEBUG" CACHE STRING
        "Flags used by the compiler during release builds"
        FORCE)
    SET(CMAKE_C_FLAGS_DEBUG "-g3 -D_DEBUG" CACHE STRING
        "Flags used by the compiler during debug builds."
        FORCE)

    CHECK_C_SOURCE_COMPILES("int foo() __attribute__((destructor));
                             int main() {return 0;}" HAVE_GCC_DESTRUCTOR)
ENDIF()

# Set visibility/export options if available
IF(WIN32)
    SET(EXPORT_DECL "__declspec(dllexport)")
    IF(NOT MINGW)
        SET(ALIGN_DECL "__declspec(align(x))")
    ELSE()
        SET(ALIGN_DECL "__declspec(aligned(x))")
    ENDIF()
ELSE()
    SET(OLD_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    # Yes GCC, really don't accept visibility modes you don't support
    SET(CMAKE_REQUIRED_FLAGS "${OLD_REQUIRED_FLAGS} -Wattributes -Werror")

    CHECK_C_SOURCE_COMPILES("int foo() __attribute__((visibility(\"protected\")));
                             int main() {return 0;}" HAVE_GCC_PROTECTED_VISIBILITY)
    IF(HAVE_GCC_PROTECTED_VISIBILITY)
        SET(EXPORT_DECL "__attribute__((visibility(\"protected\")))")
    ELSE()
        CHECK_C_SOURCE_COMPILES("int foo() __attribute__((visibility(\"default\")));
                                 int main() {return 0;}" HAVE_GCC_DEFAULT_VISIBILITY)
        IF(HAVE_GCC_DEFAULT_VISIBILITY)
            SET(EXPORT_DECL "__attribute__((visibility(\"default\")))")
        ENDIF()
    ENDIF()

    IF(HAVE_GCC_PROTECTED_VISIBILITY OR HAVE_GCC_DEFAULT_VISIBILITY)
        CHECK_C_COMPILER_FLAG(-fvisibility=internal HAVE_VISIBILITY_INTERNAL_SWITCH)
        IF(HAVE_VISIBILITY_INTERNAL_SWITCH)
            ADD_DEFINITIONS(-fvisibility=internal)
        ELSE()
            CHECK_C_COMPILER_FLAG(-fvisibility=hidden HAVE_VISIBILITY_HIDDEN_SWITCH)
            IF(HAVE_VISIBILITY_HIDDEN_SWITCH)
                ADD_DEFINITIONS(-fvisibility=hidden)
            ENDIF()
        ENDIF()
    ENDIF()

    CHECK_C_SOURCE_COMPILES("int foo __attribute__((aligned(16)));
                             int main() {return 0;}" HAVE_ATTRIBUTE_ALIGNED)
    IF(HAVE_ATTRIBUTE_ALIGNED)
        SET(ALIGN_DECL "__attribute__((aligned(x)))")
    ENDIF()

    SET(CMAKE_REQUIRED_FLAGS "${OLD_REQUIRED_FLAGS}")
ENDIF()

SET(SSE_SWITCH "")
IF(MSVC)
    CHECK_C_COMPILER_FLAG(/arch:SSE HAVE_ARCHSSE_SWITCH)
    IF(HAVE_ARCHSSE_SWITCH)
        SET(SSE_SWITCH "/arch:SSE")
    ENDIF()
ENDIF()
IF(NOT SSE_SWITCH)
    CHECK_C_COMPILER_FLAG(-msse HAVE_MSSE_SWITCH)
    IF(HAVE_MSSE_SWITCH)
        SET(SSE_SWITCH "-msse")
    ENDIF()
ENDIF()

CHECK_C_SOURCE_COMPILES("int foo(const char *str, ...) __attribute__((format(printf, 1, 2)));
                         int main() {return 0;}" HAVE_GCC_FORMAT)

CHECK_INCLUDE_FILE(malloc.h HAVE_MALLOC_H)
CHECK_INCLUDE_FILE(strings.h HAVE_STRINGS_H)
CHECK_INCLUDE_FILE(cpuid.h HAVE_CPUID_H)
CHECK_INCLUDE_FILE(sys/sysconf.h HAVE_SYS_SYSCONF_H)
CHECK_INCLUDE_FILE(fenv.h HAVE_FENV_H)
CHECK_INCLUDE_FILE(float.h HAVE_FLOAT_H)
CHECK_INCLUDE_FILE(ieeefp.h HAVE_IEEEFP_H)
CHECK_INCLUDE_FILE(guiddef.h HAVE_GUIDDEF_H)
IF(NOT HAVE_GUIDDEF_H)
    CHECK_INCLUDE_FILE(initguid.h HAVE_INITGUID_H)
ENDIF()

# Some systems need libm for some of the following math functions to work
CHECK_LIBRARY_EXISTS(m pow "" HAVE_LIBM)
IF(HAVE_LIBM)
    SET(EXTRA_LIBS m ${EXTRA_LIBS})
    SET(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} m)
ENDIF()


CHECK_SYMBOL_EXISTS(aligned_alloc    stdlib.h HAVE_ALIGNED_ALLOC)
CHECK_SYMBOL_EXISTS(posix_memalign   stdlib.h HAVE_POSIX_MEMALIGN)
CHECK_SYMBOL_EXISTS(_aligned_malloc  malloc.h HAVE__ALIGNED_MALLOC)
CHECK_SYMBOL_EXISTS(lrintf math.h HAVE_LRINTF)

IF(HAVE_FLOAT_H)
    CHECK_SYMBOL_EXISTS(_controlfp float.h HAVE__CONTROLFP)
    CHECK_SYMBOL_EXISTS(__control87_2 float.h HAVE___CONTROL87_2)
ENDIF()

CHECK_FUNCTION_EXISTS(strtof HAVE_STRTOF)
CHECK_FUNCTION_EXISTS(stat HAVE_STAT)
CHECK_FUNCTION_EXISTS(strcasecmp HAVE_STRCASECMP)
IF(NOT HAVE_STRCASECMP)
    CHECK_FUNCTION_EXISTS(_stricmp HAVE__STRICMP)
    IF(NOT HAVE__STRICMP)
        MESSAGE(FATAL_ERROR "No case-insensitive compare function found, please report!")
    ENDIF()

    ADD_DEFINITIONS(-Dstrcasecmp=_stricmp)
ENDIF()

CHECK_FUNCTION_EXISTS(strncasecmp HAVE_STRNCASECMP)
IF(NOT HAVE_STRNCASECMP)
    CHECK_FUNCTION_EXISTS(_strnicmp HAVE__STRNICMP)
    IF(NOT HAVE__STRNICMP)
        MESSAGE(FATAL_ERROR "No case-insensitive size-limitted compare function found, please report!")
    ENDIF()

    ADD_DEFINITIONS(-Dstrncasecmp=_strnicmp)
ENDIF()

CHECK_FUNCTION_EXISTS(snprintf HAVE_SNPRINTF)
IF(NOT HAVE_SNPRINTF)
    CHECK_FUNCTION_EXISTS(_snprintf HAVE__SNPRINTF)
    IF(NOT HAVE__SNPRINTF)
        MESSAGE(FATAL_ERROR "No snprintf function found, please report!")
    ENDIF()

    ADD_DEFINITIONS(-Dsnprintf=_snprintf)
ENDIF()

CHECK_SYMBOL_EXISTS(isfinite math.h HAVE_ISFINITE)
IF(NOT HAVE_ISFINITE)
    CHECK_FUNCTION_EXISTS(finite HAVE_FINITE)
    IF(NOT HAVE_FINITE)
        CHECK_FUNCTION_EXISTS(_finite HAVE__FINITE)
        IF(NOT HAVE__FINITE)
            MESSAGE(FATAL_ERROR "No isfinite function found, please report!")
        ENDIF()
        ADD_DEFINITIONS(-Disfinite=_finite)
    ELSE()
        ADD_DEFINITIONS(-Disfinite=finite)
    ENDIF()
ENDIF()

CHECK_SYMBOL_EXISTS(isnan math.h HAVE_ISNAN)
IF(NOT HAVE_ISNAN)
    CHECK_FUNCTION_EXISTS(_isnan HAVE__ISNAN)
    IF(NOT HAVE__ISNAN)
        MESSAGE(FATAL_ERROR "No isnan function found, please report!")
    ENDIF()

    ADD_DEFINITIONS(-Disnan=_isnan)
ENDIF()


# Check for the dlopen API (for dynamicly loading backend libs)
IF(ALSOFT_DLOPEN)
    CHECK_INCLUDE_FILE(dlfcn.h HAVE_DLFCN_H)
    IF(HAVE_DLFCN_H)
        CHECK_LIBRARY_EXISTS(dl dlopen "" HAVE_LIBDL)
        IF(HAVE_LIBDL)
            SET(EXTRA_LIBS dl ${EXTRA_LIBS})
        ENDIF()
    ENDIF()
ENDIF()

# Check if we have Windows headers
CHECK_INCLUDE_FILE(windows.h HAVE_WINDOWS_H -D_WIN32_WINNT=0x0501)
IF(NOT HAVE_WINDOWS_H)
    # These are needed on some systems for extra features
    ADD_DEFINITIONS(-D_GNU_SOURCE=1 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700)
    SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -D_GNU_SOURCE=1 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700")

    CHECK_FUNCTION_EXISTS(gettimeofday HAVE_GETTIMEOFDAY)
    IF(NOT HAVE_GETTIMEOFDAY)
        MESSAGE(FATAL_ERROR "No timing function found!")
    ENDIF()

    CHECK_FUNCTION_EXISTS(nanosleep HAVE_NANOSLEEP)
    IF(NOT HAVE_NANOSLEEP)
        MESSAGE(FATAL_ERROR "No sleep function found!")
    ENDIF()

    # We need pthreads outside of Windows
    CHECK_INCLUDE_FILE(pthread.h HAVE_PTHREAD_H)
    IF(NOT HAVE_PTHREAD_H)
        MESSAGE(FATAL_ERROR "PThreads is required for non-Windows builds!")
    ENDIF()
    # Some systems need pthread_np.h to get recursive mutexes
    CHECK_INCLUDE_FILES("pthread.h;pthread_np.h" HAVE_PTHREAD_NP_H)

    CHECK_C_COMPILER_FLAG(-pthread HAVE_PTHREAD)
    IF(HAVE_PTHREAD)
        ADD_DEFINITIONS(-pthread)
        SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -pthread")
        SET(EXTRA_LIBS ${EXTRA_LIBS} -pthread)
    ENDIF()

    CHECK_LIBRARY_EXISTS(pthread pthread_create "" HAVE_LIBPTHREAD)
    IF(HAVE_LIBPTHREAD)
        SET(EXTRA_LIBS pthread ${EXTRA_LIBS})
    ENDIF()

    CHECK_SYMBOL_EXISTS(pthread_setschedparam pthread.h HAVE_PTHREAD_SETSCHEDPARAM)

    CHECK_SYMBOL_EXISTS(pthread_setname_np pthread.h HAVE_PTHREAD_SETNAME_NP)
    IF(NOT HAVE_PTHREAD_SETNAME_NP)
        CHECK_SYMBOL_EXISTS(pthread_set_name_np pthread.h HAVE_PTHREAD_SET_NAME_NP)
    ENDIF()

    CHECK_LIBRARY_EXISTS(rt clock_gettime "" HAVE_LIBRT)
    IF(HAVE_LIBRT)
        SET(EXTRA_LIBS rt ${EXTRA_LIBS})
    ENDIF()
ENDIF()

# Check for a 64-bit type
CHECK_INCLUDE_FILE(stdint.h HAVE_STDINT_H)
IF(NOT HAVE_STDINT_H)
    IF(HAVE_WINDOWS_H)
        CHECK_C_SOURCE_COMPILES("#define _WIN32_WINNT 0x0501
                                 #include <windows.h>
                                 __int64 foo;
                                 int main() {return 0;}" HAVE___INT64)
    ENDIF()
    IF(NOT HAVE___INT64)
        IF(NOT SIZEOF_LONG MATCHES "8")
            IF(NOT SIZEOF_LONG_LONG MATCHES "8")
                MESSAGE(FATAL_ERROR "No 64-bit types found, please report!")
            ENDIF()
        ENDIF()
    ENDIF()
ENDIF()

# Windows needs winmm for timeGetTime, even if the backend is disabled
CHECK_SHARED_FUNCTION_EXISTS(timeGetTime "windows.h;mmsystem.h" winmm "" HAVE_LIBWINMM)
IF(HAVE_LIBWINMM)
    SET(EXTRA_LIBS winmm ${EXTRA_LIBS})
    SET(PKG_CONFIG_LIBS ${PKG_CONFIG_LIBS} -lwinmm)
ENDIF()


SET(OPENAL_OBJS  OpenAL32/alAuxEffectSlot.c
                 OpenAL32/alBuffer.c
                 OpenAL32/alEffect.c
                 OpenAL32/alError.c
                 OpenAL32/alExtension.c
                 OpenAL32/alFilter.c
                 OpenAL32/alFontsound.c
                 OpenAL32/alListener.c
                 OpenAL32/alMidi.c
                 OpenAL32/alPreset.c
                 OpenAL32/alSoundfont.c
                 OpenAL32/alSource.c
                 OpenAL32/alState.c
                 OpenAL32/alThunk.c
)
SET(ALC_OBJS  Alc/ALc.c
              Alc/ALu.c
              Alc/alcConfig.c
              Alc/alcRing.c
              Alc/bs2b.c
              Alc/effects/autowah.c
              Alc/effects/chorus.c
              Alc/effects/compressor.c
              Alc/effects/dedicated.c
              Alc/effects/distortion.c
              Alc/effects/echo.c
              Alc/effects/equalizer.c
              Alc/effects/flanger.c
              Alc/effects/modulator.c
              Alc/effects/null.c
              Alc/effects/reverb.c
              Alc/helpers.c
              Alc/hrtf.c
              Alc/panning.c
              Alc/threads.c
              Alc/mixer.c
              Alc/mixer_c.c
)


SET(CPU_EXTS "Default")
SET(HAVE_SSE  0)
SET(HAVE_NEON 0)

# Check for SSE support
IF(ALSOFT_CPUEXT_SSE AND ALIGN_DECL)
    CHECK_INCLUDE_FILE(xmmintrin.h HAVE_XMMINTRIN_H "${SSE_SWITCH}")
    IF(HAVE_XMMINTRIN_H)
        SET(HAVE_SSE 1)
        SET(ALC_OBJS  ${ALC_OBJS} Alc/mixer_sse.c)
        IF(SSE_SWITCH)
            SET_SOURCE_FILES_PROPERTIES(Alc/mixer_sse.c PROPERTIES
                                        COMPILE_FLAGS "${SSE_SWITCH}")
        ENDIF()
        SET(CPU_EXTS "${CPU_EXTS}, SSE")
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_SSE AND NOT HAVE_SSE)
    MESSAGE(FATAL_ERROR "Failed to enabled required SSE CPU extensions")
ENDIF()

# Check for ARM Neon support
IF(ALSOFT_CPUEXT_NEON)
    CHECK_INCLUDE_FILE(arm_neon.h HAVE_ARM_NEON_H)
    IF(HAVE_ARM_NEON_H)
        SET(HAVE_NEON 1)
        SET(ALC_OBJS  ${ALC_OBJS} Alc/mixer_neon.c)
        SET(CPU_EXTS "${CPU_EXTS}, Neon")
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_NEON AND NOT HAVE_NEON)
    MESSAGE(FATAL_ERROR "Failed to enabled required ARM Neon CPU extensions")
ENDIF()


SET(ALC_OBJS  ${ALC_OBJS}
              Alc/midi/base.c
              Alc/midi/sf2load.c
              Alc/midi/dummy.c
              Alc/midi/fluidsynth.c
)

SET(HAVE_FLUIDSYNTH  0)

# Check for FluidSynth support
IF(ALSOFT_MIDI_FLUIDSYNTH)
    FIND_PACKAGE(FluidSynth)
    IF(FLUIDSYNTH_FOUND)
        SET(HAVE_FLUIDSYNTH 1)
        IF(CMAKE_VERSION VERSION_LESS "2.8.8")
            INCLUDE_DIRECTORIES(${FLUIDSYNTH_INCLUDE_DIR})
        ENDIF()
        SET(EXTRA_LIBS ${FLUIDSYNTH_LIBRARIES} ${EXTRA_LIBS})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_FLUIDSYNTH AND NOT HAVE_FLUIDSYNTH)
    MESSAGE(FATAL_ERROR "Failed to enabled required FluidSynth support")
ENDIF()


SET(ALC_OBJS  ${ALC_OBJS}
              Alc/backends/base.c
              # Default backends, always available
              Alc/backends/loopback.c
              Alc/backends/null.c
)

SET(BACKENDS "")
SET(HAVE_ALSA       0)
SET(HAVE_OSS        0)
SET(HAVE_SOLARIS    0)
SET(HAVE_SNDIO      0)
SET(HAVE_QSA        0)
SET(HAVE_DSOUND     0)
SET(HAVE_MMDEVAPI   0)
SET(HAVE_WINMM      0)
SET(HAVE_PORTAUDIO  0)
SET(HAVE_PULSEAUDIO 0)
SET(HAVE_COREAUDIO  0)
SET(HAVE_OPENSL     0)
SET(HAVE_WAVE       0)

# Check ALSA backend
IF(ALSOFT_BACKEND_ALSA)
    CHECK_INCLUDE_FILE(alsa/asoundlib.h HAVE_ALSA_ASOUNDLIB_H)
    IF(HAVE_ALSA_ASOUNDLIB_H)
        CHECK_SHARED_FUNCTION_EXISTS(snd_pcm_open "alsa/asoundlib.h" asound "" HAVE_LIBASOUND)
        IF(HAVE_LIBASOUND OR HAVE_DLFCN_H OR WIN32)
            SET(HAVE_ALSA 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/alsa.c)
            IF(HAVE_DLFCN_H OR WIN32)
                SET(BACKENDS  "${BACKENDS} ALSA,")
            ELSE()
                SET(BACKENDS  "${BACKENDS} ALSA \(linked\),")
                SET(EXTRA_LIBS asound ${EXTRA_LIBS})
            ENDIF()
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_ALSA AND NOT HAVE_ALSA)
    MESSAGE(FATAL_ERROR "Failed to enabled required ALSA backend")
ENDIF()

# Check OSS backend
IF(ALSOFT_BACKEND_OSS)
    CHECK_INCLUDE_FILE(sys/soundcard.h HAVE_SYS_SOUNDCARD_H)
    IF(HAVE_SYS_SOUNDCARD_H)
        SET(HAVE_OSS 1)
        SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/oss.c)
        SET(BACKENDS  "${BACKENDS} OSS,")
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_OSS AND NOT HAVE_OSS)
    MESSAGE(FATAL_ERROR "Failed to enabled required OSS backend")
ENDIF()

# Check Solaris backend
IF(ALSOFT_BACKEND_SOLARIS)
    CHECK_INCLUDE_FILE(sys/audioio.h HAVE_SYS_AUDIOIO_H)
    IF(HAVE_SYS_AUDIOIO_H)
        SET(HAVE_SOLARIS 1)
        SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/solaris.c)
        SET(BACKENDS  "${BACKENDS} Solaris,")
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_SOLARIS AND NOT HAVE_SOLARIS)
    MESSAGE(FATAL_ERROR "Failed to enabled required Solaris backend")
ENDIF()

# Check SndIO backend
IF(ALSOFT_BACKEND_SNDIO)
    CHECK_INCLUDE_FILE(sndio.h HAVE_SNDIO_H)
    IF(HAVE_SNDIO_H)
        CHECK_SHARED_FUNCTION_EXISTS(sio_open "sndio.h" sndio "" HAVE_LIBSNDIO)
        IF(HAVE_LIBSNDIO)
            SET(HAVE_SNDIO 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/sndio.c)
            SET(BACKENDS  "${BACKENDS} SndIO \(linked\),")
            SET(EXTRA_LIBS sndio ${EXTRA_LIBS})
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_SNDIO AND NOT HAVE_SNDIO)
    MESSAGE(FATAL_ERROR "Failed to enabled required SndIO backend")
ENDIF()

# Check QSA backend
IF (ALSOFT_BACKEND_QSA AND "${CMAKE_C_PLATFORM_ID}" STREQUAL "QNX")
    CHECK_INCLUDE_FILE(sys/asoundlib.h HAVE_SYS_ASOUNDLIB_H)
    IF(HAVE_SYS_ASOUNDLIB_H)
        CHECK_SHARED_FUNCTION_EXISTS(snd_pcm_open "sys/asoundlib.h" asound "" HAVE_LIBASOUND)
        IF(HAVE_LIBASOUND)
            SET(HAVE_QSA 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/qsa.c)
            SET(EXTRA_LIBS asound ${EXTRA_LIBS})
            SET(BACKENDS  "${BACKENDS} QSA \(linked\),")
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_QSA AND NOT HAVE_QSA)
    MESSAGE(FATAL_ERROR "Failed to enabled required QSA backend")
ENDIF()

# Check for MMDevApi backend
IF(HAVE_WINDOWS_H)
    IF(ALSOFT_BACKEND_MMDEVAPI)
        CHECK_INCLUDE_FILE(mmdeviceapi.h HAVE_MMDEVICEAPI_H)
        IF(HAVE_MMDEVICEAPI_H)
            SET(HAVE_MMDEVAPI 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/mmdevapi.c)

            SET(BACKENDS  "${BACKENDS} MMDevApi,")
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_MMDEVAPI AND NOT HAVE_MMDEVAPI)
    MESSAGE(FATAL_ERROR "Failed to enabled required MMDevApi backend")
ENDIF()

# Check DSound/MMSystem backend
IF(ALSOFT_BACKEND_DSOUND)
    CHECK_INCLUDE_FILE(dsound.h HAVE_DSOUND_H)
    IF(HAVE_DSOUND_H)
        CHECK_SHARED_FUNCTION_EXISTS(DirectSoundCreate "dsound.h" dsound "" HAVE_LIBDSOUND)
        IF(HAVE_LIBDSOUND OR HAVE_DLFCN_H OR WIN32)
            SET(HAVE_DSOUND 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/dsound.c)

            IF(HAVE_DLFCN_H OR WIN32)
                SET(BACKENDS  "${BACKENDS} DirectSound,")
            ELSE()
                SET(BACKENDS  "${BACKENDS} DirectSound \(linked\),")
                SET(EXTRA_LIBS dsound ${EXTRA_LIBS})
            ENDIF()
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_DSOUND AND NOT HAVE_DSOUND)
    MESSAGE(FATAL_ERROR "Failed to enabled required DSound backend")
ENDIF()

IF(HAVE_WINDOWS_H)
    IF(ALSOFT_BACKEND_WINMM)
        CHECK_INCLUDE_FILES("windows.h;mmsystem.h" HAVE_MMSYSTEM_H -D_WIN32_WINNT=0x0501)
        IF(HAVE_MMSYSTEM_H AND HAVE_LIBWINMM)
            SET(HAVE_WINMM 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/winmm.c)
            SET(BACKENDS  "${BACKENDS} WinMM,")
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_WINMM AND NOT HAVE_WINMM)
    MESSAGE(FATAL_ERROR "Failed to enabled required WinMM backend")
ENDIF()

# Check PortAudio backend
IF(ALSOFT_BACKEND_PORTAUDIO)
    CHECK_INCLUDE_FILE(portaudio.h HAVE_PORTAUDIO_H)
    IF(HAVE_PORTAUDIO_H)
        CHECK_SHARED_FUNCTION_EXISTS(Pa_Initialize "portaudio.h" portaudio "" HAVE_LIBPORTAUDIO)
        IF(HAVE_LIBPORTAUDIO OR HAVE_DLFCN_H OR WIN32)
            SET(HAVE_PORTAUDIO 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/portaudio.c)
            IF(HAVE_DLFCN_H OR WIN32)
                SET(BACKENDS  "${BACKENDS} PortAudio,")
            ELSE()
                SET(BACKENDS  "${BACKENDS} PortAudio \(linked\),")
                SET(EXTRA_LIBS portaudio ${EXTRA_LIBS})
            ENDIF()
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_PORTAUDIO AND NOT HAVE_PORTAUDIO)
    MESSAGE(FATAL_ERROR "Failed to enabled required PortAudio backend")
ENDIF()

# Check PulseAudio backend
IF(ALSOFT_BACKEND_PULSEAUDIO)
    CHECK_INCLUDE_FILE(pulse/pulseaudio.h HAVE_PULSE_PULSEAUDIO_H)
    IF(HAVE_PULSE_PULSEAUDIO_H)
        CHECK_SHARED_FUNCTION_EXISTS(pa_context_new "pulse/pulseaudio.h" pulse "" HAVE_LIBPULSE)
        IF(HAVE_LIBPULSE OR HAVE_DLFCN_H OR WIN32)
            SET(HAVE_PULSEAUDIO 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/pulseaudio.c)
            IF(HAVE_DLFCN_H OR WIN32)
                SET(BACKENDS  "${BACKENDS} PulseAudio,")
            ELSE()
                SET(BACKENDS  "${BACKENDS} PulseAudio \(linked\),")
                SET(EXTRA_LIBS pulse ${EXTRA_LIBS})
            ENDIF()
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_PULSEAUDIO AND NOT HAVE_PULSEAUDIO)
    MESSAGE(FATAL_ERROR "Failed to enabled required PulseAudio backend")
ENDIF()

# Check CoreAudio backend
IF(ALSOFT_BACKEND_COREAUDIO)
    CHECK_INCLUDE_FILE(/System/Library/Frameworks/CoreAudio.framework/Headers/CoreAudio.h HAVE_COREAUDIO_FRAMEWORK)
    IF(HAVE_COREAUDIO_FRAMEWORK)
        SET(HAVE_COREAUDIO 1)
        SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/coreaudio.c)
        SET(BACKENDS  "${BACKENDS} CoreAudio,")
        SET(EXTRA_LIBS /System/Library/Frameworks/CoreAudio.framework ${EXTRA_LIBS})
        SET(EXTRA_LIBS /System/Library/Frameworks/AudioUnit.framework ${EXTRA_LIBS})
        SET(EXTRA_LIBS /System/Library/Frameworks/ApplicationServices.framework ${EXTRA_LIBS})

        # Some versions of OSX may need the AudioToolbox framework. Add it if
        # it's found.
        FIND_LIBRARY(AUDIOTOOLBOX_LIBRARY
                     NAMES AudioToolbox
                     PATHS ~/Library/Frameworks
                           /Library/Frameworks
                           /System/Library/Frameworks
                    )
        IF(AUDIOTOOLBOX_LIBRARY)
            SET(EXTRA_LIBS ${AUDIOTOOLBOX_LIBRARY} ${EXTRA_LIBS})
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_COREAUDIO AND NOT HAVE_COREAUDIO)
    MESSAGE(FATAL_ERROR "Failed to enabled required CoreAudio backend")
ENDIF()

# Check for OpenSL (Android) backend
IF(ALSOFT_BACKEND_OPENSL)
    CHECK_INCLUDE_FILES("SLES/OpenSLES.h;SLES/OpenSLES_Android.h" HAVE_SLES_OPENSLES_ANDROID_H)
    IF(HAVE_SLES_OPENSLES_ANDROID_H)
        CHECK_SHARED_FUNCTION_EXISTS(slCreateEngine "SLES/OpenSLES.h" OpenSLES "" HAVE_LIBOPENSLES)
        IF(HAVE_LIBOPENSLES)
            SET(HAVE_OPENSL 1)
            SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/opensl.c)
            SET(BACKENDS  "${BACKENDS} OpenSL,")
            SET(EXTRA_LIBS OpenSLES ${EXTRA_LIBS})
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_OPENSL AND NOT HAVE_OPENSL)
    MESSAGE(FATAL_ERROR "Failed to enabled required OpenSL backend")
ENDIF()

# Optionally enable the Wave Writer backend
IF(ALSOFT_BACKEND_WAVE)
    SET(HAVE_WAVE 1)
    SET(ALC_OBJS  ${ALC_OBJS} Alc/backends/wave.c)
    SET(BACKENDS  "${BACKENDS} WaveFile,")
ENDIF()

# This is always available
SET(BACKENDS  "${BACKENDS} Null")

IF(ALSOFT_UTILS AND NOT ALSOFT_NO_CONFIG_UTIL)
    add_subdirectory(utils/alsoft-config)
ENDIF()
IF(ALSOFT_EXAMPLES)
    FIND_PACKAGE(SDL)
    IF(SDL_FOUND)
        FIND_PACKAGE(SDL_sound)
        IF(SDL_SOUND_FOUND AND CMAKE_VERSION VERSION_LESS "2.8.8")
            INCLUDE_DIRECTORIES(${SDL_INCLUDE_DIR} ${SDL_SOUND_INCLUDE_DIR})
        ENDIF()
    ENDIF()
ENDIF()

IF(LIBTYPE STREQUAL "STATIC")
    ADD_DEFINITIONS(-DAL_LIBTYPE_STATIC)
    SET(PKG_CONFIG_CFLAGS -DAL_LIBTYPE_STATIC ${PKG_CONFIG_CFLAGS})
ENDIF()

# Needed for openal.pc.in
SET(prefix ${CMAKE_INSTALL_PREFIX})
SET(exec_prefix "\${prefix}")
SET(libdir "\${exec_prefix}/lib${LIB_SUFFIX}")
SET(bindir "\${exec_prefix}/bin")
SET(includedir "\${prefix}/include")
SET(PACKAGE_VERSION "${LIB_VERSION}")

# End configuration
CONFIGURE_FILE(
    "${OpenAL_SOURCE_DIR}/config.h.in"
    "${OpenAL_BINARY_DIR}/config.h")
CONFIGURE_FILE(
    "${OpenAL_SOURCE_DIR}/openal.pc.in"
    "${OpenAL_BINARY_DIR}/openal.pc"
    @ONLY)

# Build a library
ADD_LIBRARY(${LIBNAME} ${LIBTYPE} ${OPENAL_OBJS} ${ALC_OBJS})
SET_PROPERTY(TARGET ${LIBNAME} APPEND PROPERTY COMPILE_DEFINITIONS AL_BUILD_LIBRARY AL_ALEXT_PROTOTYPES)
IF(WIN32 AND ALSOFT_NO_UID_DEFS)
    SET_PROPERTY(TARGET ${LIBNAME} APPEND PROPERTY COMPILE_DEFINITIONS AL_NO_UID_DEFS)
ENDIF()
SET_PROPERTY(TARGET ${LIBNAME} APPEND PROPERTY INCLUDE_DIRECTORIES "${OpenAL_SOURCE_DIR}/OpenAL32/Include" "${OpenAL_SOURCE_DIR}/Alc")
IF(FLUIDSYNTH_FOUND)
    SET_PROPERTY(TARGET ${LIBNAME} APPEND PROPERTY INCLUDE_DIRECTORIES ${FLUIDSYNTH_INCLUDE_DIR})
ENDIF()
SET_TARGET_PROPERTIES(${LIBNAME} PROPERTIES VERSION ${LIB_VERSION}
                                            SOVERSION ${LIB_MAJOR_VERSION})
IF(WIN32 AND NOT LIBTYPE STREQUAL "STATIC")
    SET_TARGET_PROPERTIES(${LIBNAME} PROPERTIES PREFIX "")
ENDIF()

TARGET_LINK_LIBRARIES(${LIBNAME} ${EXTRA_LIBS})

# Add an install target here
INSTALL(TARGETS ${LIBNAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION "lib${LIB_SUFFIX}"
        ARCHIVE DESTINATION "lib${LIB_SUFFIX}"
)
INSTALL(FILES include/AL/al.h
              include/AL/alc.h
              include/AL/alext.h
              include/AL/efx.h
              include/AL/efx-creative.h
              include/AL/efx-presets.h
        DESTINATION include/AL
)
INSTALL(FILES "${OpenAL_BINARY_DIR}/openal.pc"
        DESTINATION "lib${LIB_SUFFIX}/pkgconfig")


MESSAGE(STATUS "")
MESSAGE(STATUS "Building OpenAL with support for the following backends:")
MESSAGE(STATUS "    ${BACKENDS}")
MESSAGE(STATUS "")
MESSAGE(STATUS "Building with support for CPU extensions:")
MESSAGE(STATUS "    ${CPU_EXTS}")
MESSAGE(STATUS "")
IF(HAVE_FLUIDSYNTH)
    MESSAGE(STATUS "FluidSynth support for ALC_SOFT_midi_interface enabled")
    MESSAGE(STATUS "")
ENDIF()

IF(WIN32)
    IF(NOT HAVE_DSOUND)
        MESSAGE(STATUS "WARNING: Building the Windows version without DirectSound output")
        MESSAGE(STATUS "         This is probably NOT what you want!")
        MESSAGE(STATUS "")
    ENDIF()
ENDIF()

# Install alsoft.conf configuration file
IF(ALSOFT_CONFIG)
    INSTALL(FILES alsoftrc.sample
            DESTINATION share/openal
    )
    MESSAGE(STATUS "Installing sample configuration")
    MESSAGE(STATUS "")
ENDIF()

IF(ALSOFT_UTILS)
    ADD_EXECUTABLE(openal-info utils/openal-info.c)
    TARGET_LINK_LIBRARIES(openal-info ${LIBNAME})

    ADD_EXECUTABLE(makehrtf utils/makehrtf.c)
    IF(HAVE_LIBM)
        TARGET_LINK_LIBRARIES(makehrtf m)
    ENDIF()

    INSTALL(TARGETS openal-info makehrtf
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION "lib${LIB_SUFFIX}"
            ARCHIVE DESTINATION "lib${LIB_SUFFIX}"
    )

    MESSAGE(STATUS "Building utility programs")
    IF(TARGET alsoft-config)
        MESSAGE(STATUS "Building configuration program")
    ENDIF()
    MESSAGE(STATUS "")
ENDIF()

IF(ALSOFT_EXAMPLES)
    IF(SDL_FOUND AND SDL_SOUND_FOUND)
        ADD_LIBRARY(ex-common STATIC examples/common/alhelpers.c
                                     examples/common/sdl_sound.c)
        SET_PROPERTY(TARGET ex-common APPEND PROPERTY INCLUDE_DIRECTORIES ${SDL_SOUND_INCLUDE_DIR})

        ADD_EXECUTABLE(alstream examples/alstream.c)
        TARGET_LINK_LIBRARIES(alstream ex-common ${SDL_SOUND_LIBRARIES} ${LIBNAME})
        SET_PROPERTY(TARGET alstream APPEND PROPERTY INCLUDE_DIRECTORIES ${SDL_SOUND_INCLUDE_DIR})

        ADD_EXECUTABLE(alreverb examples/alreverb.c)
        TARGET_LINK_LIBRARIES(alreverb ex-common ${SDL_SOUND_LIBRARIES} ${LIBNAME})
        SET_PROPERTY(TARGET alreverb APPEND PROPERTY INCLUDE_DIRECTORIES ${SDL_SOUND_INCLUDE_DIR})

        ADD_EXECUTABLE(allatency examples/allatency.c)
        TARGET_LINK_LIBRARIES(allatency ex-common ${SDL_SOUND_LIBRARIES} ${LIBNAME})
        SET_PROPERTY(TARGET allatency APPEND PROPERTY INCLUDE_DIRECTORIES ${SDL_SOUND_INCLUDE_DIR})

        ADD_EXECUTABLE(alloopback examples/alloopback.c)
        TARGET_LINK_LIBRARIES(alloopback ex-common ${SDL_SOUND_LIBRARIES} ${SDL_LIBRARY} ${LIBNAME})
        SET_PROPERTY(TARGET alloopback APPEND PROPERTY INCLUDE_DIRECTORIES ${SDL_INCLUDE_DIR}
                                                                           ${SDL_SOUND_INCLUDE_DIR})

        INSTALL(TARGETS alstream alreverb allatency alloopback
                RUNTIME DESTINATION bin
                LIBRARY DESTINATION "lib${LIB_SUFFIX}"
                ARCHIVE DESTINATION "lib${LIB_SUFFIX}"
        )

        MESSAGE(STATUS "Building example programs")
        MESSAGE(STATUS "")
    ENDIF()
ENDIF()
