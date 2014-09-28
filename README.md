openal-soft
===========

OpenAL Soft official fork(keep sync with http://repo.or.cz/w/openal-soft.git).

Ensure you have installed cmake(2.6 or higher), and make standalone android ndk toolchain($NDK/docs/STANDALONE-TOOLCHAIN.html), build steps:
$cd build
$cmake -D CMAKE_TOOLCHAIN_FILE=../cmake/android.toolchain.cmake -G "Unix Makefiles" ..
$make openal-info -j4

Enjoy it!!!
