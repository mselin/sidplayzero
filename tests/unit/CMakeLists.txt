# SPDX-License-Identifier: GPL-3.0-or-later
#
# The unit tests (SPEC-251, TF-020): doctest, `core` and the fakes, and nothing else. The framework's own
# C++ tests (tests/framework/*.cpp) are built into the same program; their names start with "TF-".
file(GLOB _unit_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_SOURCE_DIR}/tests/framework/*.cpp")

add_executable(spz_unit_tests ${_unit_sources})
target_include_directories(spz_unit_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/doctest"
    "${CMAKE_SOURCE_DIR}/tests/fakes")
target_link_libraries(spz_unit_tests PRIVATE spz_core)
# Where the fixtures are (tools/make_fixtures.py writes them before the tests run).
target_compile_definitions(spz_unit_tests PRIVATE "SPZ_FIXTURES=\"${CMAKE_SOURCE_DIR}/tests/fixtures\"")
target_include_directories(spz_unit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(spz_unit_tests PRIVATE ${SPZ_WARNINGS})

# TF-020: the link line is written out, for the test that reads it.
set_target_properties(spz_unit_tests PROPERTIES LINK_DEPENDS_NO_SHARED ON)
