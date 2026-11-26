// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/display.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "lvgl.h"
#include "spz/screen_size.h"

#if SPZ_DISPLAY_FRAMEBUFFER
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#if SPZ_DISPLAY_WINDOW
#include "src/drivers/sdl/lv_sdl_window.h"
#endif

namespace spz::platform {

namespace {

const int kWidth = kScreenWidth;
const int kHeight = kScreenHeight;

void flush_to_nowhere(lv_display_t* display, const lv_area_t*, std::uint8_t*) {
    lv_display_flush_ready(display);
}

}  // namespace

DisplayKind default_display() {
#if SPZ_DISPLAY_FRAMEBUFFER
    return DisplayKind::Framebuffer;
#elif SPZ_DISPLAY_WINDOW
    return DisplayKind::Window;
#else
    return DisplayKind::Memory;
#endif
}

bool open_display(DisplayKind kind, std::string* why) {
    switch (kind) {
        case DisplayKind::Memory: {
            static std::vector<std::uint8_t> buffer(static_cast<std::size_t>(kWidth) * kHeight * 4);
            lv_display_t* display = lv_display_create(kWidth, kHeight);
            if (display == nullptr) { *why = "LVGL could not create a display"; return false; }
            lv_display_set_buffers(display, buffer.data(), nullptr, static_cast<std::uint32_t>(buffer.size()),
                                   LV_DISPLAY_RENDER_MODE_FULL);
            lv_display_set_flush_cb(display, flush_to_nowhere);
            return true;
        }
        case DisplayKind::Window: {
#if SPZ_DISPLAY_WINDOW
            lv_display_t* display = lv_sdl_window_create(kWidth, kHeight);
            if (display == nullptr) { *why = "no window could be opened"; return false; }
            lv_sdl_window_set_title(display, "SidPlayZero");
            return true;
#else
            *why = "this build has no window";
            return false;
#endif
        }
        case DisplayKind::Framebuffer: {
#if SPZ_DISPLAY_FRAMEBUFFER
            const char* device = std::getenv("SIDPLAYZERO_FRAMEBUFFER");
            lv_display_t* display = lv_linux_fbdev_create();
            if (display == nullptr) { *why = "LVGL could not create a display"; return false; }
            if (lv_linux_fbdev_set_file(display, device && *device ? device : "/dev/fb0") != LV_RESULT_OK) {
                *why = "the framebuffer could not be opened";
                return false;
            }
            return true;
#else
            *why = "this build does not draw to a framebuffer";
            return false;
#endif
        }
    }
    return false;
}

}  // namespace spz::platform
