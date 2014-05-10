# - Find fluidsynth
# Find the native fluidsynth includes and library
#
#  FLUIDSYNTH_INCLUDE_DIR - where to find fluidsynth.h
#  FLUIDSYNTH_LIBRARIES   - List of libraries when using fluidsynth.
#  FLUIDSYNTH_FOUND       - True if fluidsynth found.


FIND_PATH(FLUIDSYNTH_INCLUDE_DIR fluidsynth.h)

FIND_LIBRARY(FLUIDSYNTH_LIBRARIES NAMES fluidsynth )
MARK_AS_ADVANCED( FLUIDSYNTH_LIBRARIES FLUIDSYNTH_INCLUDE_DIR )

# handle the QUIETLY and REQUIRED arguments and set FLUIDSYNTH_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(FluidSynth
                                  REQUIRED_VARS FLUIDSYNTH_LIBRARIES FLUIDSYNTH_INCLUDE_DIR)

