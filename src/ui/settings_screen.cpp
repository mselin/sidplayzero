// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/settings_screen.h"

#include <algorithm>
#include <exception>
#include <memory>

#include "spz/settings_file.h"
#include "spz/system.h"
#include "ui/draw.h"
#include "ui/panel.h"

namespace spz::ui {

SettingsScreen::SettingsScreen(Shell* shell) : Screen(shell), table_(settings_rows()) {}

bool SettingsScreen::ready() { return shell()->services().settings != nullptr; }

bool SettingsScreen::backlight_unavailable(const SettingsRow& row) const {
    if (row.key != "screen_off_after") return false;
    Backlight* backlight = shell()->services().backlight;
    return backlight == nullptr || !backlight->can_change();
}

bool SettingsScreen::fixed_by_variable(const SettingsRow& row) const {
    return row.key == "collection" && shell()->services().collection_from_variable;
}

std::string SettingsScreen::value_of(const SettingsRow& row) const {
    // SPEC-040: with HVSC_BASE set, the row shows the root it gives, and cannot be changed.
    if (fixed_by_variable(row)) return shell()->services().collection + " (from HVSC_BASE)";
    // SPEC-207: with a backlight the app may not change, the row says so and the setting stays 0.
    if (backlight_unavailable(row)) return "Not available on this device";
    return setting_text(shell()->services().settings->settings(), row.key);
}

void SettingsScreen::refresh() {
    std::vector<Row> rows;
    for (const SettingsRow& setting : table_) {
        Row row;
        row.text = setting.name;
        row.kind = "setting";
        row.value = value_of(setting);
        row.has_value = true;
        rows.push_back(row);
    }
    list_.set_rows(std::move(rows));
}

void SettingsScreen::shown() { refresh(); }

void SettingsScreen::draw(lv_obj_t* content) {
    refresh();
    list_.draw(content, 0);
}

void SettingsScreen::step(int by, bool wrap) {
    if (table_.empty()) return;
    const SettingsRow& row = table_[list_.highlighted()];
    if (row.kind != SettingKind::Choice || row.values.empty()) return;
    const std::string now = setting_text(shell()->services().settings->settings(), row.key);
    const auto at = std::find(row.values.begin(), row.values.end(), now);
    const long count = static_cast<long>(row.values.size());
    long index = at == row.values.end() ? 0 : static_cast<long>(at - row.values.begin());
    long next = index + by;
    if (wrap) next = (next % count + count) % count;
    // SPEC-101, FIND-024: Left and Right stop at the first and last values.
    if (next < 0 || next >= count || next == index) return;
    if (shell()->services().settings->set(row.key, row.values[static_cast<std::size_t>(next)]) &&
        shell()->services().setting_changed) {
        shell()->services().setting_changed(row.key);
    }
    refresh();
    shell()->redraw();
}

void SettingsScreen::open_prompt() {
    const SettingsRow& row = table_[list_.highlighted()];
    if (backlight_unavailable(row)) return;
    if (fixed_by_variable(row)) return;
    Shell* frame = shell();
    const std::string key = row.key;
    const std::string now = setting_text(frame->services().settings->settings(), key);
    frame->open_panel(std::make_unique<PromptPanel>(frame, row.name, now, row.limit, [frame, this, key](const std::string& text) {
        // SPEC-101: a value that is not allowed gives the notice and leaves the prompt open.
        if (!frame->services().settings->set(key, text)) {
            frame->notice("MSG_INVALID_VALUE");
            return false;
        }
        if (frame->services().setting_changed) frame->services().setting_changed(key);
        refresh();
        frame->redraw();
        return true;
    }));
}

bool SettingsScreen::on_action(const KeyEvent& event) {
    const std::string& a = event.action;
    if (list_.on_action(a)) {
        shell()->redraw();
        return true;
    }
    if (table_.empty()) return false;
    if (a == "previous_value") step(-1, false);
    else if (a == "next_value") step(1, false);
    else if (a == "change") {
        if (table_[list_.highlighted()].kind == SettingKind::Choice) step(1, true);
        else open_prompt();
    } else if (a == "back") {
        shell()->close_top();
    } else {
        return false;
    }
    return true;
}

void SettingsScreen::describe(Json* description) const {
    description->set("rows", list_.describe_rows());
    description->set("list", list_.describe_list());
}

}  // namespace spz::ui
