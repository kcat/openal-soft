SET(CMAKE_SYSTEM_NAME Linux)  # Tell CMake we're cross-compiling
include(CMakeForceCompiler)
# Prefix detection only works with compiler id "GNU"
# CMake will look for prefixed g++, cpp, ld, etc. automatically
CMAKE_FORCE_C_COMPILER(arm-linux-androideabi-gcc GNU)
SET(ANDROID TRUE)
SET(HAVE_STRCASECMP TRUE)
SET(HAVE_STRNCASECMP TRUE)
SET(HAVE_GETTIMEOFDAY TRUE)
SET(HAVE_SNPRINTF TRUE)
SET(HAVE_NANOSLEEP TRUE)
SET(HAVE_PTHREAD_H TRUE)
SET(SIZEOF_LONG 4)
SET(SIZEOF_LONG_LONG 8)
SET(LIBTYPE STATIC)