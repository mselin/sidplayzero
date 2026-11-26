# SPDX-License-Identifier: GPL-3.0-or-later
#
# SidPlayZero. How the build is laid out is docs/spec.md, SPEC-001 and SPEC-020; what it must provide
# for the tests is docs/test-framework-tasks.md, TF-001 to TF-005.
cmake_minimum_required(VERSION 3.31)

# The version lives in one place, the store's manifest (SPEC-223).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/app-builder.json" _spz_manifest)
string(JSON SPZ_VERSION GET "${_spz_manifest}" version)

project(sidplayzero VERSION "${SPZ_VERSION}" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(SIDPLAYZERO_BUILD_APP "Build the program (needs LVGL)" ON)
option(SIDPLAYZERO_BUILD_UNIT_TESTS "Build the unit tests: core and the fakes, nothing else" OFF)
option(SIDPLAYZERO_TEST_HOOKS "Build the program with the hooks the tests drive it by (SPEC-253)" OFF)
set(SIDPLAYZERO_DISPLAY "window" CACHE STRING "Where the program draws: window (desktop), framebuffer (device), or none")
set_property(CACHE SIDPLAYZERO_DISPLAY PROPERTY STRINGS window framebuffer none)
set(SIDPLAYZERO_SANITIZE "" CACHE STRING "Build with a sanitizer: address (with undefined) or thread")

include(cmake/fetch.cmake)
include(cmake/parts.cmake)

# No path of the machine that builds ends up in the program (SPEC-001, SPEC-225). The pinned downloads may
# lie elsewhere when .cache is a link: they are named from .cache too.
add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
get_filename_component(_spz_cache_real "${SPZ_CACHE_DIR}" REALPATH)
if(NOT _spz_cache_real STREQUAL "${SPZ_CACHE_DIR}")
    add_compile_options("-ffile-prefix-map=${_spz_cache_real}=.cache")
endif()

set(SPZ_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)

if(SIDPLAYZERO_SANITIZE STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
elseif(SIDPLAYZERO_SANITIZE STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
elseif(SIDPLAYZERO_SANITIZE STREQUAL "fuzzer")
    # Everything a fuzz target links is built for libFuzzer and with both sanitizers (SPEC-205).
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
elseif(NOT SIDPLAYZERO_SANITIZE STREQUAL "")
    message(FATAL_ERROR "SIDPLAYZERO_SANITIZE must be empty, 'address', 'thread' or 'fuzzer'")
endif()

add_subdirectory(src/core)

# The engine, built from its pinned archives with this build's compiler and flags (SPEC-002), for the
# program and for the fuzz target that gives it tunes (SPEC-205).
if(SIDPLAYZERO_BUILD_APP OR SIDPLAYZERO_BUILD_FUZZ)
    include(cmake/engine.cmake)
endif()

if(SIDPLAYZERO_BUILD_APP)
    add_subdirectory(src)
endif()

if(SIDPLAYZERO_BUILD_UNIT_TESTS)
    add_subdirectory(tests/unit)
endif()

option(SIDPLAYZERO_BUILD_SIM_TESTS "Build the C++ tests of the simulator level (SPEC-253)" OFF)
if(SIDPLAYZERO_BUILD_APP AND SIDPLAYZERO_BUILD_SIM_TESTS)
    add_subdirectory(tests/simulator)
endif()

option(SIDPLAYZERO_BUILD_FUZZ "Build the fuzz targets (SPEC-205); needs clang" OFF)
option(SIDPLAYZERO_FUZZ_CANARY "Also build the fuzz target with a deliberate fault (TF-060)" OFF)
if(SIDPLAYZERO_BUILD_FUZZ)
    add_subdirectory(tests/fuzz)
endif()

option(SIDPLAYZERO_PACKAGE "Add the rules that make the .deb for the device (SPEC-220)" OFF)
if(SIDPLAYZERO_PACKAGE)
    include(cmake/package.cmake)
endif()
