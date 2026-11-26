// SPDX-License-Identifier: GPL-3.0-or-later
// The test framework's own stand-in screen (TF-005): the empty screen of SPEC-048, drawn alone, without
// the app around it. The app does not use it; the framework's C++ tests (tests/framework/sim, TF-043 and
// TF-044) draw it to check the screen description and the pictures without starting the app.
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
