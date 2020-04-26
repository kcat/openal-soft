# CMake build file list for OpenAL

CMAKE_MINIMUM_REQUIRED(VERSION 3.0.2)

PROJECT(OpenAL)

IF(COMMAND CMAKE_POLICY)
    CMAKE_POLICY(SET CMP0003 NEW)
    CMAKE_POLICY(SET CMP0005 NEW)
    IF(POLICY CMP0020)
        CMAKE_POLICY(SET CMP0020 NEW)
    ENDIF(POLICY CMP0020)
    IF(POLICY CMP0042)
        CMAKE_POLICY(SET CMP0042 NEW)
    ENDIF(POLICY CMP0042)
    IF(POLICY CMP0054)
        CMAKE_POLICY(SET CMP0054 NEW)
    ENDIF(POLICY CMP0054)
    IF(POLICY CMP0075)
        CMAKE_POLICY(SET CMP0075 NEW)
    ENDIF(POLICY CMP0075)
ENDIF(COMMAND CMAKE_POLICY)

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

SET(CMAKE_MODULE_PATH "${OpenAL_SOURCE_DIR}/cmake")


INCLUDE(CheckFunctionExists)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckIncludeFile)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckSymbolExists)
INCLUDE(CheckCCompilerFlag)
INCLUDE(CheckCXXCompilerFlag)
INCLUDE(CheckCSourceCompiles)
INCLUDE(CheckCXXSourceCompiles)
INCLUDE(CheckTypeSize)
include(CheckStructHasMember)
include(GNUInstallDirs)


option(ALSOFT_DLOPEN  "Check for the dlopen API for loading optional libs"  ON)

option(ALSOFT_WERROR  "Treat compile warnings as errors"      OFF)

option(ALSOFT_UTILS "Build utility programs"  ON)
option(ALSOFT_NO_CONFIG_UTIL "Disable building the alsoft-config utility" OFF)

option(ALSOFT_EXAMPLES  "Build example programs"  ON)

option(ALSOFT_INSTALL_CONFIG "Install alsoft.conf sample configuration file" ON)
option(ALSOFT_INSTALL_HRTF_DEFS "Install HRTF definition files" ON)
option(ALSOFT_INSTALL_AMBDEC_PRESETS "Install AmbDec preset files" ON)
option(ALSOFT_INSTALL_EXAMPLES "Install example programs (alplay, alstream, ...)" ON)
option(ALSOFT_INSTALL_UTILS "Install utility programs (openal-info, alsoft-config, ...)" ON)
option(ALSOFT_UPDATE_BUILD_VERSION "Update git build version info" ON)

if(DEFINED SHARE_INSTALL_DIR)
    message(WARNING "SHARE_INSTALL_DIR is deprecated.  Use the variables provided by the GNUInstallDirs module instead")
    set(CMAKE_INSTALL_DATADIR "${SHARE_INSTALL_DIR}")
endif()

if(DEFINED LIB_SUFFIX)
    message(WARNING "LIB_SUFFIX is deprecated.  Use the variables provided by the GNUInstallDirs module instead")
endif()
if(DEFINED ALSOFT_CONFIG)
    message(WARNING "ALSOFT_CONFIG is deprecated. Use ALSOFT_INSTALL_CONFIG instead")
endif()
if(DEFINED ALSOFT_HRTF_DEFS)
    message(WARNING "ALSOFT_HRTF_DEFS is deprecated. Use ALSOFT_INSTALL_HRTF_DEFS instead")
endif()
if(DEFINED ALSOFT_AMBDEC_PRESETS)
    message(WARNING "ALSOFT_AMBDEC_PRESETS is deprecated. Use ALSOFT_INSTALL_AMBDEC_PRESETS instead")
endif()


SET(CPP_DEFS ) # C pre-processor, not C++
SET(INC_PATHS )
SET(C_FLAGS )
SET(LINKER_FLAGS )
SET(EXTRA_LIBS )

IF(WIN32)
    SET(CPP_DEFS ${CPP_DEFS} _WIN32)

    OPTION(ALSOFT_BUILD_ROUTER  "Build the router (EXPERIMENTAL; creates OpenAL32.dll and soft_oal.dll)"  OFF)

    IF(MINGW)
        OPTION(ALSOFT_BUILD_IMPORT_LIB "Build an import .lib using dlltool (requires sed)" ON)
        IF(NOT DLLTOOL)
            IF(HOST)
                SET(DLLTOOL "${HOST}-dlltool")
            ELSE()
                SET(DLLTOOL "dlltool")
            ENDIF()
        ENDIF()
    ENDIF()
ENDIF()


# QNX's gcc do not uses /usr/include and /usr/lib pathes by default
IF ("${CMAKE_C_PLATFORM_ID}" STREQUAL "QNX")
    SET(INC_PATHS ${INC_PATHS} /usr/include)
    SET(LINKER_FLAGS ${LINKER_FLAGS} -L/usr/lib)
ENDIF()

IF(NOT LIBTYPE)
    SET(LIBTYPE SHARED)
ENDIF()

SET(LIB_MAJOR_VERSION "1")
SET(LIB_MINOR_VERSION "20")
SET(LIB_REVISION "1")
SET(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_REVISION}")
SET(LIB_VERSION_NUM ${LIB_MAJOR_VERSION},${LIB_MINOR_VERSION},${LIB_REVISION},0)

SET(EXPORT_DECL "")


CHECK_TYPE_SIZE("long" SIZEOF_LONG)


