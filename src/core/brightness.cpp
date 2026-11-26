// SPDX-License-Identifier: GPL-3.0-or-later
// The brightness keys (SPEC-104).
#include "spz/brightness.h"

namespace spz {

BrightnessStep step_brightness(Backlight* backlight, bool up) {
    BrightnessStep step;
    int level = 0;
    if (backlight == nullptr || !backlight->can_change() || !backlight->get(&level)) {
        step.notice = "MSG_BRIGHTNESS_UNAVAILABLE";
        return step;
    }
    int wanted = up ? (level / 10 + 1) * 10 : ((level + 9) / 10 - 1) * 10;
    if (wanted < 10) wanted = 10;
    if (wanted > 100) wanted = 100;
    if (wanted != level) {
        if (!backlight->set(wanted)) {
            step.notice = "MSG_BRIGHTNESS_UNAVAILABLE";
            return step;
        }
    }
    step.notice = "MSG_BRIGHTNESS";
    step.percent = wanted;
    return step;
}

}  // namespace spz
