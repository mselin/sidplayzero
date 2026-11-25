# SPDX-License-Identifier: GPL-3.0-or-later
#
# Downloading pinned files (TF-004). Everything lands in .cache/ under the source folder, which is not
# tracked, and a file whose SHA-256 differs from its pin is refused and removed.

# Where pinned downloads are kept. Not a cache variable, so that this file also works in script mode.
if(NOT DEFINED SPZ_CACHE_DIR)
    set(SPZ_CACHE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.cache")
    get_filename_component(SPZ_CACHE_DIR "${SPZ_CACHE_DIR}" ABSOLUTE)
endif()

# spz_pin(NAME URL SHA256): records a pin. Called from cmake/pins.cmake.
macro(spz_pin name url sha256)
    set(SPZ_PIN_${name}_URL "${url}")
    set(SPZ_PIN_${name}_SHA256 "${sha256}")
endmacro()

include("${CMAKE_CURRENT_LIST_DIR}/pins.cmake")

# spz_fetch(NAME OUT_VAR): makes sure the pinned file NAME is in the cache and sets OUT_VAR to its path.
function(spz_fetch name out_var)
    if(NOT DEFINED SPZ_PIN_${name}_URL)
        message(FATAL_ERROR "No pin named '${name}' in cmake/pins.cmake")
    endif()
    get_filename_component(_file "${SPZ_PIN_${name}_URL}" NAME)
    set(_path "${SPZ_CACHE_DIR}/downloads/${name}/${_file}")
    if(EXISTS "${_path}")
        file(SHA256 "${_path}" _have)
        if(NOT _have STREQUAL "${SPZ_PIN_${name}_SHA256}")
            file(REMOVE "${_path}")
        endif()
    endif()
    if(NOT EXISTS "${_path}")
        message(STATUS "Downloading ${name}: ${SPZ_PIN_${name}_URL}")
        file(DOWNLOAD "${SPZ_PIN_${name}_URL}" "${_path}" STATUS _status TLS_VERIFY ON)
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            file(REMOVE "${_path}")
            message(FATAL_ERROR "Could not download ${name}: ${_status}")
        endif()
        file(SHA256 "${_path}" _have)
        if(NOT _have STREQUAL "${SPZ_PIN_${name}_SHA256}")
            file(REMOVE "${_path}")
            message(FATAL_ERROR
                "Checksum mismatch for ${name}\n  expected ${SPZ_PIN_${name}_SHA256}\n  got      ${_have}")
        endif()
    endif()
    set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()

# spz_fetch_lvgl(OUT_VAR): a shallow clone of LVGL at its pinned tag, refused unless the tag is the
# pinned commit.
function(spz_fetch_lvgl out_var)
    set(_dir "${SPZ_CACHE_DIR}/lvgl-${SPZ_LVGL_TAG}")
    if(NOT EXISTS "${_dir}/lvgl.h")
        file(REMOVE_RECURSE "${_dir}")
        find_package(Git REQUIRED)
        message(STATUS "Fetching LVGL ${SPZ_LVGL_TAG}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -c advice.detachedHead=false clone --quiet --depth 1
                    --branch "${SPZ_LVGL_TAG}" "${SPZ_LVGL_REPOSITORY}" "${_dir}"
            RESULT_VARIABLE _result)
        if(NOT _result EQUAL 0)
            file(REMOVE_RECURSE "${_dir}")
            message(FATAL_ERROR "Could not fetch LVGL")
        endif()
    endif()
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_dir}" rev-parse HEAD
        OUTPUT_VARIABLE _commit OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _commit STREQUAL "${SPZ_LVGL_COMMIT}")
        message(FATAL_ERROR "LVGL in ${_dir} is at ${_commit}, not the pinned ${SPZ_LVGL_COMMIT}")
    endif()
    set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()