# Require C++14
SET(CMAKE_CXX_STANDARD 14)
SET(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Prefer C11, but support C99 and C90 too.
SET(CMAKE_C_STANDARD 11)


if(NOT WIN32)
    # Check if _POSIX_C_SOURCE and _XOPEN_SOURCE needs to be set for POSIX functions
    CHECK_SYMBOL_EXISTS(posix_memalign stdlib.h HAVE_POSIX_MEMALIGN_DEFAULT)
    IF(NOT HAVE_POSIX_MEMALIGN_DEFAULT)
        SET(OLD_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
        SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600")
        CHECK_SYMBOL_EXISTS(posix_memalign stdlib.h HAVE_POSIX_MEMALIGN_POSIX)
        IF(NOT HAVE_POSIX_MEMALIGN_POSIX)
            SET(CMAKE_REQUIRED_FLAGS ${OLD_REQUIRED_FLAGS})
        ELSE()
            SET(CPP_DEFS ${CPP_DEFS} _POSIX_C_SOURCE=200112L _XOPEN_SOURCE=600)
        ENDIF()
    ENDIF()
    UNSET(OLD_REQUIRED_FLAGS)
ENDIF()

# C99 has restrict, but C++ does not, so we can only utilize __restrict.
SET(RESTRICT_DECL )
CHECK_CXX_SOURCE_COMPILES("int *__restrict foo;
int main() { return 0; }" HAVE___RESTRICT)
IF(HAVE___RESTRICT)
    SET(RESTRICT_DECL "__restrict")
ENDIF()

# Some systems may need libatomic for atomic functions to work
SET(OLD_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
SET(CMAKE_REQUIRED_LIBRARIES ${OLD_REQUIRED_LIBRARIES} atomic)
CHECK_CXX_SOURCE_COMPILES("#include <atomic>
std::atomic<int> foo{0};
int main() { return foo.fetch_add(2); }"
HAVE_LIBATOMIC)
IF(NOT HAVE_LIBATOMIC)
    SET(CMAKE_REQUIRED_LIBRARIES "${OLD_REQUIRED_LIBRARIES}")
ELSE()
    SET(EXTRA_LIBS atomic ${EXTRA_LIBS})
ENDIF()
UNSET(OLD_REQUIRED_LIBRARIES)

# Include liblog for Android logging
CHECK_LIBRARY_EXISTS(log __android_log_print "" HAVE_LIBLOG)
IF(HAVE_LIBLOG)
    SET(EXTRA_LIBS log ${EXTRA_LIBS})
    SET(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} log)
ENDIF()

IF(MSVC)
    SET(CPP_DEFS ${CPP_DEFS} _CRT_SECURE_NO_WARNINGS NOMINMAX)
    CHECK_CXX_COMPILER_FLAG(/permissive- HAVE_PERMISSIVE_SWITCH)
    IF(HAVE_PERMISSIVE_SWITCH)
        SET(C_FLAGS ${C_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
    ENDIF()
    SET(C_FLAGS ${C_FLAGS} /W4 /w14640 /wd4065 /wd4268 /wd4324 /wd5030)

    IF(NOT DXSDK_DIR)
        STRING(REGEX REPLACE "\\\\" "/" DXSDK_DIR "$ENV{DXSDK_DIR}")
    ELSE()
        STRING(REGEX REPLACE "\\\\" "/" DXSDK_DIR "${DXSDK_DIR}")
    ENDIF()
    IF(DXSDK_DIR)
        MESSAGE(STATUS "Using DirectX SDK directory: ${DXSDK_DIR}")
    ENDIF()

    OPTION(FORCE_STATIC_VCRT "Force /MT for static VC runtimes" OFF)
    IF(FORCE_STATIC_VCRT)
        FOREACH(flag_var
                CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
                CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
                CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
                CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
            IF(${flag_var} MATCHES "/MD")
                STRING(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
            ENDIF()
        ENDFOREACH(flag_var)
    ENDIF()
ELSE()
    SET(C_FLAGS ${C_FLAGS} -Winline -Wunused -Wall -Wextra -Wshadow -Wconversion -Wcast-align
        -Wpedantic
        $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual>)

    IF(ALSOFT_WERROR)
        SET(C_FLAGS ${C_FLAGS} -Werror)
    ENDIF()

    # We want RelWithDebInfo to actually include debug stuff (define _DEBUG
    # instead of NDEBUG)
    FOREACH(flag_var  CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        IF(${flag_var} MATCHES "-DNDEBUG")
            STRING(REGEX REPLACE "-DNDEBUG" "-D_DEBUG" ${flag_var} "${${flag_var}}")
        ENDIF()
    ENDFOREACH()

    CHECK_C_COMPILER_FLAG(-fno-math-errno HAVE_FNO_MATH_ERRNO)
    IF(HAVE_FNO_MATH_ERRNO)
        SET(C_FLAGS ${C_FLAGS} -fno-math-errno)
    ENDIF()

    option(ALSOFT_STATIC_LIBGCC "Force -static-libgcc for static GCC runtimes" OFF)
    if(ALSOFT_STATIC_LIBGCC)
        set(OLD_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
        set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} -static-libgcc)
        check_cxx_source_compiles("int main() { }" HAVE_STATIC_LIBGCC_SWITCH)
        set(CMAKE_REQUIRED_LIBRARIES ${OLD_REQUIRED_LIBRARIES})
        unset(OLD_REQUIRED_LIBRARIES)

        if(NOT HAVE_STATIC_LIBGCC_SWITCH)
            message(FATAL_ERROR "Cannot static link libgcc")
        endif()
        set(LINKER_FLAGS ${LINKER_FLAGS} -static-libgcc)
    endif()

    option(ALSOFT_STATIC_STDCXX "Static link libstdc++" OFF)
    if(ALSOFT_STATIC_STDCXX)
        set(OLD_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
        set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} "-Wl,--push-state,-Bstatic,-lstdc++,--pop-state")
        check_cxx_source_compiles("int main() { }" HAVE_STATIC_LIBSTDCXX_SWITCH)
        set(CMAKE_REQUIRED_LIBRARIES ${OLD_REQUIRED_LIBRARIES})
        unset(OLD_REQUIRED_LIBRARIES)

        if(NOT HAVE_STATIC_LIBSTDCXX_SWITCH)
            message(FATAL_ERROR "Cannot static link libstdc++")
        endif()
        set(LINKER_FLAGS ${LINKER_FLAGS} "-Wl,--push-state,-Bstatic,-lstdc++,--pop-state")
    endif()

    if(WIN32)
        option(ALSOFT_STATIC_WINPTHREAD "Static link libwinpthread" OFF)
        if(ALSOFT_STATIC_WINPTHREAD)
            set(OLD_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
            set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} "-Wl,--push-state,-Bstatic,-lwinpthread,--pop-state")
            check_cxx_source_compiles("int main() { }" HAVE_STATIC_LIBWINPTHREAD_SWITCH)
            set(CMAKE_REQUIRED_LIBRARIES ${OLD_REQUIRED_LIBRARIES})
            unset(OLD_REQUIRED_LIBRARIES)

            if(NOT HAVE_STATIC_LIBWINPTHREAD_SWITCH)
                message(FATAL_ERROR "Cannot static link libwinpthread")
            endif()
            set(LINKER_FLAGS ${LINKER_FLAGS} "-Wl,--push-state,-Bstatic,-lwinpthread,--pop-state")
        endif()
    endif()
ENDIF()

# Set visibility/export options if available
IF(WIN32)
    SET(EXPORT_DECL "__declspec(dllexport)")
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
        CHECK_C_COMPILER_FLAG(-fvisibility=hidden HAVE_VISIBILITY_HIDDEN_SWITCH)
        IF(HAVE_VISIBILITY_HIDDEN_SWITCH)
            SET(C_FLAGS ${C_FLAGS} -fvisibility=hidden)
        ENDIF()
    ENDIF()

    SET(CMAKE_REQUIRED_FLAGS "${OLD_REQUIRED_FLAGS}")
ENDIF()


set(SSE2_SWITCH "")
set(SSE3_SWITCH "")
set(SSE4_1_SWITCH "")
set(FPU_NEON_SWITCH "")

set(OLD_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
if(NOT MSVC)
    # Yes GCC, really don't accept command line options you don't support
    set(CMAKE_REQUIRED_FLAGS "${OLD_REQUIRED_FLAGS} -Werror")
endif()
check_c_compiler_flag(-msse2 HAVE_MSSE2_SWITCH)
if(HAVE_MSSE2_SWITCH)
    set(SSE2_SWITCH "-msse2")
    check_c_compiler_flag(-msse3 HAVE_MSSE3_SWITCH)
    if(HAVE_MSSE3_SWITCH)
        set(SSE3_SWITCH "-msse3")
        check_c_compiler_flag(-msse4.1 HAVE_MSSE4_1_SWITCH)
        if(HAVE_MSSE4_1_SWITCH)
            set(SSE4_1_SWITCH "-msse4.1")
        endif()
    endif()
endif()
check_c_compiler_flag(-mfpu=neon HAVE_MFPU_NEON_SWITCH)
if(HAVE_MFPU_NEON_SWITCH)
    set(FPU_NEON_SWITCH "-mfpu=neon")
endif()
set(CMAKE_REQUIRED_FLAGS ${OLD_REQUIRED_FLAGS})
unset(OLD_REQUIRED_FLAGS)

check_include_file(xmmintrin.h HAVE_XMMINTRIN_H ${SSE2_SWITCH})
check_include_file(emmintrin.h HAVE_EMMINTRIN_H ${SSE2_SWITCH})
check_include_file(pmmintrin.h HAVE_PMMINTRIN_H ${SSE3_SWITCH})
check_include_file(smmintrin.h HAVE_SMMINTRIN_H ${SSE4_1_SWITCH})
check_include_file(arm_neon.h HAVE_ARM_NEON_H ${FPU_NEON_SWITCH})

set(SSE_FLAGS )
set(FPMATH_SET "0")
if(CMAKE_SIZEOF_VOID_P MATCHES "4" AND (SSE2_SWITCH OR MSVC))
    option(ALSOFT_ENABLE_SSE2_CODEGEN "Enable SSE2 code generation instead of x87 for 32-bit targets." TRUE)
    if(ALSOFT_ENABLE_SSE2_CODEGEN)
        if(SSE2_SWITCH)
            check_c_compiler_flag("${SSE2_SWITCH} -mfpmath=sse" HAVE_MFPMATH_SSE_2)
            if(HAVE_MFPMATH_SSE_2)
                set(SSE_FLAGS ${SSE_FLAGS} ${SSE2_SWITCH} -mfpmath=sse)
                set(C_FLAGS ${C_FLAGS} ${SSE_FLAGS})
                set(FPMATH_SET 2)
            endif()
        elseif(MSVC)
            check_c_compiler_flag("/arch:SSE2" HAVE_ARCH_SSE2)
            if(HAVE_ARCH_SSE2)
                set(SSE_FLAGS ${SSE_FLAGS} "/arch:SSE2")
                set(C_FLAGS ${C_FLAGS} ${SSE_FLAGS})
                set(FPMATH_SET 2)
            endif()
        endif()
    endif()
endif()

IF(HAVE_EMMINTRIN_H)
    SET(OLD_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
    FOREACH(flag_var ${SSE_FLAGS})
        SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${flag_var}")
    ENDFOREACH()

    CHECK_C_SOURCE_COMPILES("#include <emmintrin.h>
                             int main() {_mm_pause(); return 0;}" HAVE_SSE_INTRINSICS)

    SET(CMAKE_REQUIRED_FLAGS ${OLD_REQUIRED_FLAGS})
ENDIF()


CHECK_INCLUDE_FILE(malloc.h HAVE_MALLOC_H)
CHECK_INCLUDE_FILE(cpuid.h HAVE_CPUID_H)
CHECK_INCLUDE_FILE(intrin.h HAVE_INTRIN_H)
CHECK_INCLUDE_FILE(guiddef.h HAVE_GUIDDEF_H)
IF(NOT HAVE_GUIDDEF_H)
    CHECK_INCLUDE_FILE(initguid.h HAVE_INITGUID_H)
ENDIF()

# Some systems need libm for some math functions to work
SET(MATH_LIB )
CHECK_LIBRARY_EXISTS(m pow "" HAVE_LIBM)
IF(HAVE_LIBM)
    SET(MATH_LIB ${MATH_LIB} m)
    SET(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} m)
ENDIF()

# Some systems need to link with -lrt for clock_gettime as used by the common
# eaxmple functions.
SET(RT_LIB )
CHECK_LIBRARY_EXISTS(rt clock_gettime "" HAVE_LIBRT)
IF(HAVE_LIBRT)
    SET(RT_LIB rt)
ENDIF()

# Check for the dlopen API (for dynamicly loading backend libs)
IF(ALSOFT_DLOPEN)
    CHECK_INCLUDE_FILE(dlfcn.h HAVE_DLFCN_H)
    CHECK_LIBRARY_EXISTS(dl dlopen "" HAVE_LIBDL)
    IF(HAVE_LIBDL)
        SET(EXTRA_LIBS dl ${EXTRA_LIBS})
        SET(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} dl)
    ENDIF()
ENDIF()

# Check for a cpuid intrinsic
IF(HAVE_CPUID_H)
    CHECK_C_SOURCE_COMPILES("#include <cpuid.h>
        int main()
        {
            unsigned int eax, ebx, ecx, edx;
            return __get_cpuid(0, &eax, &ebx, &ecx, &edx);
        }" HAVE_GCC_GET_CPUID)
ENDIF()
IF(HAVE_INTRIN_H)
    CHECK_C_SOURCE_COMPILES("#include <intrin.h>
        int main()
        {
            int regs[4];
            __cpuid(regs, 0);
            return regs[0];
        }" HAVE_CPUID_INTRINSIC)
    CHECK_C_SOURCE_COMPILES("#include <intrin.h>
        int main()
        {
            unsigned long idx = 0;
            _BitScanForward64(&idx, 1);
            return idx;
        }" HAVE_BITSCANFORWARD64_INTRINSIC)
    CHECK_C_SOURCE_COMPILES("#include <intrin.h>
        int main()
        {
            unsigned long idx = 0;
            _BitScanForward(&idx, 1);
            return idx;
        }" HAVE_BITSCANFORWARD_INTRINSIC)
ENDIF()

CHECK_SYMBOL_EXISTS(posix_memalign   stdlib.h HAVE_POSIX_MEMALIGN)
CHECK_SYMBOL_EXISTS(_aligned_malloc  malloc.h HAVE__ALIGNED_MALLOC)
CHECK_SYMBOL_EXISTS(proc_pidpath     libproc.h HAVE_PROC_PIDPATH)

IF(NOT WIN32)
    # We need pthreads outside of Windows, for semaphores. It's also used to
    # set the priority and name of threads, when possible.
    CHECK_INCLUDE_FILE(pthread.h HAVE_PTHREAD_H)
    IF(NOT HAVE_PTHREAD_H)
        MESSAGE(FATAL_ERROR "PThreads is required for non-Windows builds!")
    ENDIF()

    CHECK_C_COMPILER_FLAG(-pthread HAVE_PTHREAD)
    IF(HAVE_PTHREAD)
        SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -pthread")
        SET(C_FLAGS ${C_FLAGS} -pthread)
        SET(LINKER_FLAGS ${LINKER_FLAGS} -pthread)
    ENDIF()

    CHECK_SYMBOL_EXISTS(pthread_setschedparam pthread.h HAVE_PTHREAD_SETSCHEDPARAM)

    # Some systems need pthread_np.h to get pthread_setname_np
    CHECK_INCLUDE_FILES("pthread.h;pthread_np.h" HAVE_PTHREAD_NP_H)
    IF(HAVE_PTHREAD_NP_H)
        CHECK_SYMBOL_EXISTS(pthread_setname_np "pthread.h;pthread_np.h" HAVE_PTHREAD_SETNAME_NP)
        IF(NOT HAVE_PTHREAD_SETNAME_NP)
            CHECK_SYMBOL_EXISTS(pthread_set_name_np "pthread.h;pthread_np.h" HAVE_PTHREAD_SET_NAME_NP)
        ELSE()
            CHECK_C_SOURCE_COMPILES("
#include <pthread.h>
#include <pthread_np.h>
int main()
{
    pthread_setname_np(\"testname\");
    return 0;
}"
                PTHREAD_SETNAME_NP_ONE_PARAM
            )
            CHECK_C_SOURCE_COMPILES("
#include <pthread.h>
#include <pthread_np.h>
int main()
{
    pthread_setname_np(pthread_self(), \"%s\", \"testname\");
    return 0;
}"
                PTHREAD_SETNAME_NP_THREE_PARAMS
            )
        ENDIF()
    ELSE()
        CHECK_SYMBOL_EXISTS(pthread_setname_np pthread.h HAVE_PTHREAD_SETNAME_NP)
        IF(NOT HAVE_PTHREAD_SETNAME_NP)
            CHECK_SYMBOL_EXISTS(pthread_set_name_np pthread.h HAVE_PTHREAD_SET_NAME_NP)
        ELSE()
            CHECK_C_SOURCE_COMPILES("
#include <pthread.h>
int main()
{
    pthread_setname_np(\"testname\");
    return 0;
}"
                PTHREAD_SETNAME_NP_ONE_PARAM
            )
            CHECK_C_SOURCE_COMPILES("
#include <pthread.h>
int main()
{
    pthread_setname_np(pthread_self(), \"%s\", \"testname\");
    return 0;
}"
                PTHREAD_SETNAME_NP_THREE_PARAMS
            )
        ENDIF()
    ENDIF()
ENDIF()

CHECK_SYMBOL_EXISTS(getopt unistd.h HAVE_GETOPT)


# Common sources used by both the OpenAL implementation library and potentially
# the OpenAL router.
SET(COMMON_OBJS
    common/albyte.h
    common/alcomplex.cpp
    common/alcomplex.h
    common/alexcpt.cpp
    common/alexcpt.h
    common/alfstream.cpp
    common/alfstream.h
    common/almalloc.cpp
    common/almalloc.h
    common/alnumeric.h
    common/aloptional.h
    common/alspan.h
    common/alstring.cpp
    common/alstring.h
    common/atomic.h
    common/bsinc_defs.h
    common/bsinc_tables.cpp
    common/bsinc_tables.h
    common/dynload.cpp
    common/dynload.h
    common/endiantest.h
    common/intrusive_ptr.h
    common/math_defs.h
    common/opthelpers.h
    common/polyphase_resampler.cpp
    common/polyphase_resampler.h
    common/pragmadefs.h
    common/strutils.cpp
    common/strutils.h
    common/threads.cpp
    common/threads.h
    common/vecmat.h
    common/vector.h
)
SET(OPENAL_OBJS
    al/auxeffectslot.cpp
    al/auxeffectslot.h
    al/buffer.cpp
    al/buffer.h
    al/effect.cpp
    al/effect.h
    al/error.cpp
    al/event.cpp
    al/event.h
    al/extension.cpp
    al/filter.cpp
    al/filter.h
    al/listener.cpp
    al/listener.h
    al/source.cpp
    al/source.h
    al/state.cpp
)
SET(ALC_OBJS
    alc/alc.cpp
    alc/alcmain.h
    alc/alu.cpp
    alc/alu.h
    alc/alconfig.cpp
    alc/alconfig.h
    alc/alcontext.h
    alc/ambdec.cpp
    alc/ambdec.h
    alc/ambidefs.h
    alc/bformatdec.cpp
    alc/bformatdec.h
    alc/bs2b.cpp
    alc/bs2b.h
    alc/compat.h
    alc/converter.cpp
    alc/converter.h
    alc/cpu_caps.cpp
    alc/cpu_caps.h
    alc/devformat.h
    alc/effects/base.h
    alc/effects/autowah.cpp
    alc/effects/chorus.cpp
    alc/effects/compressor.cpp
    alc/effects/dedicated.cpp
    alc/effects/distortion.cpp
    alc/effects/echo.cpp
    alc/effects/equalizer.cpp
    alc/effects/fshifter.cpp
    alc/effects/modulator.cpp
    alc/effects/null.cpp
    alc/effects/pshifter.cpp
    alc/effects/reverb.cpp
    alc/effects/vmorpher.cpp
    alc/filters/biquad.h
    alc/filters/biquad.cpp
    alc/filters/nfc.cpp
    alc/filters/nfc.h
    alc/filters/splitter.cpp
    alc/filters/splitter.h
    alc/fpu_ctrl.cpp
    alc/fpu_ctrl.h
    alc/front_stablizer.h
    alc/helpers.cpp
    alc/hrtf.cpp
    alc/hrtf.h
    alc/inprogext.h
    alc/logging.h
    alc/mastering.cpp
    alc/mastering.h
    alc/panning.cpp
    alc/ringbuffer.cpp
    alc/ringbuffer.h
    alc/uhjfilter.cpp
    alc/uhjfilter.h
    alc/uiddefs.cpp
    alc/voice.cpp
    alc/voice.h
    alc/mixer/defs.h
    alc/mixer/hrtfbase.h
    alc/mixer/mixer_c.cpp
)


SET(CPU_EXTS "Default")
SET(HAVE_SSE        0)
SET(HAVE_SSE2       0)
SET(HAVE_SSE3       0)
SET(HAVE_SSE4_1     0)
SET(HAVE_NEON       0)

# Check for SSE+SSE2 support
OPTION(ALSOFT_REQUIRE_SSE "Require SSE support" OFF)
OPTION(ALSOFT_REQUIRE_SSE2 "Require SSE2 support" OFF)
IF(HAVE_XMMINTRIN_H AND HAVE_EMMINTRIN_H)
    OPTION(ALSOFT_CPUEXT_SSE "Enable SSE support" ON)
    OPTION(ALSOFT_CPUEXT_SSE2 "Enable SSE2 support" ON)
    IF(ALSOFT_CPUEXT_SSE AND ALSOFT_CPUEXT_SSE2)
        SET(HAVE_SSE 1)
        SET(HAVE_SSE2 1)
        SET(ALC_OBJS  ${ALC_OBJS} alc/mixer/mixer_sse.cpp alc/mixer/mixer_sse2.cpp)
        IF(SSE2_SWITCH)
            SET_SOURCE_FILES_PROPERTIES(alc/mixer/mixer_sse.cpp alc/mixer/mixer_sse2.cpp
                PROPERTIES COMPILE_FLAGS "${SSE2_SWITCH}")
        ENDIF()
        SET(CPU_EXTS "${CPU_EXTS}, SSE, SSE2")
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_SSE AND NOT HAVE_SSE)
    MESSAGE(FATAL_ERROR "Failed to enabled required SSE CPU extensions")
ENDIF()
IF(ALSOFT_REQUIRE_SSE2 AND NOT HAVE_SSE2)
    MESSAGE(FATAL_ERROR "Failed to enable required SSE2 CPU extensions")
ENDIF()

OPTION(ALSOFT_REQUIRE_SSE3 "Require SSE3 support" OFF)
IF(HAVE_EMMINTRIN_H)
    OPTION(ALSOFT_CPUEXT_SSE3 "Enable SSE3 support" ON)
    IF(HAVE_SSE2 AND ALSOFT_CPUEXT_SSE3)
        SET(HAVE_SSE3 1)
        SET(ALC_OBJS  ${ALC_OBJS} alc/mixer/mixer_sse3.cpp)
        IF(SSE2_SWITCH)
            SET_SOURCE_FILES_PROPERTIES(alc/mixer/mixer_sse3.cpp PROPERTIES
                                        COMPILE_FLAGS "${SSE3_SWITCH}")
        ENDIF()
        SET(CPU_EXTS "${CPU_EXTS}, SSE3")
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_SSE3 AND NOT HAVE_SSE3)
    MESSAGE(FATAL_ERROR "Failed to enable required SSE3 CPU extensions")
ENDIF()

OPTION(ALSOFT_REQUIRE_SSE4_1 "Require SSE4.1 support" OFF)
IF(HAVE_SMMINTRIN_H)
    OPTION(ALSOFT_CPUEXT_SSE4_1 "Enable SSE4.1 support" ON)
    IF(HAVE_SSE3 AND ALSOFT_CPUEXT_SSE4_1)
        SET(HAVE_SSE4_1 1)
        SET(ALC_OBJS  ${ALC_OBJS} alc/mixer/mixer_sse41.cpp)
        IF(SSE4_1_SWITCH)
            SET_SOURCE_FILES_PROPERTIES(alc/mixer/mixer_sse41.cpp PROPERTIES
                                        COMPILE_FLAGS "${SSE4_1_SWITCH}")
        ENDIF()
        SET(CPU_EXTS "${CPU_EXTS}, SSE4.1")
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_SSE4_1 AND NOT HAVE_SSE4_1)
    MESSAGE(FATAL_ERROR "Failed to enable required SSE4.1 CPU extensions")
ENDIF()

# Check for ARM Neon support
OPTION(ALSOFT_REQUIRE_NEON "Require ARM Neon support" OFF)
IF(HAVE_ARM_NEON_H)
    OPTION(ALSOFT_CPUEXT_NEON "Enable ARM Neon support" ON)
    IF(ALSOFT_CPUEXT_NEON)
        SET(HAVE_NEON 1)
        SET(ALC_OBJS  ${ALC_OBJS} alc/mixer/mixer_neon.cpp)
        IF(FPU_NEON_SWITCH)
            SET_SOURCE_FILES_PROPERTIES(alc/mixer/mixer_neon.cpp PROPERTIES
                                        COMPILE_FLAGS "${FPU_NEON_SWITCH}")
        ENDIF()
        SET(CPU_EXTS "${CPU_EXTS}, Neon")
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_NEON AND NOT HAVE_NEON)
    MESSAGE(FATAL_ERROR "Failed to enabled required ARM Neon CPU extensions")
ENDIF()


set(HAVE_ALSA       0)
set(HAVE_OSS        0)
set(HAVE_SOLARIS    0)
set(HAVE_SNDIO      0)
set(HAVE_DSOUND     0)
set(HAVE_WASAPI     0)
set(HAVE_WINMM      0)
set(HAVE_PORTAUDIO  0)
set(HAVE_PULSEAUDIO 0)
set(HAVE_COREAUDIO  0)
set(HAVE_OPENSL     0)
set(HAVE_OBOE       0)
set(HAVE_WAVE       0)
set(HAVE_SDL2       0)

if(WIN32 OR HAVE_DLFCN_H)
    set(IS_LINKED "")
    macro(ADD_BACKEND_LIBS _LIBS)
    endmacro()
else()
    set(IS_LINKED " (linked)")
    macro(ADD_BACKEND_LIBS _LIBS)
        set(EXTRA_LIBS ${_LIBS} ${EXTRA_LIBS})
    endmacro()
endif()

set(BACKENDS "")
set(ALC_OBJS  ${ALC_OBJS}
    alc/backends/base.cpp
    alc/backends/base.h
    # Default backends, always available
    alc/backends/loopback.cpp
    alc/backends/loopback.h
    alc/backends/null.cpp
    alc/backends/null.h
)

# Check ALSA backend
OPTION(ALSOFT_REQUIRE_ALSA "Require ALSA backend" OFF)
FIND_PACKAGE(ALSA)
IF(ALSA_FOUND)
    OPTION(ALSOFT_BACKEND_ALSA "Enable ALSA backend" ON)
    IF(ALSOFT_BACKEND_ALSA)
        SET(HAVE_ALSA 1)
        SET(BACKENDS  "${BACKENDS} ALSA${IS_LINKED},")
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/alsa.cpp alc/backends/alsa.h)
        ADD_BACKEND_LIBS(${ALSA_LIBRARIES})
        SET(INC_PATHS ${INC_PATHS} ${ALSA_INCLUDE_DIRS})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_ALSA AND NOT HAVE_ALSA)
    MESSAGE(FATAL_ERROR "Failed to enabled required ALSA backend")
ENDIF()

# Check OSS backend
OPTION(ALSOFT_REQUIRE_OSS "Require OSS backend" OFF)
FIND_PACKAGE(OSS)
IF(OSS_FOUND)
    OPTION(ALSOFT_BACKEND_OSS "Enable OSS backend" ON)
    IF(ALSOFT_BACKEND_OSS)
        SET(HAVE_OSS 1)
        SET(BACKENDS  "${BACKENDS} OSS,")
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/oss.cpp alc/backends/oss.h)
        IF(OSS_LIBRARIES)
            SET(EXTRA_LIBS ${OSS_LIBRARIES} ${EXTRA_LIBS})
        ENDIF()
        SET(INC_PATHS ${INC_PATHS} ${OSS_INCLUDE_DIRS})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_OSS AND NOT HAVE_OSS)
    MESSAGE(FATAL_ERROR "Failed to enabled required OSS backend")
ENDIF()

# Check Solaris backend
OPTION(ALSOFT_REQUIRE_SOLARIS "Require Solaris backend" OFF)
FIND_PACKAGE(AudioIO)
IF(AUDIOIO_FOUND)
    OPTION(ALSOFT_BACKEND_SOLARIS "Enable Solaris backend" ON)
    IF(ALSOFT_BACKEND_SOLARIS)
        SET(HAVE_SOLARIS 1)
        SET(BACKENDS  "${BACKENDS} Solaris,")
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/solaris.cpp alc/backends/solaris.h)
        SET(INC_PATHS ${INC_PATHS} ${AUDIOIO_INCLUDE_DIRS})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_SOLARIS AND NOT HAVE_SOLARIS)
    MESSAGE(FATAL_ERROR "Failed to enabled required Solaris backend")
ENDIF()

# Check SndIO backend
OPTION(ALSOFT_REQUIRE_SNDIO "Require SndIO backend" OFF)
FIND_PACKAGE(SoundIO)
IF(SOUNDIO_FOUND)
    OPTION(ALSOFT_BACKEND_SNDIO "Enable SndIO backend" ON)
    IF(ALSOFT_BACKEND_SNDIO)
        SET(HAVE_SNDIO 1)
        SET(BACKENDS  "${BACKENDS} SndIO (linked),")
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/sndio.cpp alc/backends/sndio.h)
        SET(EXTRA_LIBS ${SOUNDIO_LIBRARIES} ${EXTRA_LIBS})
        SET(INC_PATHS ${INC_PATHS} ${SOUNDIO_INCLUDE_DIRS})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_SNDIO AND NOT HAVE_SNDIO)
    MESSAGE(FATAL_ERROR "Failed to enabled required SndIO backend")
ENDIF()

# Check Windows-only backends
OPTION(ALSOFT_REQUIRE_WINMM "Require Windows Multimedia backend" OFF)
OPTION(ALSOFT_REQUIRE_DSOUND "Require DirectSound backend" OFF)
OPTION(ALSOFT_REQUIRE_WASAPI "Require WASAPI backend" OFF)
IF(WIN32)
    SET(WINSDK_LIB_DIRS )
    SET(WINSDK_INCLUDE_DIRS )
    FIND_PACKAGE(WindowsSDK)
    IF(WINDOWSSDK_FOUND)
        get_windowssdk_library_dirs(${WINDOWSSDK_PREFERRED_DIR} WINSDK_LIB_DIRS)
        get_windowssdk_include_dirs(${WINDOWSSDK_PREFERRED_DIR} WINSDK_INCLUDE_DIRS)
    ENDIF()

    # Check MMSystem backend
    check_include_files("windows.h;mmsystem.h" HAVE_MMSYSTEM_H)
    find_library(WINMM_LIBRARY NAMES winmm
        PATHS ${WINSDK_LIB_DIRS})
    if(HAVE_MMSYSTEM_H AND WINMM_LIBRARY)
        option(ALSOFT_BACKEND_WINMM "Enable Windows Multimedia backend" ON)
        if(ALSOFT_BACKEND_WINMM)
            set(HAVE_WINMM 1)
            set(BACKENDS  "${BACKENDS} WinMM,")
            set(ALC_OBJS  ${ALC_OBJS} alc/backends/winmm.cpp alc/backends/winmm.h)
            set(EXTRA_LIBS ${WINMM_LIBRARY} ${EXTRA_LIBS})
        endif()
    endif()

    # Check DSound backend
    FIND_PACKAGE(DSound)
    IF(DSOUND_FOUND)
        OPTION(ALSOFT_BACKEND_DSOUND "Enable DirectSound backend" ON)
        IF(ALSOFT_BACKEND_DSOUND)
            SET(HAVE_DSOUND 1)
            SET(BACKENDS  "${BACKENDS} DirectSound${IS_LINKED},")
            SET(ALC_OBJS  ${ALC_OBJS} alc/backends/dsound.cpp alc/backends/dsound.h)
            ADD_BACKEND_LIBS(${DSOUND_LIBRARIES})
            SET(INC_PATHS ${INC_PATHS} ${DSOUND_INCLUDE_DIRS})
        ENDIF()
    ENDIF()

    # Check for WASAPI backend
    CHECK_INCLUDE_FILE(mmdeviceapi.h HAVE_MMDEVICEAPI_H)
    IF(HAVE_MMDEVICEAPI_H)
        OPTION(ALSOFT_BACKEND_WASAPI "Enable WASAPI backend" ON)
        IF(ALSOFT_BACKEND_WASAPI)
            SET(HAVE_WASAPI 1)
            SET(BACKENDS  "${BACKENDS} WASAPI,")
            SET(ALC_OBJS  ${ALC_OBJS} alc/backends/wasapi.cpp alc/backends/wasapi.h)
        ENDIF()
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_WINMM AND NOT HAVE_WINMM)
    MESSAGE(FATAL_ERROR "Failed to enabled required WinMM backend")
ENDIF()
IF(ALSOFT_REQUIRE_DSOUND AND NOT HAVE_DSOUND)
    MESSAGE(FATAL_ERROR "Failed to enabled required DSound backend")
ENDIF()
IF(ALSOFT_REQUIRE_WASAPI AND NOT HAVE_WASAPI)
    MESSAGE(FATAL_ERROR "Failed to enabled required WASAPI backend")
ENDIF()

# Check PortAudio backend
OPTION(ALSOFT_REQUIRE_PORTAUDIO "Require PortAudio backend" OFF)
FIND_PACKAGE(PortAudio)
IF(PORTAUDIO_FOUND)
    OPTION(ALSOFT_BACKEND_PORTAUDIO "Enable PortAudio backend" ON)
    IF(ALSOFT_BACKEND_PORTAUDIO)
        SET(HAVE_PORTAUDIO 1)
        SET(BACKENDS  "${BACKENDS} PortAudio${IS_LINKED},")
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/portaudio.cpp alc/backends/portaudio.h)
        ADD_BACKEND_LIBS(${PORTAUDIO_LIBRARIES})
        SET(INC_PATHS ${INC_PATHS} ${PORTAUDIO_INCLUDE_DIRS})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_PORTAUDIO AND NOT HAVE_PORTAUDIO)
    MESSAGE(FATAL_ERROR "Failed to enabled required PortAudio backend")
