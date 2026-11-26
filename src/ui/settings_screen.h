// SPDX-License-Identifier: GPL-3.0-or-later
// The Settings screen (SPEC-101): a row for each setting of this build, with its value. Left and Right
// step a value of a fixed set, Enter steps it forward or opens a prompt for one that is typed. Every
// change goes to SettingsFile (SPEC-130), which saves it at once.
#pragma once

#include "spz/screen_settings.h"
#include "ui/rows.h"
#include "ui/screen.h"

namespace spz::ui {

class SettingsScreen : public Screen {
public:
    explicit SettingsScreen(Shell* shell);
    std::string name() const override { return "settings"; }
    // It needs SettingsFile, of another area.
    bool ready() override;
    std::string title() const override { return "Settings"; }
    std::string position() const override { return list_.position(); }
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;
    void shown() override;

private:
    void refresh();
    std::string value_of(const SettingsRow& row) const;
    bool backlight_unavailable(const SettingsRow& row) const;
    bool fixed_by_variable(const SettingsRow& row) const;
    void step(int by, bool wrap);
    void open_prompt();

    std::vector<SettingsRow> table_;
    RowList list_;
};

}  // namespace spz::ui
