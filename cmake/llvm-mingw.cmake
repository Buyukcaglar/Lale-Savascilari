# Toolchain: llvm-mingw (clang, UCRT, x86_64) from .tools/ - see tools/bootstrap.sh
# On Windows the native package (llvm-mingw-*-ucrt-x86_64); on macOS / Linux
# the package for that host (llvm-mingw-*-ucrt-macos-universal, ...) cross-
# compiles the same Windows program - used to check from the Mac that shared
# code still builds for Windows (preset windows-cross).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if(CMAKE_HOST_WIN32)
    file(GLOB _LLVM_MINGW_DIR "${CMAKE_CURRENT_LIST_DIR}/../.tools/llvm-mingw/llvm-mingw-*-ucrt-x86_64")
    set(_EXE ".exe")
else()
    file(GLOB _LLVM_MINGW_DIR "${CMAKE_CURRENT_LIST_DIR}/../.tools/llvm-mingw/llvm-mingw-*-ucrt-*")
    list(FILTER _LLVM_MINGW_DIR EXCLUDE REGEX "ucrt-x86_64$")      # the Windows-hosted package
    set(_EXE "")
    # look for packages (SDL3) only in the target's prefix, not on the host
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
endif()
set(CMAKE_C_COMPILER   "${_LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang${_EXE}")
set(CMAKE_CXX_COMPILER "${_LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang++${_EXE}")
set(CMAKE_RC_COMPILER  "${_LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-windres${_EXE}")
file(GLOB _SDL3_DIR "${CMAKE_CURRENT_LIST_DIR}/../.tools/sdl3/SDL3-*/x86_64-w64-mingw32")
list(APPEND CMAKE_PREFIX_PATH "${_SDL3_DIR}")