ENDIF()

# Check PulseAudio backend
OPTION(ALSOFT_REQUIRE_PULSEAUDIO "Require PulseAudio backend" OFF)
FIND_PACKAGE(PulseAudio)
IF(PULSEAUDIO_FOUND)
    OPTION(ALSOFT_BACKEND_PULSEAUDIO "Enable PulseAudio backend" ON)
    IF(ALSOFT_BACKEND_PULSEAUDIO)
        SET(HAVE_PULSEAUDIO 1)
        SET(BACKENDS  "${BACKENDS} PulseAudio${IS_LINKED},")
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/pulseaudio.cpp alc/backends/pulseaudio.h)
        ADD_BACKEND_LIBS(${PULSEAUDIO_LIBRARIES})
        SET(INC_PATHS ${INC_PATHS} ${PULSEAUDIO_INCLUDE_DIRS})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_PULSEAUDIO AND NOT HAVE_PULSEAUDIO)
    MESSAGE(FATAL_ERROR "Failed to enabled required PulseAudio backend")
ENDIF()

# Check JACK backend
OPTION(ALSOFT_REQUIRE_JACK "Require JACK backend" OFF)
FIND_PACKAGE(JACK)
IF(JACK_FOUND)
    OPTION(ALSOFT_BACKEND_JACK "Enable JACK backend" ON)
    IF(ALSOFT_BACKEND_JACK)
        SET(HAVE_JACK 1)
        SET(BACKENDS  "${BACKENDS} JACK${IS_LINKED},")
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/jack.cpp alc/backends/jack.h)
        ADD_BACKEND_LIBS(${JACK_LIBRARIES})
        SET(INC_PATHS ${INC_PATHS} ${JACK_INCLUDE_DIRS})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_JACK AND NOT HAVE_JACK)
    MESSAGE(FATAL_ERROR "Failed to enabled required JACK backend")
