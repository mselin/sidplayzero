# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makes the app's fonts again (SPEC-010): downloads DejaVu's pinned release archive, checks it, and runs
# tools/make_fonts.py on it. Run in script mode from anywhere:
#
#     cmake -P cmake/fonts.cmake                        # writes src/ui/fonts/dejavu_sans_{12,14}.c
#     cmake -DSPZ_FONTS_OUT=/some/folder -P cmake/fonts.cmake
#
# The sources it writes are kept in the repository, so a build needs neither the archive nor Python for
# them; tests/simulator/test_fonts_source.py makes them again into a scratch folder and compares.

cmake_minimum_required(VERSION 3.31)
include("${CMAKE_CURRENT_LIST_DIR}/fetch.cmake")

get_filename_component(_spz_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED SPZ_FONTS_OUT)
    set(SPZ_FONTS_OUT "${_spz_root}/src/ui/fonts")
endif()

spz_fetch(dejavu_fonts _spz_dejavu_archive)
find_program(SPZ_PYTHON3 NAMES python3 REQUIRED)
execute_process(
    COMMAND "${SPZ_PYTHON3}" -I "${_spz_root}/tools/make_fonts.py" "${_spz_dejavu_archive}" "${SPZ_FONTS_OUT}"
    RESULT_VARIABLE _spz_result)
if(NOT _spz_result EQUAL 0)
    message(FATAL_ERROR "tools/make_fonts.py failed")
endif()
message(STATUS "Fonts written to ${SPZ_FONTS_OUT}")
