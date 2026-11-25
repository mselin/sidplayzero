// SPDX-License-Identifier: GPL-3.0-or-later
// The brightness keys (SPEC-104), the part `brightness`.
#pragma once

#include <string>

#include "spz/system.h"

namespace spz {

// What a brightness key did, as the notice to show for it.
struct BrightnessStep {
    std::string notice;   // "MSG_BRIGHTNESS", or "MSG_BRIGHTNESS_UNAVAILABLE" when the level cannot be changed
    int percent = 0;      // with MSG_BRIGHTNESS: the level the screen has now, for the notice's "n"
};

// SPEC-104: one press of a brightness key. Reads the level through `backlight` and sets it to the nearest
// multiple of 10 % above (`up`) or below, never below 10 % and never above 100 %; at such an end it sets
// nothing, and the notice shows the level as it is. When `backlight` cannot change the level, nothing is
// set and the notice is MSG_BRIGHTNESS_UNAVAILABLE. The app calls this on the quick worker (SPEC-022).
BrightnessStep step_brightness(Backlight* backlight, bool up);

}  // namespace spz
