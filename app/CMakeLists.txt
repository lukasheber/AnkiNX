cmake_minimum_required(VERSION 3.13)

# AnkiNX: native Nintendo Switch frontend (borealis + litehtml) over Anki's Rust
# backend (switch/dist/libanki_switch_core.a, built by switch/build_core.sh).
# Build with app/build.sh.

set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(AnkiNX)
# One version for the whole project: ../VERSION (see CHANGELOG.md).
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION ANKINX_VERSION LIMIT_COUNT 1)
string(REPLACE "." ";" ANKINX_VERSION_PARTS ${ANKINX_VERSION})
list(GET ANKINX_VERSION_PARTS 0 VERSION_MAJOR)
list(GET ANKINX_VERSION_PARTS 1 VERSION_MINOR)
list(GET ANKINX_VERSION_PARTS 2 VERSION_ALTER)
add_definitions(-DANKINX_VERSION="${ANKINX_VERSION}")
set(PROJECT_AUTHOR "Lucas H")
set(PROJECT_TITLEID "0100414E4B490000")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif ()

# borealis
add_subdirectory(${BOREALIS_LIBRARY} borealis)

# litehtml (card HTML/CSS layout)
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LITEHTML_ENABLE_LINT OFF CACHE BOOL "" FORCE)
add_subdirectory(library/litehtml EXCLUDE_FROM_ALL)

# Anki's Rust backend: switch/dist (Switch) or switch/dist-host (desktop preview)
set(ANKI_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../switch/dist CACHE PATH "libanki_switch_core.a folder")
add_library(anki_switch_core STATIC IMPORTED)
set_target_properties(anki_switch_core PROPERTIES
    IMPORTED_LOCATION ${ANKI_CORE_DIR}/libanki_switch_core.a
    INTERFACE_INCLUDE_DIRECTORIES ${ANKI_CORE_DIR})

find_package(Threads REQUIRED)
# Card audio via SDL2_mixer. The desktop preview can build without it.
option(ANKINX_AUDIO "Play card audio with SDL2_mixer" ON)
find_package(PkgConfig REQUIRED)
if (ANKINX_AUDIO)
    # Static link: use the full dependency lists (*_STATIC_*).
    # SDL2_mixer for MP3/OGG/Opus/WAV, FFmpeg for the rest (M4A/AAC, FLAC, ...).
    pkg_check_modules(AUDIO REQUIRED SDL2_mixer libavformat libavcodec libswresample libavutil)
endif ()
# Card images: WebP (PNG/JPEG/GIF/BMP use NanoVG's stb_image).
pkg_check_modules(WEBP REQUIRED libwebp)

# SVG card images (lunasvg + plutovg)
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LUNASVG_DISABLE_LOAD_SYSTEM_FONTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(library/lunasvg EXCLUDE_FROM_ALL)

# QuickJS-ng (JavaScript in card templates, src/script): the engine only, no
# libc module (scripts get a DOM from us, not files or processes).
set(QJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/quickjs)
add_library(quickjs STATIC ${QJS_DIR}/quickjs.c ${QJS_DIR}/libregexp.c ${QJS_DIR}/libunicode.c ${QJS_DIR}/dtoa.c)
set_target_properties(quickjs PROPERTIES C_STANDARD 11)
target_include_directories(quickjs PUBLIC ${QJS_DIR})
# NDEBUG: QuickJS asserts on objects still referenced at teardown; a card
# script must never be able to abort the app.
target_compile_definitions(quickjs PRIVATE _GNU_SOURCE NDEBUG)
target_compile_options(quickjs PRIVATE -w)

file(GLOB_RECURSE MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
# Exactly one audio backend: SDL2_mixer, or the silent stub.
if (ANKINX_AUDIO)
    list(REMOVE_ITEM MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/util/audio_none.cpp)
else ()
    list(REMOVE_ITEM MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/util/audio.cpp)
endif ()
if (PLATFORM_SWITCH)
    # libnx service init/exit hooks (sockets, romfs, fonts) from borealis
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
endif ()

program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE include
    ${AUDIO_STATIC_INCLUDE_DIRS} ${WEBP_STATIC_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections -Wall)
target_link_directories(${PROJECT_NAME} PRIVATE ${AUDIO_STATIC_LIBRARY_DIRS} ${WEBP_STATIC_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis litehtml gumbo quickjs lunasvg::lunasvg anki_switch_core
    ${AUDIO_STATIC_LIBRARIES} ${WEBP_STATIC_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} m)
if (PLATFORM_DESKTOP)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl) # Rust std on Linux

    # Host test for card scripts: app/test_scripts.sh
    add_executable(script_test tests/script_test.cpp src/script/dom.cpp src/script/card_script.cpp)
    set_target_properties(script_test PROPERTIES CXX_STANDARD 17)
    target_include_directories(script_test PRIVATE include)
    target_link_libraries(script_test PRIVATE gumbo quickjs m)
    add_executable(rtl_test tests/rtl_test.cpp src/view/rtl_text.cpp)
    set_target_properties(rtl_test PROPERTIES CXX_STANDARD 17)
    target_include_directories(rtl_test PRIVATE include)
endif ()

if (PLATFORM_SWITCH)
    add_custom_target(${PROJECT_NAME}.nro ALL DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "${PROJECT_NAME}" "${PROJECT_AUTHOR}"
            "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp --titleid=${PROJECT_TITLEID}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        # Fonts are for the desktop preview; the Switch uses its shared system fonts.
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/resources/font
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON}
            --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
        VERBATIM
    )
endif ()
