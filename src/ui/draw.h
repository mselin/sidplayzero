// SPDX-License-Identifier: GPL-3.0-or-later
// What every screen draws with: the theme (SPEC-091), the fonts, and labels that keep to their boxes
// (SPEC-098) and name themselves for the screen description's `boxes` (SPEC-254).
#pragma once

#include <cstdint>
#include <string>

#include "lvgl.h"
#include "spz/screen_size.h"

namespace spz::ui {

// SPEC-091: the bands, as rows of pixels.
const int kWidth = kScreenWidth;
const int kHeight = kScreenHeight;
const int kTopHeight = 16;          // rows 0 to 15
const int kContentTop = 16;         // rows 16 to 135
const int kContentHeight = 120;
const int kActionTop = 136;         // rows 136 to 169
const int kRowHeight = 20;          // a row of a list
const int kRowsShown = 6;           // rows of a list in the content band

// The dark theme.
lv_color_t colour_background();
lv_color_t colour_text();
lv_color_t colour_dim();
lv_color_t colour_highlight();      // the filled bar under a highlighted row
lv_color_t colour_panel();
lv_color_t colour_line();

// SPEC-091: the 12-pixel font for all text, the 14-pixel font for the title on the now-playing screen.
// The app's own fonts (SPEC-010).
const lv_font_t* text_font();
const lv_font_t* title_font();

enum class Align { Left, Centre, Right };

struct LabelStyle {
    Align align = Align::Left;
    const lv_font_t* font = nullptr;   // the 12-pixel font when null
    bool dim = false;
    bool cut_start = false;            // cut a text too long from its start, not its end (a path, SPEC-091)
    // SPEC-098: 0 or more for a text that scrolls sideways instead of being cut: the whole text, moved left
    // by this many pixels within its box (scroll_offset() gives it). The box is marked as cut.
    int scroll = -1;
};

// SPEC-098: how far a text `overflow` pixels too wide for its box has scrolled at `now_ms`: it rests 1 s at
// each end and moves at 25 pixels a second between them.
int scroll_offset(int overflow, std::int64_t now_ms);

// A label in a box of its own, named `id` in the screen description. Text too wide for the box is cut and
// ends in "…", or begins with it with `cut_start` (SPEC-098), and the box is marked as cut; nothing is drawn
// outside it.
lv_obj_t* make_label(lv_obj_t* parent, const std::string& id, const std::string& text, int x, int y, int w, int h,
                     const LabelStyle& style = LabelStyle());

// A filled rectangle with no text: the highlight bar, a panel's frame, a tick.
lv_obj_t* make_fill(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t colour);

// A plain container at a place, which draws nothing itself and does not scroll.
lv_obj_t* make_area(lv_obj_t* parent, int x, int y, int w, int h);

// How wide a text is in a font, in pixels.
int text_width(const std::string& text, const lv_font_t* font);

// What make_label() recorded of a label: the text it was given, the width that text needs, and whether
// it was cut. False for an object that make_label() did not make.
// What make_label recorded of a label: its id, the width and text it would need whole, and whether it was cut.
bool label_record(lv_obj_t* label, std::string* id, int* full_width, bool* cut, std::string* full_text = nullptr);
// Whether a label made by make_label() scrolls (LabelStyle::scroll): its box is then its parent's.
bool label_scrolls(lv_obj_t* label);

}  // namespace spz::ui
