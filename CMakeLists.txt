cmake_minimum_required(VERSION 3.24)
project(lale_savascilari C)
if(APPLE)
    enable_language(OBJC)          # src/platform/about_macos.m (the About box)
endif()
set(LALE_VERSION 1.0)              # keep in step with LALE_VERSION in src/platform/about.c

# Portable C11; the same tree builds on Windows (llvm-mingw) and macOS (Apple clang).
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# SDL3: an installed one (Windows: .tools/sdl3, macOS: Homebrew), or - for a
# program that runs on other machines (preset macos-release) - the same
# release built from source as a static library, so that nothing outside the
# application is needed and the deployment target is ours, not Homebrew's.
option(LALE_VENDOR_SDL "Build SDL3 from source as a static library" OFF)
if(LALE_VENDOR_SDL)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(SDL3)
else()
    find_package(SDL3 REQUIRED CONFIG)
endif()

# portable flags for every target below
# -ffp-contract=off: no fused multiply-add (clang's default on arm64), so
# host floating point rounds the same on every target.
set(LALE_WARN -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -fwrapv -ffp-contract=off)
if(APPLE)
    # Apple clang still makes zero-initialised globals "common" symbols and
    # gives the big ones (dg, pc_a000, the far-heap arena) an alignment the
    # linker then complains about; -fno-common is every other clang's default
    list(APPEND LALE_WARN -fno-common)
    # lale_game and lale_platform depend on each other (see below): CMake
    # repeats them on the link line, which Apple's linker remarks on
    add_link_options(LINKER:-no_warn_duplicate_libraries)
endif()

# ---- emulated PC: every platform source except the SDL3 host side ----------
add_library(lale_platform STATIC
    src/platform/pc.c
    src/platform/vga.c
    src/platform/cdrom.c
    src/platform/iso9660.c
    src/platform/zdeflate.c
    src/platform/flac.c
    src/platform/cdsector.c
    src/platform/chd.c
    src/platform/zipfile.c
    src/platform/drive.c
    src/platform/install.c
    src/platform/options.c
    src/platform/audio.c
    src/platform/speaker.c
)
target_include_directories(lale_platform PUBLIC src)

