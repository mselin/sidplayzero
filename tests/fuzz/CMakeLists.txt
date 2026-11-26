# SPDX-License-Identifier: GPL-3.0-or-later
#
# The fuzz targets (SPEC-205, TF-060): one program for each tests/fuzz/*_fuzz.cpp, built with libFuzzer,
# AddressSanitizer and UndefinedBehaviorSanitizer. This build is configured with clang, and `core` is
# built with the same sanitizers (see the top CMakeLists.txt).
file(GLOB _fuzz_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_fuzz.cpp")
if(SIDPLAYZERO_FUZZ_CANARY)
    # TF-060: a reader with a deliberate fault, for the proof that the fuzz command notices one.
    file(GLOB _canary_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/framework/fuzz/*_fuzz.cpp")
    list(APPEND _fuzz_sources ${_canary_sources})
endif()

foreach(_source IN LISTS _fuzz_sources)
    get_filename_component(_name "${_source}" NAME_WE)
    add_executable(${_name} "${_source}")
    target_link_libraries(${_name} PRIVATE spz_core)
    target_compile_options(${_name} PRIVATE ${SPZ_WARNINGS})
    target_link_options(${_name} PRIVATE -fsanitize=fuzzer)
    if(_name STREQUAL "engine_fuzz")
        # The one target that reaches the engine: libsidplayfp, built with these sanitizers (cmake/engine.cmake).
        target_link_libraries(${_name} PRIVATE spz_engine)
    endif()
endforeach()
