# - Find OpenSL ES includes and libraries
#
#   OPENSLES_FOUND        - True if OPENSLES_INCLUDE_DIR & OPENSLES_LIBRARY
#                            are found
#   OPENSLES_LIBRARIES    - Set when OPENSLES_LIBRARY is found
#   OPENSLES_INCLUDE_DIRS - Set when OPENSLES_INCLUDE_DIR is found
#
#   OPENSLES_INCLUDE_DIR - where to find OpenSLES.h, etc.
#   OPENSLES_LIBRARY     - the OpenSLES library
#

find_path(OPENSLES_INCLUDE_DIR
          NAMES OpenSLES.h
          PATH_SUFFIXES SLES
          DOC "The OpenSL ES include directory"
)

find_library(OPENSLES_LIBRARY
             NAME OpenSLES
             DOC "The OpenSL ES library"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenSLES
    REQUIRED_VARS OPENSLES_LIBRARY OPENSLES_INCLUDE_DIR
)

if(OPENSLES_FOUND)
    set(OPENSLES_LIBRARIES ${OPENSLES_LIBRARY})
    set(OPENSLES_INCLUDE_DIRS ${OPENSLES_INCLUDE_DIRS})
endif()

mark_as_advanced(OPENSLES_INCLUDE_DIR OPENSLES_LIBRARY)