ENDIF()

# Check CoreAudio backend
OPTION(ALSOFT_REQUIRE_COREAUDIO "Require CoreAudio backend" OFF)
FIND_LIBRARY(COREAUDIO_FRAMEWORK NAMES CoreAudio)
FIND_PATH(AUDIOUNIT_INCLUDE_DIR NAMES AudioUnit/AudioUnit.h)
IF(COREAUDIO_FRAMEWORK AND AUDIOUNIT_INCLUDE_DIR)
    OPTION(ALSOFT_BACKEND_COREAUDIO "Enable CoreAudio backend" ON)
    IF(ALSOFT_BACKEND_COREAUDIO)
        SET(HAVE_COREAUDIO 1)
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/coreaudio.cpp alc/backends/coreaudio.h)
        SET(BACKENDS  "${BACKENDS} CoreAudio,")
        SET(EXTRA_LIBS ${COREAUDIO_FRAMEWORK} /System/Library/Frameworks/AudioUnit.framework
            /System/Library/Frameworks/ApplicationServices.framework ${EXTRA_LIBS})

        # Some versions of OSX may need the AudioToolbox framework. Add it if
        # it's found.
        FIND_LIBRARY(AUDIOTOOLBOX_LIBRARY NAMES AudioToolbox)
        IF(AUDIOTOOLBOX_LIBRARY)
            SET(EXTRA_LIBS ${AUDIOTOOLBOX_LIBRARY} ${EXTRA_LIBS})
        ENDIF()

        SET(INC_PATHS ${INC_PATHS} ${AUDIOUNIT_INCLUDE_DIR})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_COREAUDIO AND NOT HAVE_COREAUDIO)
    MESSAGE(FATAL_ERROR "Failed to enabled required CoreAudio backend")
