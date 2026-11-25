# SPDX-License-Identifier: GPL-3.0-or-later
#
# C++ tests of the simulator level (SPEC-253, TF-046): for what a script cannot ask, such as whether a
# font has a glyph. They link `ui` and LVGL and draw into memory.
file(GLOB _sim_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_SOURCE_DIR}/tests/framework/sim/*.cpp")

add_executable(spz_sim_tests ${_sim_sources})
target_include_directories(spz_sim_tests PRIVATE "${CMAKE_SOURCE_DIR}/third_party/doctest")
target_link_libraries(spz_sim_tests PRIVATE spz_ui spz_platform)
target_compile_options(spz_sim_tests PRIVATE ${SPZ_WARNINGS})
