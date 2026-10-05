# Cross-compile for 64-bit Windows from Linux with MinGW-w64 (POSIX threads
# variant, needed for std::thread/std::mutex).
#   sudo apt-get install mingw-w64 cmake ninja-build
#   cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
#   cmake --build build-win
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc-posix)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++-posix)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