ENDIF()

# Check for OpenSL (Android) backend
OPTION(ALSOFT_REQUIRE_OPENSL "Require OpenSL backend" OFF)
FIND_PACKAGE(OpenSL)
IF(OPENSL_FOUND)
    OPTION(ALSOFT_BACKEND_OPENSL "Enable OpenSL backend" ON)
    IF(ALSOFT_BACKEND_OPENSL)
        SET(HAVE_OPENSL 1)
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/opensl.cpp alc/backends/opensl.h)
        SET(BACKENDS  "${BACKENDS} OpenSL,")
        SET(EXTRA_LIBS ${OPENSL_LIBRARIES} ${EXTRA_LIBS})
        SET(INC_PATHS ${INC_PATHS} ${OPENSL_INCLUDE_DIRS})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_OPENSL AND NOT HAVE_OPENSL)
    MESSAGE(FATAL_ERROR "Failed to enabled required OpenSL backend")
ENDIF()

# Check for Oboe (Android) backend
set(OBOE_TARGET )
if(ANDROID)
    set(OBOE_SOURCE "" CACHE STRING "Source directory for Oboe.")
    if(OBOE_SOURCE)
        # Force Oboe to build with hidden symbols. Don't want to be exporting
        # them from OpenAL.
        set(OLD_CXX_FLAGS ${CMAKE_CXX_FLAGS})
        check_cxx_compiler_flag(-fvisibility=hidden HAVE_VISIBILITY_HIDDEN_SWITCH)
        if(HAVE_VISIBILITY_HIDDEN_SWITCH)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
        endif()
        add_subdirectory(${OBOE_SOURCE} ./oboe)
        set(CMAKE_CXX_FLAGS ${OLD_CXX_FLAGS})
        unset(OLD_CXX_FLAGS)

        set(OBOE_TARGET oboe)
    else()
        find_package(Oboe)
        if(OBOE_FOUND)
            set(OBOE_TARGET "oboe::oboe")
        endif()
    endif()
