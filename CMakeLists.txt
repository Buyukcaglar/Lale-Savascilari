cmake_minimum_required(VERSION 3.24)
project(lale_savascilari C)

# Portable C11; the same tree builds on Windows (llvm-mingw) and macOS (Apple clang).
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SDL3 REQUIRED CONFIG)

# portable flags for every target below
set(LALE_WARN -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -fwrapv)

# ---- emulated PC: every platform source except the SDL3 host side ----------
add_library(lale_platform STATIC
    src/platform/pc.c
    src/platform/vga.c
    src/platform/cdrom.c
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
add_executable(lale
    src/platform/sdl3_main.c
    src/platform/audio_sdl.c
    src/platform/about.c
    src/platform/about_win32.c
)
# the built program is Lale64(.exe) (the user's decision of 2026-11-21); the
# CMake target keeps the name lale
set_target_properties(lale PROPERTIES OUTPUT_NAME Lale64)
if(WIN32)
    # program icon of the .exe (src/gen/lale.ico, tools/genicon.py)
    enable_language(RC)
    target_sources(lale PRIVATE src/platform/lale.rc)
    # the About box (about_win32.c): dark title bar, themed button, link, registry
    target_link_libraries(lale PRIVATE dwmapi uxtheme shell32 advapi32)
endif()
target_include_directories(lale PRIVATE src)
target_link_libraries(lale PRIVATE lale_game lale_platform SDL3::SDL3)
if(WIN32)
    # ship SDL3.dll next to the executable
    add_custom_command(TARGET lale POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:lale>)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lale PRIVATE ${LALE_WARN})
    target_compile_options(lale_platform PRIVATE ${LALE_WARN})
    target_compile_options(lale_game PRIVATE ${LALE_WARN})
endif()

# ---- tests ----------------------------------------------------------------------
enable_testing()
add_executable(test_vga tests/test_vga.c)
target_link_libraries(test_vga PRIVATE lale_platform)
add_test(NAME vga COMMAND test_vga)
add_executable(test_cdaudio tests/test_cdaudio.c)
target_link_libraries(test_cdaudio PRIVATE lale_game)
add_test(NAME cdaudio COMMAND test_cdaudio WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
    target_compile_options(lale_testlib PRIVATE ${LALE_WARN})
endif()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME differential
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/py/run.py
                     $<TARGET_FILE:lale_testlib>)
    set_tests_properties(differential PROPERTIES TIMEOUT 7200)   # whole-battle tests take minutes each
endif()
