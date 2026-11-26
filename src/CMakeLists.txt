# SPDX-License-Identifier: GPL-3.0-or-later
#
# The program: LVGL, then the `ui` and `platform` layers, then the program itself (SPEC-020).
#

# --- LVGL ---------------------------------------------------------------------------------------------
spz_fetch_lvgl(SPZ_LVGL_DIR)

set(_spz_use_sdl 0)
set(_spz_use_fbdev 0)
if(SIDPLAYZERO_DISPLAY STREQUAL "window")
    set(_spz_use_sdl 1)
elseif(SIDPLAYZERO_DISPLAY STREQUAL "framebuffer")
    set(_spz_use_fbdev 1)
elseif(NOT SIDPLAYZERO_DISPLAY STREQUAL "none")
    message(FATAL_ERROR "SIDPLAYZERO_DISPLAY must be window, framebuffer or none")
endif()

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ui/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(LV_BUILD_SET_CONFIG_OPTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${SPZ_LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)
target_compile_definitions(lvgl PUBLIC SPZ_LV_USE_SDL=${_spz_use_sdl} SPZ_LV_USE_FBDEV=${_spz_use_fbdev})
target_compile_options(lvgl PRIVATE -w)
if(_spz_use_sdl)
    find_package(SDL2 REQUIRED)
    if(TARGET SDL2::SDL2)
        target_link_libraries(lvgl PUBLIC SDL2::SDL2)
    else()
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
    endif()
endif()

# --- ui: screens and drawing. May use core and LVGL; not the engine, not the sound library ---------------
# The fonts of SPEC-010: C sources made ahead of time by tools/make_fonts.py (cmake/fonts.cmake) and kept in
# the repository. The 12-pixel one is LVGL's default font (lv_conf.h), and the fonts call LVGL's font
# functions: each needs the other. So they are compiled on their own, with our warnings and LVGL's headers,
# and their objects are put into LVGL's library itself: every program that has LVGL has them, and no two
# libraries need each other. As files of `ui`, they are built with `core`'s include path too (SPEC-020).
add_library(spz_fonts OBJECT ui/fonts/dejavu_sans_12.c ui/fonts/dejavu_sans_14.c)
target_link_libraries(spz_fonts PRIVATE spz_core)
target_include_directories(spz_fonts SYSTEM PRIVATE $<TARGET_PROPERTY:lvgl,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(spz_fonts PRIVATE $<TARGET_PROPERTY:lvgl,INTERFACE_COMPILE_DEFINITIONS>)
target_compile_options(spz_fonts PRIVATE ${SPZ_WARNINGS})
target_sources(lvgl PRIVATE $<TARGET_OBJECTS:spz_fonts>)

# Each source named, so that nothing that lies in the folder is built in by chance.
add_library(spz_ui STATIC
    ui/about_screen.cpp
    ui/app_fonts.cpp
    ui/browser_screen.cpp
    ui/describe.cpp
    ui/draw.cpp
    ui/empty_screen.cpp
    ui/filter_panel.cpp
    ui/hvsc_screen.cpp
    ui/lists_panels.cpp
    ui/panel.cpp
    ui/playing_screen.cpp
    ui/playlist_screen.cpp
    ui/playlists_screen.cpp
    ui/png.cpp
    ui/rows.cpp
    ui/search_screen.cpp
    ui/selftest_screen.cpp
    ui/settings_screen.cpp
)
target_include_directories(spz_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(spz_ui PUBLIC spz_core lvgl)
target_compile_options(spz_ui PRIVATE ${SPZ_WARNINGS})

# --- platform: the outside world -----------------------------------------------------------------------
# The real seams (SPEC-021): the engine (cmake/engine.cmake), sound through miniaudio (SPEC-005), HTTP
# through the system's libcurl (SPEC-006), the 7z runner, the volume, the backlight, the keyboard and the
# display. An object library, so that every part of it is in the program whether or not the shell uses it
# yet: the engine and the sound library are always built in (SPEC-002, SPEC-005). The engine itself,
# platform/real_engine.cpp, is the library spz_engine (cmake/engine.cmake), which the fuzz targets link too:
# it is compiled once, there.
spz_fetch(miniaudio _spz_miniaudio_header)
get_filename_component(_spz_miniaudio_dir "${_spz_miniaudio_header}" DIRECTORY)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
add_library(spz_platform OBJECT
    platform/audio_guard.cpp
    platform/child_process.cpp
    platform/curl_http.cpp
    platform/display.cpp
    platform/input_setup.cpp
    platform/keyboard.cpp
    platform/miniaudio_sink.cpp
    platform/real_clock.cpp
    platform/real_files.cpp
    platform/software_sound.cpp
    platform/system_seams.cpp
    platform/miniaudio_impl.c
)
target_include_directories(spz_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(spz_platform SYSTEM PRIVATE "${_spz_miniaudio_dir}")
target_link_libraries(spz_platform PUBLIC spz_core spz_engine spz::engine_libraries lvgl CURL::libcurl
    Threads::Threads ${CMAKE_DL_LIBS})
add_dependencies(spz_platform spz_libsidplayfp)
target_compile_definitions(spz_platform PRIVATE
    SPZ_DISPLAY_WINDOW=${_spz_use_sdl} SPZ_DISPLAY_FRAMEBUFFER=${_spz_use_fbdev}
    # miniaudio: PulseAudio first, then ALSA, each loaded when the program runs; the null device for tests.
    MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_PULSEAUDIO MA_ENABLE_ALSA MA_ENABLE_NULL
    MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE)
# The guard of the audio callback (SPEC-022) is in a test build without a sanitizer: a sanitizer brings
# allocators of its own.
if(SIDPLAYZERO_TEST_HOOKS AND SIDPLAYZERO_SANITIZE STREQUAL "")
    target_compile_definitions(spz_platform PRIVATE SPZ_AUDIO_GUARD=1)
endif()
if(SIDPLAYZERO_TEST_HOOKS)
    target_compile_definitions(spz_platform PRIVATE SPZ_TEST_HOOKS=1)
endif()
set_source_files_properties(platform/miniaudio_impl.c PROPERTIES COMPILE_OPTIONS "-w")
target_compile_options(spz_platform PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SPZ_WARNINGS}>)

# --- the program ---------------------------------------------------------------------------------------
# The app's frame (SPEC-022, SPEC-090): its threads, the stack of screens, and the list of screens.
add_executable(sidplayzero
    app/main.cpp
    app/start.cpp
    app/app.cpp
    app/workers.cpp
    app/screens.cpp
    # The composition (SPEC-020 to SPEC-024): one file for its order, and one for each area.
    app/composition.cpp
    app/compose_keep.cpp
    app/compose_hvsc.cpp
    app/compose_browse.cpp
    app/compose_play.cpp
    app/compose_search.cpp
    app/compose_lists.cpp
    app/compose_selftest.cpp
    app/selftest_benchmark.cpp
    app/selftest_script.cpp
)
target_link_libraries(sidplayzero PRIVATE spz_ui spz_platform Threads::Threads)
target_compile_options(sidplayzero PRIVATE ${SPZ_WARNINGS})
if(SIDPLAYZERO_TEST_HOOKS)
    # The hooks the simulator tests drive the program by (SPEC-253). Never in a package.
    target_sources(sidplayzero PRIVATE app/hooks/script.cpp app/hooks/sim_fakes.cpp)
    target_include_directories(sidplayzero PRIVATE "${CMAKE_SOURCE_DIR}/tests/fakes")
    target_compile_definitions(sidplayzero PRIVATE SPZ_TEST_HOOKS=1)
    # The integration tests' way into the `platform` layer (see app/hooks/tool_main.cpp).
    add_executable(spz_tool app/hooks/tool_main.cpp app/hooks/tool_hvsc.cpp app/hooks/tool_sound.cpp app/workers.cpp)
    target_link_libraries(spz_tool PRIVATE spz_platform)
    target_compile_definitions(spz_tool PRIVATE SPZ_TEST_HOOKS=1)
    target_compile_options(spz_tool PRIVATE ${SPZ_WARNINGS})
else()
    target_compile_definitions(sidplayzero PRIVATE SPZ_TEST_HOOKS=0)
endif()
install(TARGETS sidplayzero RUNTIME DESTINATION bin)
