# SPDX-License-Identifier: GPL-3.0-or-later
#
# The program: LVGL, then the `ui` and `platform` layers, then the program itself (SPEC-020).
#
# What is here is a stub (docs/test-framework-tasks.md, TF-005): the least that lets every level of
# tests run. Phase 5 replaces it with the app.

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
add_library(spz_ui STATIC
    ui/describe.cpp
    ui/png.cpp
    ui/stub_screen.cpp
)
target_include_directories(spz_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(spz_ui PUBLIC spz_core lvgl)
target_compile_options(spz_ui PRIVATE ${SPZ_WARNINGS})

# --- platform: the outside world -----------------------------------------------------------------------
add_library(spz_platform STATIC
    platform/display.cpp
    platform/keyboard.cpp
    platform/real_clock.cpp
)
target_include_directories(spz_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(spz_platform PUBLIC spz_core lvgl)
target_compile_definitions(spz_platform PRIVATE SPZ_DISPLAY_WINDOW=${_spz_use_sdl} SPZ_DISPLAY_FRAMEBUFFER=${_spz_use_fbdev})
target_compile_options(spz_platform PRIVATE ${SPZ_WARNINGS})

# --- the program ---------------------------------------------------------------------------------------
add_executable(sidplayzero
    app/main.cpp
    app/stub_app.cpp
)
target_link_libraries(sidplayzero PRIVATE spz_ui spz_platform)
target_compile_options(sidplayzero PRIVATE ${SPZ_WARNINGS})
if(SIDPLAYZERO_TEST_HOOKS)
    # The hooks the simulator tests drive the program by (SPEC-253). Never in a package.
    target_sources(sidplayzero PRIVATE app/hooks/script.cpp app/hooks/sim_fakes.cpp)
    target_include_directories(sidplayzero PRIVATE "${CMAKE_SOURCE_DIR}/tests/fakes")
    target_compile_definitions(sidplayzero PRIVATE SPZ_TEST_HOOKS=1)
    # The integration tests' way into the `platform` layer (see app/hooks/tool_main.cpp).
    add_executable(spz_tool app/hooks/tool_main.cpp)
    target_link_libraries(spz_tool PRIVATE spz_platform)
    target_compile_options(spz_tool PRIVATE ${SPZ_WARNINGS})
else()
    target_compile_definitions(sidplayzero PRIVATE SPZ_TEST_HOOKS=0)
endif()
install(TARGETS sidplayzero RUNTIME DESTINATION bin)
