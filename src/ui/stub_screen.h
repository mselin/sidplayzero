// SPDX-License-Identifier: GPL-3.0-or-later
// STUB (TF-005). The one screen the stub draws: the empty screen of SPEC-048, without its behaviour.
// Phase 5 replaces this file with the app's screens.
#pragma once

#include <string>

#include "lvgl.h"
#include "spz/json.h"

namespace spz::ui {

class StubScreen {
public:
    // Draws the screen on LVGL's active screen. `root` is the collection folder it names.
    explicit StubScreen(const std::string& root);
    ~StubScreen();

    // Shows or hides the one notice the stub has, "Keep holding ESC to leave" (SPEC-106).
    void show_hold_notice(bool shown);

    // The screen description of SPEC-254.
    Json describe() const;

private:
    std::string root_;
    lv_obj_t* notice_ = nullptr;
    bool notice_shown_ = false;
};

}  // namespace spz::ui
