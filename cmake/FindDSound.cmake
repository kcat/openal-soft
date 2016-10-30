# - Find DirectSound includes and libraries
#
#   DSOUND_FOUND        - True if DSOUND_INCLUDE_DIR & DSOUND_LIBRARY are found
#   DSOUND_LIBRARIES    - Set when DSOUND_LIBRARY is found
#   DSOUND_INCLUDE_DIRS - Set when DSOUND_INCLUDE_DIR is found
#
#   DSOUND_INCLUDE_DIR - where to find dsound.h, etc.
#   DSOUND_LIBRARY     - the dsound library
#

# DSOUND_INCLUDE_DIR
file(GLOB DXSDK_INCLUDE_DIRS LIST_DIRECTORIES TRUE "C:/Program Files (x86)/Windows Kits/10/Include/*")
if (DXSDK_INCLUDE_DIRS)
  list(SORT DXSDK_INCLUDE_DIRS)
  list(REVERSE DXSDK_INCLUDE_DIRS)
endif() 

find_path(DSOUND_INCLUDE_DIR
          NAMES 
            "dsound.h" 
          PATHS 
            "${DXSDK_DIR}"
            ${DXSDK_INCLUDE_DIRS}
            "C:/Program Files (x86)/Windows Kits/8.0"
            "C:/Program Files (x86)/Windows Kits/8.1"
          PATH_SUFFIXES 
            Include
            um 
            Include/um
          DOC 
            "The DirectSound include directory"
)

# DSOUND_LIBRARY
if(CMAKE_CL_64)
    set (DirectX_ARCHITECTURE x64)
else()
    set (DirectX_ARCHITECTURE x86)
endif()

file(GLOB DXSDK_LIB_DIRS LIST_DIRECTORIES TRUE "C:/Program Files (x86)/Windows Kits/10/Lib/*")
if (DXSDK_LIB_DIRS)
  list(SORT DXSDK_LIB_DIRS)
  list(REVERSE DXSDK_LIB_DIRS)
endif() 

find_library(DSOUND_LIBRARY
             NAMES dsound
             DOC "The DirectSound library")
if (NOT DSOUND_LIBRARY)
  find_library(DSOUND_LIBRARY
               NAMES dsound
               PATHS 
                "${DXSDK_DIR}"
                ${DXSDK_LIB_DIRS}
                "C:/Program Files (x86)/Windows Kits/8.0"
                "C:/Program Files (x86)/Windows Kits/8.1"               
               PATH_SUFFIXES 
                Lib 
                Lib/${DirectX_ARCHITECTURE}
                um/${DirectX_ARCHITECTURE}
                Lib/win8/um/${DirectX_ARCHITECTURE}
                Lib/winv6.3/um/${DirectX_ARCHITECTURE}
               DOC 
                "The DirectSound library"
  )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DSound
    REQUIRED_VARS DSOUND_LIBRARY DSOUND_INCLUDE_DIR
)

if(DSOUND_FOUND)
    set(DSOUND_LIBRARIES ${DSOUND_LIBRARY})
    set(DSOUND_INCLUDE_DIRS ${DSOUND_INCLUDE_DIR})
endif()

mark_as_advanced(DSOUND_INCLUDE_DIR DSOUND_LIBRARY)
