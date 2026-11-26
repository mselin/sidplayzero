// SPDX-License-Identifier: GPL-3.0-or-later
// The app's own fonts (SPEC-010). Their sources, src/ui/fonts/dejavu_sans_{12,14}.c, are made from DejaVu
// Sans by tools/make_fonts.py (`cmake -P cmake/fonts.cmake`) and kept in the repository.
#include "ui/app_fonts.h"

extern "C" {
extern const lv_font_t spz_font_dejavu_sans_12;
extern const lv_font_t spz_font_dejavu_sans_14;
}

namespace spz::ui {

const lv_font_t* font_12() { return &spz_font_dejavu_sans_12; }
const lv_font_t* font_14() { return &spz_font_dejavu_sans_14; }

}  // namespace spz::ui
