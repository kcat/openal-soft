# - Find Windows Multi Media Extensions (WinMM) includes and libraries
#
#   WINMM_FOUND        - True if WINMM_INCLUDE_DIR & WINMM_LIBRARY are found
#   WINMM_LIBRARIES    - Set when WINMM_LIBRARY is found
#   WINMM_INCLUDE_DIRS - Set when WINMM_INCLUDE_DIR is found
#
#   WINMM_INCLUDE_DIR - where to find mmsystem.h, etc.
#   WINMM_LIBRARY     - the winmm library
#
if (WIN32)
  include(FindWindowsSDK)
  if (WINDOWSSDK_FOUND)
    get_windowssdk_library_dirs(${WINDOWSSDK_PREFERRED_DIR} WINSDK_LIB_DIRS)
    get_windowssdk_include_dirs(${WINDOWSSDK_PREFERRED_DIR} WINSDK_INCLUDE_DIRS)
  endif()
endif()

# WINMM_INCLUDE_DIR
find_path(WINMM_INCLUDE_DIR
          NAMES "mmsystem.h"
          PATHS ${WINSDK_INCLUDE_DIRS}
          DOC "The Windows Multi Media Extensions include directory")

# WINMM_LIBRARY
find_library(WINMM_LIBRARY
             NAMES winmm
             PATHS ${WINSDK_LIB_DIRS}
             PATH_SUFFIXES lib lib/x86 lib/x64
             DOC "The Windows Multi Media Extensions library")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WinMM REQUIRED_VARS WINMM_LIBRARY WINMM_INCLUDE_DIR)

if(WINMM_FOUND)
    set(WINMM_LIBRARIES ${WINMM_LIBRARY})
    set(WINMM_INCLUDE_DIRS ${WINMM_INCLUDE_DIR})
endif()

mark_as_advanced(WINMM_INCLUDE_DIR WINMM_LIBRARY)