endif()

option(ALSOFT_REQUIRE_OBOE "Require Oboe backend" OFF)
if(OBOE_TARGET)
    option(ALSOFT_BACKEND_OBOE "Enable Oboe backend" ON)
    if(ALSOFT_BACKEND_OBOE)
        set(HAVE_OBOE 1)
        set(ALC_OBJS  ${ALC_OBJS} alc/backends/oboe.cpp alc/backends/oboe.h)
        set(BACKENDS  "${BACKENDS} Oboe,")
        set(EXTRA_LIBS ${OBOE_TARGET} ${EXTRA_LIBS})
    endif()
endif()
if(ALSOFT_REQUIRE_OBOE AND NOT HAVE_OBOE)
    message(FATAL_ERROR "Failed to enabled required Oboe backend")
endif()

# Check for SDL2 backend
OPTION(ALSOFT_REQUIRE_SDL2 "Require SDL2 backend" OFF)
FIND_PACKAGE(SDL2)
IF(SDL2_FOUND)
    # Off by default, since it adds a runtime dependency
    OPTION(ALSOFT_BACKEND_SDL2 "Enable SDL2 backend" OFF)
    IF(ALSOFT_BACKEND_SDL2)
        SET(HAVE_SDL2 1)
        SET(ALC_OBJS  ${ALC_OBJS} alc/backends/sdl2.cpp alc/backends/sdl2.h)
        SET(BACKENDS  "${BACKENDS} SDL2,")
        SET(EXTRA_LIBS ${SDL2_LIBRARY} ${EXTRA_LIBS})
        SET(INC_PATHS ${INC_PATHS} ${SDL2_INCLUDE_DIR})
    ENDIF()
ENDIF()
IF(ALSOFT_REQUIRE_SDL2 AND NOT SDL2_FOUND)
    MESSAGE(FATAL_ERROR "Failed to enabled required SDL2 backend")
ENDIF()

# Optionally enable the Wave Writer backend
OPTION(ALSOFT_BACKEND_WAVE "Enable Wave Writer backend" ON)
IF(ALSOFT_BACKEND_WAVE)
    SET(HAVE_WAVE 1)
    SET(ALC_OBJS  ${ALC_OBJS} alc/backends/wave.cpp alc/backends/wave.h)
    SET(BACKENDS  "${BACKENDS} WaveFile,")
ENDIF()

# This is always available
SET(BACKENDS  "${BACKENDS} Null")


FIND_PACKAGE(Git)
IF(ALSOFT_UPDATE_BUILD_VERSION AND GIT_FOUND AND EXISTS "${OpenAL_SOURCE_DIR}/.git")
    # Get the current working branch and its latest abbreviated commit hash
    ADD_CUSTOM_TARGET(build_version
        ${CMAKE_COMMAND} -D GIT_EXECUTABLE=${GIT_EXECUTABLE} -D LIB_VERSION=${LIB_VERSION}
            -D LIB_VERSION_NUM=${LIB_VERSION_NUM} -D SRC=${OpenAL_SOURCE_DIR}/version.h.in
            -D DST=${OpenAL_BINARY_DIR}/version.h -P ${OpenAL_SOURCE_DIR}/version.cmake
        WORKING_DIRECTORY "${OpenAL_SOURCE_DIR}"
        VERBATIM
    )
ELSE()
    SET(GIT_BRANCH "UNKNOWN")
    SET(GIT_COMMIT_HASH "unknown")
    CONFIGURE_FILE(
        "${OpenAL_SOURCE_DIR}/version.h.in"
        "${OpenAL_BINARY_DIR}/version.h")
ENDIF()


option(ALSOFT_EMBED_HRTF_DATA "Embed the HRTF data files (increases library footprint)" ON)
if(ALSOFT_EMBED_HRTF_DATA)
    MACRO(make_hrtf_header FILENAME VARNAME)
        SET(infile  "${OpenAL_SOURCE_DIR}/hrtf/${FILENAME}")
        SET(outfile  "${OpenAL_BINARY_DIR}/${VARNAME}.h")

        ADD_CUSTOM_COMMAND(OUTPUT "${outfile}"
            COMMAND ${CMAKE_COMMAND} -D "INPUT_FILE=${infile}" -D "OUTPUT_FILE=${outfile}"
                -D "VARIABLE_NAME=${VARNAME}" -P "${CMAKE_MODULE_PATH}/bin2h.script.cmake"
            WORKING_DIRECTORY "${OpenAL_SOURCE_DIR}"
            DEPENDS "${infile}" "${CMAKE_MODULE_PATH}/bin2h.script.cmake"
            VERBATIM
        )
        SET(ALC_OBJS  ${ALC_OBJS} "${outfile}")
    ENDMACRO()

    make_hrtf_header("Default HRTF.mhr" "hrtf_default")
endif()


IF(ALSOFT_UTILS AND NOT ALSOFT_NO_CONFIG_UTIL)
    find_package(Qt5Widgets)
ENDIF()
IF(ALSOFT_EXAMPLES)
    FIND_PACKAGE(SndFile)
    FIND_PACKAGE(SDL2)
    IF(SDL2_FOUND)
        FIND_PACKAGE(FFmpeg COMPONENTS AVFORMAT AVCODEC AVUTIL SWSCALE SWRESAMPLE)
    ENDIF()
ENDIF()

IF(NOT WIN32)
    SET(LIBNAME "openal")
ELSE()
    SET(LIBNAME "OpenAL32")
ENDIF()

