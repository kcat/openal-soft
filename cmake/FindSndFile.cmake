# - Try to find SndFile
# Once done this will define
#
#  SNDFILE_FOUND - system has SndFile
#  SNDFILE_INCLUDE_DIRS - the SndFile include directory
#  SNDFILE_LIBRARIES - Link these to use SndFile

find_path(SNDFILE_INCLUDE_DIR NAMES sndfile.h)

find_library(SNDFILE_LIBRARY NAMES sndfile sndfile-1)

# handle the QUIETLY and REQUIRED arguments and set SNDFILE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SndFile DEFAULT_MSG SNDFILE_LIBRARY SNDFILE_INCLUDE_DIR)

if(SNDFILE_FOUND)
    set(SNDFILE_INCLUDE_DIRS ${SNDFILE_INCLUDE_DIR})
    set(SNDFILE_LIBRARIES ${SNDFILE_LIBRARY})
endif()

# show the SNDFILE_INCLUDE_DIR and SNDFILE_LIBRARY variables only in the advanced view
mark_as_advanced(SNDFILE_INCLUDE_DIR SNDFILE_LIBRARY)
