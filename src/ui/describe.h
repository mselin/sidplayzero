// SPDX-License-Identifier: GPL-3.0-or-later
// The screen description (SPEC-254): what is on the screen, as data, for the simulator's tests.
#pragma once

#include "lvgl.h"
#include "spz/json.h"
#include "ui/draw.h"

namespace spz::ui {

// The last row of pixels of the top line and of the content band (SPEC-091): a box is in the band its top
// row is in.
const int kTopLineBottom = kContentTop - 1;
const int kContentBottom = kActionTop - 1;

// A description with every field of SPEC-254, each null, for a screen to fill in.
Json blank_description();

// Fills in `text` and `boxes` from the labels that are drawn on the active screen, in reading order. A
// label made by make_label() (ui/draw.h) gives its name, the width its whole text needs, and whether it
// was cut to fit.
void describe_drawn(Json* description);

}  // namespace spz::ui
