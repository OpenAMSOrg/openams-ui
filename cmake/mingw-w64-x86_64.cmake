# CMake toolchain file for cross-compiling the SDL simulator to 64-bit
# Windows with MinGW-w64 (Debian/Ubuntu package names: gcc-mingw-w64-x86-64,
# g++-mingw-w64-x86-64 — the "posix" thread-model alternative).
#
# Usage:
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
#         -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH=/path/to/SDL2-mingw-devel/x86_64-w64-mingw32
#   cmake --build build-win -j3

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TARGET x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${MINGW_TARGET}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_TARGET}-g++)
set(CMAKE_RC_COMPILER  ${MINGW_TARGET}-windres)

# Root the search paths at the MinGW sysroot so find_package()/find_library()
# resolve Windows libraries (e.g. the SDL2 devel package under
# CMAKE_PREFIX_PATH) instead of the host's own /usr.
set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TARGET})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# BOTH (not ONLY): the SDL2 devel package normally lives outside the MinGW
# sysroot (e.g. third_party/SDL2-2.30.9/x86_64-w64-mingw32, passed via
# -DCMAKE_PREFIX_PATH), and ONLY would silently re-root that absolute path
# under CMAKE_FIND_ROOT_PATH and never find it, turning a missing-SDL2 error
# into a confusing one that names the right file but the wrong reason.
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# The devel SDL2 package ships a CMake package config (SDL2Config.cmake);
# find_package(SDL2 CONFIG) is what CMakeLists.txt uses when WIN32 is set, so
# it needs no pkg-config on the host. Point CMAKE_PREFIX_PATH (or SDL2_DIR
# directly) at the package's root — e.g.
#   -DCMAKE_PREFIX_PATH=/path/to/SDL2-2.30.9/x86_64-w64-mingw32
