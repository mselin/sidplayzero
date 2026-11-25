# SPDX-License-Identifier: GPL-3.0-or-later
#
# The parts that may be left out of a build (SPEC-115, TF-013).
#
# SIDPLAYZERO_PARTS is "all" (every part but usbsid), "must" (none), or a list of names separated by
# semicolons or commas. For each part the build defines SPZ_PART_<NAME> as 1 or 0, with "-" in a name
# written as "_": SPZ_PART_TOO_SLOW, SPZ_PART_DOWNLOAD_ALL. tools/check_repo.py holds the same list.

set(SPZ_ALL_PARTS
    detail mini modes shuffle restart skip voices bars notes rate force roms too-slow brightness
    screenshot playlists import-more export filters resume screen-off releases download-all usbsid)

set(SIDPLAYZERO_PARTS "all" CACHE STRING "Parts to build: all, must, or a list of part names")

string(REPLACE "," ";" _spz_asked "${SIDPLAYZERO_PARTS}")
if(_spz_asked STREQUAL "all")
    set(SPZ_PARTS ${SPZ_ALL_PARTS})
    list(REMOVE_ITEM SPZ_PARTS usbsid)
elseif(_spz_asked STREQUAL "must")
    set(SPZ_PARTS "")
else()
    set(SPZ_PARTS ${_spz_asked})
    foreach(_part IN LISTS SPZ_PARTS)
        if(NOT _part IN_LIST SPZ_ALL_PARTS)
            message(FATAL_ERROR "SIDPLAYZERO_PARTS names '${_part}', which is not a part of SPEC-115")
        endif()
    endforeach()
endif()

set(SPZ_PART_DEFINITIONS "")
foreach(_part IN LISTS SPZ_ALL_PARTS)
    string(TOUPPER "${_part}" _upper)
    string(REPLACE "-" "_" _upper "${_upper}")
    if(_part IN_LIST SPZ_PARTS)
        list(APPEND SPZ_PART_DEFINITIONS "SPZ_PART_${_upper}=1")
    else()
        list(APPEND SPZ_PART_DEFINITIONS "SPZ_PART_${_upper}=0")
    endif()
endforeach()
list(JOIN SPZ_PARTS " " SPZ_PARTS_TEXT)
message(STATUS "Parts built: ${SPZ_PARTS_TEXT}")
