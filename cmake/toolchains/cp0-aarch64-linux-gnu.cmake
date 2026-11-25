# SPDX-License-Identifier: GPL-3.0-or-later
#
# The toolchain for the CardputerZero (SPEC-001): the aarch64-linux-gnu compiler against M5Stack's sysroot,
# release 0.0.4. The sysroot is a pinned download (cmake/pins.cmake): it is fetched into .cache/ and checked
# by SHA-256 the first time, which takes a few minutes, and unpacked beside it.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

include("${CMAKE_CURRENT_LIST_DIR}/../fetch.cmake")

set(_spz_sysroot "${SPZ_CACHE_DIR}/sysroot-${SPZ_PIN_sysroot_SHA256}")
if(NOT EXISTS "${_spz_sysroot}/usr/include")
    spz_fetch(sysroot _spz_sysroot_archive)
    message(STATUS "Unpacking the device's sysroot (once)")
    file(REMOVE_RECURSE "${_spz_sysroot}.partial")
    file(MAKE_DIRECTORY "${_spz_sysroot}.partial")
    file(ARCHIVE_EXTRACT INPUT "${_spz_sysroot_archive}" DESTINATION "${_spz_sysroot}.partial")
    # The archive holds the sysroot either directly or in one folder.
    if(EXISTS "${_spz_sysroot}.partial/usr/include")
        file(RENAME "${_spz_sysroot}.partial" "${_spz_sysroot}")
    else()
        file(GLOB _spz_inner LIST_DIRECTORIES true "${_spz_sysroot}.partial/*")
        foreach(_spz_folder IN LISTS _spz_inner)
            if(EXISTS "${_spz_folder}/usr/include")
                file(RENAME "${_spz_folder}" "${_spz_sysroot}")
            endif()
        endforeach()
        file(REMOVE_RECURSE "${_spz_sysroot}.partial")
    endif()
    if(NOT EXISTS "${_spz_sysroot}/usr/include")
        message(FATAL_ERROR "The device's sysroot did not unpack to a folder with usr/include")
    endif()
endif()

set(CMAKE_SYSROOT "${_spz_sysroot}")
find_program(_spz_cc NAMES aarch64-linux-gnu-gcc REQUIRED)
find_program(_spz_cxx NAMES aarch64-linux-gnu-g++ REQUIRED)
set(CMAKE_C_COMPILER "${_spz_cc}")
set(CMAKE_CXX_COMPILER "${_spz_cxx}")
set(CMAKE_FIND_ROOT_PATH "${_spz_sysroot}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
