# SPDX-License-Identifier: GPL-3.0-or-later
#
# `core`: everything the app does that is not drawing and not the outside world (SPEC-020). It uses the
# C++ library only. It has no include path to LVGL, the engine library, the sound library or libcurl,
# and gets none from what links it, because nothing it links has one (TF-003).
file(GLOB _core_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(spz_core STATIC ${_core_sources})
target_include_directories(spz_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(spz_core
    PUBLIC ${SPZ_PART_DEFINITIONS}
    PRIVATE "SPZ_VERSION=\"${SPZ_VERSION}\"")
target_compile_options(spz_core PRIVATE ${SPZ_WARNINGS})
