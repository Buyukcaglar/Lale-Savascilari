# Toolchain: llvm-mingw (clang, UCRT, x86_64) from .tools/ - see tools/bootstrap.sh
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
file(GLOB _LLVM_MINGW_DIR "${CMAKE_CURRENT_LIST_DIR}/../.tools/llvm-mingw/llvm-mingw-*-ucrt-x86_64")
set(CMAKE_C_COMPILER   "${_LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang.exe")
set(CMAKE_CXX_COMPILER "${_LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang++.exe")
set(CMAKE_RC_COMPILER  "${_LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-windres.exe")
file(GLOB _SDL3_DIR "${CMAKE_CURRENT_LIST_DIR}/../.tools/sdl3/SDL3-*/x86_64-w64-mingw32")
list(APPEND CMAKE_PREFIX_PATH "${_SDL3_DIR}")
