// SPDX-License-Identifier: GPL-3.0-or-later
// The app's own fonts (SPEC-010): bitmap fonts made ahead of time from DejaVu Sans at 12 and 14 pixels,
// whose generated sources are in the repository and built into the program. No font file is loaded when
// the program runs. Each covers U+0020 to U+007E, U+00A0 to U+00FF, the 27 characters that Windows-1252
// has at 0x80 to 0x9F, and the symbols the screens use.
#pragma once

#include "lvgl.h"

namespace spz::ui {

// The 12-pixel font: all text but the title on the now-playing screen (SPEC-091).
const lv_font_t* font_12();
// The 14-pixel font: the title on the now-playing screen.
const lv_font_t* font_14();

}  // namespace spz::ui
