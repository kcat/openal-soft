# - Find Windows Audio Session API (WASAPI) includes and libraries
#
#   WASAPI_FOUND        - True if WASAPI_INCLUDE_DIR & WASAPI_LIBRARY are found
#   WASAPI_INCLUDE_DIRS - Set when WASAPI_INCLUDE_DIR is found
#
#   WASAPI_INCLUDE_DIR - where to find mmdeviceapi.h, etc.
#
if (WIN32)
  include(FindWindowsSDK)
  if (WINDOWSSDK_FOUND)
    get_windowssdk_library_dirs(${WINDOWSSDK_PREFERRED_DIR} WINSDK_LIB_DIRS)
    get_windowssdk_include_dirs(${WINDOWSSDK_PREFERRED_DIR} WINSDK_INCLUDE_DIRS)
  endif()
endif()

# WASAPI_INCLUDE_DIR
find_path(WASAPI_INCLUDE_DIR
          NAMES "mmdeviceapi.h"
          PATHS ${WINSDK_INCLUDE_DIRS}
          DOC "The Windows Core Audio Multi Media Device API include directory")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MMDevAPI REQUIRED_VARS WASAPI_INCLUDE_DIR)

if(WASAPI_FOUND)
    set(WASAPI_INCLUDE_DIRS ${WASAPI_INCLUDE_DIR})
endif()

mark_as_advanced(WASAPI_INCLUDE_DIR)
