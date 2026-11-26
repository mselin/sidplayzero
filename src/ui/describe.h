// SPDX-License-Identifier: GPL-3.0-or-later
// The screen description (SPEC-254): what is on the screen, as data, for the simulator's tests.
#pragma once

#include "lvgl.h"
#include "spz/json.h"

namespace spz::ui {

// The bands of SPEC-091.
const int kScreenWidth = 320;
const int kScreenHeight = 170;
const int kTopLineBottom = 15;
const int kContentBottom = 135;

// A description with every field of SPEC-254, each null, for a screen to fill in.
Json blank_description();

// Fills in `text` and `boxes` from the labels that are drawn on the active screen, in reading order. A
// label made by make_label() (ui/draw.h) gives its name, the width its whole text needs, and whether it
// was cut to fit.
void describe_drawn(Json* description);

}  // namespace spz::ui
