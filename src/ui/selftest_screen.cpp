// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/selftest_screen.h"

#include "ui/draw.h"

namespace spz::ui {

const char* const kSelfTestDone = "Done. Hold ESC to leave.";

SelfTestScreen::SelfTestScreen(Shell* shell) : Screen(shell) {}

std::vector<std::string> SelfTestScreen::now() const {
    const SelfTestView* view = shell()->services().selftest;
    return view ? view->lines() : std::vector<std::string>();
}

std::string SelfTestScreen::place() const {
    const SelfTestView* view = shell()->services().selftest;
    return view ? view->place() : "selftest";
}

void SelfTestScreen::draw(lv_obj_t* content) {
    drawn_ = now();
    for (std::size_t i = 0; i < drawn_.size() && i < static_cast<std::size_t>(kRowsShown); ++i) {
        make_label(content, "line" + std::to_string(i), drawn_[i], 4, static_cast<int>(i) * kRowHeight, kWidth - 8,
                   kRowHeight);
    }
}

bool SelfTestScreen::on_action(const KeyEvent& event) {
    // No keys of its own (SPEC-260): the frame has done what the groups of the key map do. The key step's
    // `record_key` is the self-test's, registered with the frame.
    (void)event;
    return false;
}

void SelfTestScreen::describe(Json* description) const {
    Json lines = Json::array();
    for (const std::string& line : now()) lines.push(line);
    description->set("lines", lines);
}

void SelfTestScreen::tick() {
    if (now() != drawn_) shell()->redraw();
}

}  // namespace spz::ui