# decompiled game/engine code + Borland-compatible RTL pieces + generated
# data.  Globbed so that modules can be added without touching this file
# (re-run cmake after adding).  Test stand-ins (tests/stubs) never go here.
file(GLOB LALE_GAME_SRC CONFIGURE_DEPENDS
     src/game/*.c src/engine/*.c src/rtl/*.c src/dsmi/*.c src/gen/*.c)
add_library(lale_game STATIC ${LALE_GAME_SRC})
target_include_directories(lale_game PUBLIC src)
# platform/audio.c drives the DSMI mixer (dsmi/mcp.c): the two static
# libraries depend on each other (CMake repeats them on the link line)
target_link_libraries(lale_game PUBLIC lale_platform)
target_link_libraries(lale_platform PUBLIC lale_game)

# ---- the game: SDL3 host + everything else ------------------------------------
set(LALE_HOST_SRC
    src/platform/sdl3_main.c
    src/platform/setup.c
    src/platform/hdvideo.c
    src/platform/hdvideo_mf.c
    src/platform/audio_sdl.c
    src/platform/about.c
    src/platform/about_win32.c
    src/platform/ui.c
    src/platform/ui_win32.c
)
if(APPLE)
    list(APPEND LALE_HOST_SRC src/platform/about_macos.m src/platform/hdvideo_avf.m src/platform/ui_macos.m)
    set_source_files_properties(src/platform/about_macos.m src/platform/hdvideo_avf.m src/platform/ui_macos.m
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif()
add_executable(lale ${LALE_HOST_SRC})
# the built program is Lale64(.exe) (the user's decision of 2026-11-21); the
# CMake target keeps the name lale
set_target_properties(lale PROPERTIES OUTPUT_NAME Lale64)
if(WIN32)
    # program icon of the .exe (src/gen/lale.ico, tools/genicon.py)
    enable_language(RC)
    target_sources(lale PRIVATE src/platform/lale.rc)
    # the About box (about_win32.c): dark title bar, themed button, link, registry
    target_link_libraries(lale PRIVATE dwmapi uxtheme shell32 advapi32)
    # the 4K videos (hdvideo_mf.c): Media Foundation
    target_link_libraries(lale PRIVATE mfreadwrite mfplat mfuuid ole32)
    # ... loaded at first use, so that the program starts without them too
    # (Windows N editions; hdreader_available() in hdvideo_mf.c)
    target_link_options(lale PRIVATE -Wl,--delayload=mfplat.dll -Wl,--delayload=mfreadwrite.dll)
endif()
target_include_directories(lale PRIVATE src)
target_link_libraries(lale PRIVATE lale_game lale_platform SDL3::SDL3)
if(WIN32)
    # ship SDL3.dll next to the executable
    add_custom_command(TARGET lale POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:lale>)
endif()

if(APPLE)
    # the About box; the 4K videos (hdvideo_avf.m)
    set(LALE_MAC_FRAMEWORKS "-framework Cocoa" "-framework AVFoundation" "-framework CoreMedia"
                            "-framework CoreVideo")
    target_link_libraries(lale PRIVATE ${LALE_MAC_FRAMEWORKS})

    # ---- Lale64.app: the same program as a double-clickable application ------
    # (the bare out/macos/Lale64 stays, for the terminal, tests and scripts).
    # Icon: src/gen/lale.icns (tools/genicon.py).  With the Homebrew SDL3 the
    # app runs on this machine only; tools/package_macos.sh builds the one to
    # give away (preset macos-release: static SDL3, arm64 + x86_64).
    set(LALE_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/src/gen/lale.icns)
    add_executable(lale_app MACOSX_BUNDLE ${LALE_HOST_SRC} ${LALE_ICNS})
    set_source_files_properties(${LALE_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(LALE_MACOS_MIN ${CMAKE_OSX_DEPLOYMENT_TARGET})
    else()
        set(LALE_MACOS_MIN 11.0)
    endif()
    set_target_properties(lale_app PROPERTIES
        OUTPUT_NAME Lale64
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in)
    target_include_directories(lale_app PRIVATE src)
    target_link_libraries(lale_app PRIVATE lale_game lale_platform SDL3::SDL3 ${LALE_MAC_FRAMEWORKS})
    # ad-hoc signature over the finished bundle (Apple Silicon runs nothing unsigned)
    add_custom_command(TARGET lale_app POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:lale_app>
        VERBATIM)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lale PRIVATE ${LALE_WARN})
    if(APPLE)
        target_compile_options(lale_app PRIVATE ${LALE_WARN})
    endif()
    target_compile_options(lale_platform PRIVATE ${LALE_WARN})
    target_compile_options(lale_game PRIVATE ${LALE_WARN})
endif()

# ---- chdtool: the disc code on the command line (tools/chdtool.c) ----------------
add_executable(chdtool tools/chdtool.c)
target_include_directories(chdtool PRIVATE src)
target_link_libraries(chdtool PRIVATE lale_platform)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chdtool PRIVATE ${LALE_WARN})
endif()

# ---- tests ----------------------------------------------------------------------
enable_testing()
add_executable(test_vga tests/test_vga.c)
target_link_libraries(test_vga PRIVATE lale_platform)
add_test(NAME vga COMMAND test_vga)
add_executable(test_cdaudio tests/test_cdaudio.c)
target_link_libraries(test_cdaudio PRIVATE lale_game)
# The tests that write files (disc images made up by the test) work in a
# folder of their own: the build folder is also where the program looks for
# the player's game (docs/INSTALL.md), and it skips folders named with a dot.
set(LALE_TEST_TMP ${CMAKE_CURRENT_BINARY_DIR}/.test_tmp)
file(MAKE_DIRECTORY ${LALE_TEST_TMP})
add_test(NAME cdaudio COMMAND test_cdaudio WORKING_DIRECTORY ${LALE_TEST_TMP})
# The port's own codecs (deflate, FLAC, CD sectors, CHD, ISO 9660, ZIP) and the
# importer, on data made up by the test.
add_executable(test_codecs tests/test_codecs.c)
target_link_libraries(test_codecs PRIVATE lale_platform)
add_test(NAME codecs COMMAND test_codecs WORKING_DIRECTORY ${LALE_TEST_TMP})

# Shared library for the Python differential tests (tests/py): the original
# runs in tools/emu86.py, the C through ctypes.  Exactly the sources of
# tests/base_sources.txt - the list tools/testlib.sh builds private test
# DLLs from (it includes the test-only recording devices in tests/stubs).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tests/base_sources.txt)
file(STRINGS tests/base_sources.txt _lale_base_lines)
set(LALE_TESTLIB_SRC "")
foreach(_line IN LISTS _lale_base_lines)
    string(STRIP "${_line}" _line)
    if(_line STREQUAL "" OR _line MATCHES "^#")
        continue()
    endif()
    list(APPEND LALE_TESTLIB_SRC ${_line})
endforeach()
add_library(lale_testlib SHARED ${LALE_TESTLIB_SRC})
target_include_directories(lale_testlib PRIVATE src)
set_target_properties(lale_testlib PROPERTIES PREFIX "")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    # 16-byte function alignment: tests/py/lale.py patches a jump (up to 16
    # bytes) over the entry of functions it redirects
    target_compile_options(lale_testlib PRIVATE ${LALE_WARN} -falign-functions=16)
endif()
# the tests' Python needs unicorn/capstone/numpy: the project's virtual
# environment when there is one (macOS, where the system Python takes no
# packages - see tools/bootstrap.sh), else the Python on the PATH
if(NOT Python3_EXECUTABLE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.tools/venv/bin/python)
    set(Python3_EXECUTABLE ${CMAKE_CURRENT_SOURCE_DIR}/.tools/venv/bin/python)
endif()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME differential
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/py/run.py
                     $<TARGET_FILE:lale_testlib>)
    set_tests_properties(differential PROPERTIES TIMEOUT 7200)   # whole-battle tests take minutes each
endif()
