// SPDX-License-Identifier: GPL-3.0-or-later
// Where LVGL draws its 320×170 screen: the device's framebuffer, a window on a desktop, or memory only.
#pragma once

#include <string>

namespace spz::platform {

enum class DisplayKind {
    Memory,       // nowhere to be seen: for tests
    Window,       // a desktop window (SDL)
    Framebuffer,  // the device's screen
};

// Creates LVGL's display. False, with the reason, if this build cannot draw there or it cannot be opened.
bool open_display(DisplayKind kind, std::string* why);

// What this build draws on when it is not told otherwise.
DisplayKind default_display();

}  // namespace spz::platform
