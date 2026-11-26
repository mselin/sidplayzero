// SPDX-License-Identifier: GPL-3.0-or-later
// The browser (SPEC-040 to SPEC-049, SPEC-095): the folder that the browser's model has open, drawn a
// screenful at a time; the detail line of the part `detail` (SPEC-046); the marks of absent, fetching,
// favourite and playing tunes (SPEC-049); and its keys. The model itself is the composition's, and lives
// while the app runs (ui::Services::browser).
#pragma once

#include <memory>
#include <string>

#include "spz/clock.h"
#include "ui/rows.h"
#include "ui/screen.h"

namespace spz {
class BrowserModel;
struct FolderEntry;
}  // namespace spz

namespace spz::ui {

class BrowserScreen : public Screen {
public:
    explicit BrowserScreen(Shell* shell);
    ~BrowserScreen() override;
    std::string name() const override { return "browser"; }
    bool ready() override;
    std::string title() const override;
    std::string position() const override;
    std::array<std::string, 5> actions() const override;
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;

private:
    BrowserModel* model() const;
    int rows_shown() const;
    // The rows that show now, from first_, and where the highlight is among them.
    RowList visible() const;
    // The highlighted entry, if it is a tune: its entry for the player and the playlists.
    bool highlighted_tune(Entry* out) const;
    void play_highlighted();
    void keep_in_view();
    // SPEC-046: the detail line, asked for once the highlight has rested on an entry for 150 ms.
    void watch_detail();
    void ask_detail(const std::string& key);

    std::size_t first_ = 0;
    std::string detail_key_;          // folder and name of the entry the detail is for, or is awaited for
    std::string detail_;              // its text; "" for none
    Clock::TimerId detail_timer_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace spz::ui