# Needed for openal.pc.in
SET(prefix ${CMAKE_INSTALL_PREFIX})
SET(exec_prefix "\${prefix}")
SET(libdir "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
SET(bindir "\${exec_prefix}/${CMAKE_INSTALL_BINDIR}")
SET(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
SET(PACKAGE_VERSION "${LIB_VERSION}")
SET(PKG_CONFIG_CFLAGS )
SET(PKG_CONFIG_PRIVATE_LIBS )
IF(LIBTYPE STREQUAL "STATIC")
    SET(PKG_CONFIG_CFLAGS -DAL_LIBTYPE_STATIC)
    FOREACH(FLAG  ${LINKER_FLAGS} ${EXTRA_LIBS} ${MATH_LIB})
        # If this is already a linker flag, or is a full path+file, add it
        # as-is. Otherwise, it's a name intended to be dressed as -lname.
        IF(FLAG MATCHES "^-.*" OR EXISTS "${FLAG}")
            SET(PKG_CONFIG_PRIVATE_LIBS "${PKG_CONFIG_PRIVATE_LIBS} ${FLAG}")
        ELSE()
            SET(PKG_CONFIG_PRIVATE_LIBS "${PKG_CONFIG_PRIVATE_LIBS} -l${FLAG}")
        ENDIF()
    ENDFOREACH()
ENDIF()

# End configuration
CONFIGURE_FILE(
    "${OpenAL_SOURCE_DIR}/config.h.in"
    "${OpenAL_BINARY_DIR}/config.h")
CONFIGURE_FILE(
    "${OpenAL_SOURCE_DIR}/openal.pc.in"
    "${OpenAL_BINARY_DIR}/openal.pc"
    @ONLY)


ADD_LIBRARY(common STATIC EXCLUDE_FROM_ALL ${COMMON_OBJS})
TARGET_INCLUDE_DIRECTORIES(common PRIVATE ${OpenAL_BINARY_DIR} ${OpenAL_SOURCE_DIR}/include)
TARGET_COMPILE_DEFINITIONS(common PRIVATE ${CPP_DEFS})
TARGET_COMPILE_OPTIONS(common PRIVATE ${C_FLAGS})
SET_TARGET_PROPERTIES(common PROPERTIES POSITION_INDEPENDENT_CODE TRUE)


UNSET(HAS_ROUTER)
SET(IMPL_TARGET OpenAL) # Either OpenAL or soft_oal.

# Build main library
IF(LIBTYPE STREQUAL "STATIC")
    add_library(${IMPL_TARGET} STATIC ${COMMON_OBJS} ${OPENAL_OBJS} ${ALC_OBJS})
    target_compile_definitions(${IMPL_TARGET} PUBLIC AL_LIBTYPE_STATIC)
    target_link_libraries(${IMPL_TARGET} PRIVATE ${LINKER_FLAGS} ${EXTRA_LIBS} ${MATH_LIB})
    if(WIN32)
        # This option is for static linking OpenAL Soft into another project
        # that already defines the IDs. It is up to that project to ensure all
        # required IDs are defined.
        option(ALSOFT_NO_UID_DEFS "Do not define GUIDs, IIDs, CLSIDs, or PropertyKeys" OFF)
        if(ALSOFT_NO_UID_DEFS)
            target_compile_definitions(${IMPL_TARGET} PRIVATE AL_NO_UID_DEFS)
        endif()
    endif()
ELSE()
    SET(RC_CONFIG resources/openal32.rc)
    IF(WIN32 AND ALSOFT_BUILD_ROUTER)
        ADD_LIBRARY(OpenAL SHARED
            resources/router.rc
            router/router.cpp
            router/router.h
            router/alc.cpp
            router/al.cpp
        )
        TARGET_COMPILE_DEFINITIONS(OpenAL
            PRIVATE AL_BUILD_LIBRARY AL_ALEXT_PROTOTYPES ${CPP_DEFS})
        TARGET_COMPILE_OPTIONS(OpenAL PRIVATE ${C_FLAGS})
        TARGET_LINK_LIBRARIES(OpenAL PRIVATE common ${LINKER_FLAGS})
        TARGET_INCLUDE_DIRECTORIES(OpenAL
          PUBLIC
            $<BUILD_INTERFACE:${OpenAL_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
          PRIVATE
            ${OpenAL_SOURCE_DIR}/common
            ${OpenAL_BINARY_DIR}
        )
        SET_TARGET_PROPERTIES(OpenAL PROPERTIES PREFIX "")
        SET_TARGET_PROPERTIES(OpenAL PROPERTIES OUTPUT_NAME ${LIBNAME})
        IF(TARGET build_version)
            ADD_DEPENDENCIES(OpenAL build_version)
        ENDIF()
        SET(HAS_ROUTER 1)

        SET(LIBNAME "soft_oal")
        SET(IMPL_TARGET soft_oal)
        SET(RC_CONFIG resources/soft_oal.rc)
    ENDIF()

    ADD_LIBRARY(${IMPL_TARGET} SHARED ${OPENAL_OBJS} ${ALC_OBJS} ${RC_CONFIG})
    IF(WIN32)
        SET_TARGET_PROPERTIES(${IMPL_TARGET} PROPERTIES PREFIX "")
    ENDIF()
    TARGET_LINK_LIBRARIES(${IMPL_TARGET} PRIVATE common ${LINKER_FLAGS} ${EXTRA_LIBS} ${MATH_LIB})
ENDIF()

TARGET_INCLUDE_DIRECTORIES(${IMPL_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${OpenAL_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${INC_PATHS}
    ${OpenAL_BINARY_DIR}
    ${OpenAL_SOURCE_DIR}
    ${OpenAL_SOURCE_DIR}/alc
    ${OpenAL_SOURCE_DIR}/common
)

SET_TARGET_PROPERTIES(${IMPL_TARGET} PROPERTIES OUTPUT_NAME ${LIBNAME}
    VERSION ${LIB_VERSION}
    SOVERSION ${LIB_MAJOR_VERSION}
)
TARGET_COMPILE_DEFINITIONS(${IMPL_TARGET}
    PRIVATE AL_BUILD_LIBRARY AL_ALEXT_PROTOTYPES ${CPP_DEFS})
TARGET_COMPILE_OPTIONS(${IMPL_TARGET} PRIVATE ${C_FLAGS})

IF(TARGET build_version)
    ADD_DEPENDENCIES(${IMPL_TARGET} build_version)
ENDIF()

IF(WIN32 AND MINGW AND ALSOFT_BUILD_IMPORT_LIB AND NOT LIBTYPE STREQUAL "STATIC")
    FIND_PROGRAM(SED_EXECUTABLE NAMES sed DOC "sed executable")
    FIND_PROGRAM(DLLTOOL_EXECUTABLE NAMES "${DLLTOOL}" DOC "dlltool executable")
    IF(NOT SED_EXECUTABLE OR NOT DLLTOOL_EXECUTABLE)
        MESSAGE(STATUS "")
        IF(NOT SED_EXECUTABLE)
            MESSAGE(STATUS "WARNING: Cannot find sed, disabling .def/.lib generation")
        ENDIF()
        IF(NOT DLLTOOL_EXECUTABLE)
            MESSAGE(STATUS "WARNING: Cannot find dlltool, disabling .def/.lib generation")
        ENDIF()
    ELSE()
        SET_PROPERTY(TARGET OpenAL APPEND_STRING PROPERTY
            LINK_FLAGS " -Wl,--output-def,OpenAL32.def")
        ADD_CUSTOM_COMMAND(TARGET OpenAL POST_BUILD
            COMMAND "${SED_EXECUTABLE}" -i -e "s/ @[^ ]*//" OpenAL32.def
            COMMAND "${DLLTOOL_EXECUTABLE}" -d OpenAL32.def -l OpenAL32.lib -D OpenAL32.dll
            COMMENT "Stripping ordinals from OpenAL32.def and generating OpenAL32.lib..."
            VERBATIM
        )
    ENDIF()
ENDIF()

install(TARGETS OpenAL EXPORT OpenAL
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} ${CMAKE_INSTALL_INCLUDEDIR}/AL)
export(TARGETS OpenAL
    NAMESPACE OpenAL::
    FILE OpenALConfig.cmake)
install(EXPORT OpenAL
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenAL
    NAMESPACE OpenAL::
    FILE OpenALConfig.cmake)
install(DIRECTORY include/AL
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${OpenAL_BINARY_DIR}/openal.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
if(TARGET soft_oal)
    install(TARGETS soft_oal
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()


if(HAS_ROUTER)
    message(STATUS "")
    message(STATUS "Building DLL router")
endif()

MESSAGE(STATUS "")
MESSAGE(STATUS "Building OpenAL with support for the following backends:")
MESSAGE(STATUS "    ${BACKENDS}")
MESSAGE(STATUS "")
MESSAGE(STATUS "Building with support for CPU extensions:")
MESSAGE(STATUS "    ${CPU_EXTS}")
MESSAGE(STATUS "")
IF(FPMATH_SET)
    MESSAGE(STATUS "Building with SSE${FPMATH_SET} codegen")
    MESSAGE(STATUS "")
ENDIF()

if(ALSOFT_EMBED_HRTF_DATA)
    message(STATUS "Embedding HRTF datasets")
    message(STATUS "")
endif()

# Install alsoft.conf configuration file
if(ALSOFT_INSTALL_CONFIG)
    install(FILES alsoftrc.sample
        DESTINATION ${CMAKE_INSTALL_DATADIR}/openal)
    message(STATUS "Installing sample configuration")
    message(STATUS "")
endif()

# Install HRTF definitions
if(ALSOFT_INSTALL_HRTF_DEFS)
    install(DIRECTORY hrtf
        DESTINATION ${CMAKE_INSTALL_DATADIR}/openal)
    message(STATUS "Installing HRTF definitions")
    message(STATUS "")
endif()

# Install AmbDec presets
if(ALSOFT_INSTALL_AMBDEC_PRESETS)
    install(DIRECTORY presets
        DESTINATION ${CMAKE_INSTALL_DATADIR}/openal)
    message(STATUS "Installing AmbDec presets")
    message(STATUS "")
endif()

set(EXTRA_INSTALLS )
if(ALSOFT_UTILS)
    ADD_EXECUTABLE(openal-info utils/openal-info.c)
    TARGET_INCLUDE_DIRECTORIES(openal-info PRIVATE ${OpenAL_SOURCE_DIR}/common)
    TARGET_COMPILE_OPTIONS(openal-info PRIVATE ${C_FLAGS})
    TARGET_LINK_LIBRARIES(openal-info PRIVATE ${LINKER_FLAGS} OpenAL)
    if(ALSOFT_INSTALL_EXAMPLES)
        set(EXTRA_INSTALLS ${EXTRA_INSTALLS} openal-info)
    endif()

    find_package(MySOFA)
    if(MYSOFA_FOUND)
        set(SOFA_SUPPORT_SRCS
            utils/sofa-support.cpp
            utils/sofa-support.h)
        add_library(sofa-support STATIC EXCLUDE_FROM_ALL ${SOFA_SUPPORT_SRCS})
        target_compile_definitions(sofa-support PRIVATE ${CPP_DEFS})
        target_include_directories(sofa-support PUBLIC ${OpenAL_SOURCE_DIR}/common)
        target_compile_options(sofa-support PRIVATE ${C_FLAGS})
        target_link_libraries(sofa-support PUBLIC common MySOFA::MySOFA PRIVATE ${LINKER_FLAGS})

        set(MAKEMHR_SRCS
            utils/makemhr/loaddef.cpp
            utils/makemhr/loaddef.h
            utils/makemhr/loadsofa.cpp
            utils/makemhr/loadsofa.h
            utils/makemhr/makemhr.cpp
            utils/makemhr/makemhr.h)
        if(NOT HAVE_GETOPT)
            set(MAKEMHR_SRCS  ${MAKEMHR_SRCS} utils/getopt.c utils/getopt.h)
        endif()
        add_executable(makemhr ${MAKEMHR_SRCS})
        target_compile_definitions(makemhr PRIVATE ${CPP_DEFS})
        target_include_directories(makemhr
            PRIVATE ${OpenAL_BINARY_DIR} ${OpenAL_SOURCE_DIR}/utils)
        target_compile_options(makemhr PRIVATE ${C_FLAGS})
        target_link_libraries(makemhr PRIVATE ${LINKER_FLAGS} sofa-support)
        if(ALSOFT_INSTALL_EXAMPLES)
            set(EXTRA_INSTALLS ${EXTRA_INSTALLS} makemhr)
        endif()

        set(SOFAINFO_SRCS  utils/sofa-info.cpp)
        add_executable(sofa-info ${SOFAINFO_SRCS})
        target_compile_definitions(sofa-info PRIVATE ${CPP_DEFS})
        target_include_directories(sofa-info PRIVATE ${OpenAL_SOURCE_DIR}/utils)
        target_compile_options(sofa-info PRIVATE ${C_FLAGS})
        target_link_libraries(sofa-info PRIVATE ${LINKER_FLAGS} sofa-support)
    endif()
    message(STATUS "Building utility programs")

    if(NOT ALSOFT_NO_CONFIG_UTIL)
        add_subdirectory(utils/alsoft-config)
    endif()
    message(STATUS "")
endif()


# Add a static library with common functions used by multiple example targets
ADD_LIBRARY(ex-common STATIC EXCLUDE_FROM_ALL
    examples/common/alhelpers.c
    examples/common/alhelpers.h)
TARGET_COMPILE_DEFINITIONS(ex-common PUBLIC ${CPP_DEFS})
TARGET_INCLUDE_DIRECTORIES(ex-common PUBLIC ${OpenAL_SOURCE_DIR}/common)
TARGET_COMPILE_OPTIONS(ex-common PUBLIC ${C_FLAGS})
TARGET_LINK_LIBRARIES(ex-common PUBLIC OpenAL PRIVATE ${RT_LIB})

if(ALSOFT_EXAMPLES)
    add_executable(altonegen examples/altonegen.c)
    target_link_libraries(altonegen PRIVATE ${LINKER_FLAGS} ${MATH_LIB} ex-common)

    add_executable(alrecord examples/alrecord.c)
    target_link_libraries(alrecord PRIVATE ${LINKER_FLAGS} ex-common)

    if(ALSOFT_INSTALL_EXAMPLES)
        set(EXTRA_INSTALLS ${EXTRA_INSTALLS} altonegen alrecord)
    endif()

    message(STATUS "Building example programs")

    if(SNDFILE_FOUND)
        add_executable(alplay examples/alplay.c)
        target_link_libraries(alplay PRIVATE ${LINKER_FLAGS} SndFile::SndFile ex-common)

        add_executable(alstream examples/alstream.c)
        target_link_libraries(alstream PRIVATE ${LINKER_FLAGS} SndFile::SndFile ex-common)

        add_executable(alreverb examples/alreverb.c)
        target_link_libraries(alreverb PRIVATE ${LINKER_FLAGS} SndFile::SndFile ex-common)

        add_executable(almultireverb examples/almultireverb.c)
        target_link_libraries(almultireverb
            PRIVATE ${LINKER_FLAGS} SndFile::SndFile ex-common ${MATH_LIB})

        add_executable(allatency examples/allatency.c)
        target_link_libraries(allatency PRIVATE ${LINKER_FLAGS} SndFile::SndFile ex-common)

        add_executable(alhrtf examples/alhrtf.c)
        target_link_libraries(alhrtf
            PRIVATE ${LINKER_FLAGS} SndFile::SndFile ex-common ${MATH_LIB})

        add_executable(alstreamcb examples/alstreamcb.cpp)
        target_link_libraries(alstreamcb PRIVATE ${LINKER_FLAGS} SndFile::SndFile ex-common)

        if(ALSOFT_INSTALL_EXAMPLES)
            set(EXTRA_INSTALLS ${EXTRA_INSTALLS} alplay alstream alreverb almultireverb allatency
                alhrtf)
        endif()

        message(STATUS "Building SndFile example programs")
    endif()

    IF(SDL2_FOUND)
        ADD_EXECUTABLE(alloopback examples/alloopback.c)
        TARGET_INCLUDE_DIRECTORIES(alloopback PRIVATE ${SDL2_INCLUDE_DIR})
        TARGET_LINK_LIBRARIES(alloopback
            PRIVATE ${LINKER_FLAGS} ${SDL2_LIBRARY} ex-common ${MATH_LIB})

        if(ALSOFT_INSTALL_EXAMPLES)
            set(EXTRA_INSTALLS ${EXTRA_INSTALLS} alloopback)
        endif()

        MESSAGE(STATUS "Building SDL example programs")

        SET(FFVER_OK FALSE)
        IF(FFMPEG_FOUND)
            SET(FFVER_OK TRUE)
            IF(AVFORMAT_VERSION VERSION_LESS "57.56.101")
                MESSAGE(STATUS "libavformat is too old! (${AVFORMAT_VERSION}, wanted 57.56.101)")
                SET(FFVER_OK FALSE)
            ENDIF()
            IF(AVCODEC_VERSION VERSION_LESS "57.64.101")
                MESSAGE(STATUS "libavcodec is too old! (${AVCODEC_VERSION}, wanted 57.64.101)")
                SET(FFVER_OK FALSE)
            ENDIF()
            IF(AVUTIL_VERSION VERSION_LESS "55.34.101")
                MESSAGE(STATUS "libavutil is too old! (${AVUTIL_VERSION}, wanted 55.34.101)")
                SET(FFVER_OK FALSE)
            ENDIF()
            IF(SWSCALE_VERSION VERSION_LESS "4.2.100")
                MESSAGE(STATUS "libswscale is too old! (${SWSCALE_VERSION}, wanted 4.2.100)")
                SET(FFVER_OK FALSE)
            ENDIF()
            IF(SWRESAMPLE_VERSION VERSION_LESS "2.3.100")
                MESSAGE(STATUS "libswresample is too old! (${SWRESAMPLE_VERSION}, wanted 2.3.100)")
                SET(FFVER_OK FALSE)
            ENDIF()
        ENDIF()
        IF(FFVER_OK)
            ADD_EXECUTABLE(alffplay examples/alffplay.cpp)
            TARGET_INCLUDE_DIRECTORIES(alffplay
                PRIVATE ${SDL2_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
            TARGET_LINK_LIBRARIES(alffplay
                PRIVATE ${LINKER_FLAGS} ${SDL2_LIBRARY} ${FFMPEG_LIBRARIES} ex-common)

            if(ALSOFT_INSTALL_EXAMPLES)
                set(EXTRA_INSTALLS ${EXTRA_INSTALLS} alffplay)
            endif()
            message(STATUS "Building SDL+FFmpeg example programs")
        ENDIF()
    ENDIF()
    MESSAGE(STATUS "")
ENDIF()

if(EXTRA_INSTALLS)
    install(TARGETS ${EXTRA_INSTALLS}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
