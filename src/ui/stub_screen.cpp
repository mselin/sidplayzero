// SPDX-License-Identifier: GPL-3.0-or-later
// STUB (TF-005): see stub_screen.h.
#include "ui/stub_screen.h"

#include "ui/describe.h"

namespace spz::ui {

namespace {

const int kActionCentres[5] = {49, 105, 161, 217, 273};   // SPEC-091
const char* const kActionIds[5] = {"action4", "action5", "action6", "action7", "action8"};

lv_obj_t* label(lv_obj_t* parent, const char* id, const char* text, int x, int y, int width) {
    lv_obj_t* object = lv_label_create(parent);
    lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_obj_set_height(object, 16);
    lv_label_set_text(object, text);
    name_box(object, id);
    return object;
}

}  // namespace

StubScreen::StubScreen(const std::string& root) : root_(root) {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101014), LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xE8E8E8), LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    label(screen, "title", "SidPlayZero", 4, 0, 200);
    label(screen, "line1", "No collection was found in", 4, 18, 312);
    label(screen, "line2", root_.c_str(), 4, 38, 312);

    // The one row, highlighted: a filled bar with the row's text on it.
    lv_obj_t* bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 0, 58);
    lv_obj_set_size(bar, kScreenWidth, 20);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x2E5AAC), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    label(bar, "row0", "Get HVSC", 4, 2, 312);

    label(screen, "line3", "or copy a C64Music folder there.", 4, 80, 312);

    notice_ = label(screen, "notice", "Keep holding ESC to leave", 4, 118, 312);
    lv_obj_add_flag(notice_, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 5; ++i) {
        lv_obj_t* action = label(screen, kActionIds[i], "", kActionCentres[i] - 26, 140, 52);
        lv_obj_set_style_text_align(action, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

StubScreen::~StubScreen() {
    lv_obj_clean(lv_screen_active());
}

void StubScreen::show_hold_notice(bool shown) {
    notice_shown_ = shown;
    if (shown) {
        lv_obj_remove_flag(notice_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(notice_, LV_OBJ_FLAG_HIDDEN);
    }
}

Json StubScreen::describe() const {
    Json d = blank_description();
    d.set("screen", "empty");
    Json stack = Json::array();
    stack.push("empty");
    d.set("stack", stack);
    d.set("place", "empty");
    d.set("title", "SidPlayZero");

    Json rows = Json::array();
    Json row = Json::object();
    row.set("text", "Get HVSC");
    row.set("kind", "action");
    row.set("value", nullptr);
    row.set("marks", Json::array());
    row.set("highlighted", true);
    rows.push(row);
    d.set("rows", rows);

    Json actions = Json::array();
    for (int i = 0; i < 5; ++i) actions.push("");
    d.set("actions", actions);
    d.set("typing", false);

    if (notice_shown_) {
        Json notice = Json::object();
        notice.set("id", "MSG_HOLD_TO_LEAVE");
        notice.set("text", "Keep holding ESC to leave");
        d.set("notice", notice);
    }
    describe_drawn(&d);
    return d;
}

}  // namespace spz::ui
