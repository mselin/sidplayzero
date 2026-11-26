// SPDX-License-Identifier: GPL-3.0-or-later
// The empty screen (SPEC-048): at the bottom of the stack in the browser's place while the collection
// holds nothing. Its one row is "Get HVSC". Until the browser is built, it stands in the browser's place.
#pragma once

#include "ui/rows.h"
#include "ui/screen.h"

namespace spz::ui {

class EmptyScreen : public Screen {
public:
    explicit EmptyScreen(Shell* shell);
    std::string name() const override { return "empty"; }
    std::string title() const override { return "SidPlayZero"; }
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;

private:
    RowList rows_;
};

}  // namespace spz::ui
