// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/empty_screen.h"

#include <exception>

#include "spz/notice_gate.h"
#include "ui/draw.h"

namespace spz::ui {

EmptyScreen::EmptyScreen(Shell* shell) : Screen(shell) {
    Row row;
    row.text = "Get HVSC";
    row.kind = "action";
    rows_.set_rows({row});
}

void EmptyScreen::draw(lv_obj_t* content) {
    make_label(content, "line1", "No collection was found in", 4, 2, 312, kRowHeight);
    make_label(content, "line2", shell()->services().collection, 4, 22, 312, kRowHeight);
    lv_obj_t* row = make_area(content, 0, 42, kWidth, kRowHeight);
    rows_.draw(row, 0);
    make_label(content, "line3", "or copy a C64Music folder there.", 4, 64, 312, kRowHeight);
}

bool EmptyScreen::on_action(const KeyEvent& event) {
    if (rows_.on_action(event.action)) return true;
    if (event.action != "get_hvsc") return false;
    // SPEC-090: "Get HVSC" opens the HVSC screen, once SPEC-152's notice has been read and accepted. Both the
    // gate and the HVSC screen belong to other areas; while either is not built, the key does nothing.
    NoticeGate* gate = shell()->services().notice_gate;
    if (gate == nullptr) return true;
    Shell* frame = shell();
    try {
        gate->pass([frame] { frame->open("hvsc"); }, nullptr);
    } catch (const std::exception&) {
        // not built yet
    }
    return true;
}

void EmptyScreen::describe(Json* description) const {
    description->set("rows", rows_.describe_rows());
}

}  // namespace spz::ui
