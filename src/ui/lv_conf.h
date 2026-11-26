/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LVGL 9.5.0's settings for SidPlayZero. Only what differs from LVGL's defaults is here.
 * SPZ_LV_USE_SDL and SPZ_LV_USE_FBDEV come from the build: a window on a desktop, the framebuffer on the
 * device, neither when the program only ever draws into memory. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32

/* The system's allocator, so that the sanitizers see every allocation. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

/* One UI thread (SPEC-022): LVGL needs no locking of its own. */
#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 130

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* The app's own fonts (SPEC-010, src/ui/fonts) are LVGL's default too, so that nothing in the program
 * draws with a font of LVGL's. Montserrat 12 is still compiled into LVGL's library for the framework's
 * canary test (tests/framework/sim), which asks it for a glyph; the program does not link it. */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(spz_font_dejavu_sans_12)
#define LV_FONT_DEFAULT &spz_font_dejavu_sans_12

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/* For screenshots (SPEC-108) and the tests' `shot`. */
#define LV_USE_SNAPSHOT 1

#define LV_USE_SDL SPZ_LV_USE_SDL
#define LV_USE_LINUX_FBDEV SPZ_LV_USE_FBDEV
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BUFFER_COUNT 0
#define LV_LINUX_FBDEV_BUFFER_SIZE 60

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
