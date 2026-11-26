// SPDX-License-Identifier: GPL-3.0-or-later
// A list of rows (SPEC-091): rows of 20 pixels, a highlight that the `lists` keys move, the window of rows
// that shows, and its part of the screen description (SPEC-254: `rows`, `list`, `position`).
#pragma once

#include <string>
#include <vector>

#include "lvgl.h"
#include "spz/json.h"
#include "spz/screen_size.h"

namespace spz::ui {

struct Row {
    std::string text;                 // as SPEC-254 has `rows[].text`
    std::string kind;                 // "folder", "tune", "entry", "setting", "action"
    std::string value;                // a setting's value; "" for none
    bool has_value = false;
    std::vector<std::string> marks;   // "absent", "fetching", "missing", "favourite", "playing"
};

class RowList {
public:
    void set_rows(std::vector<Row> rows);
    const std::vector<Row>& rows() const { return rows_; }
    std::size_t size() const { return rows_.size(); }

    // The highlighted row, counted from 0; 0 for an empty list.
    std::size_t highlighted() const { return highlighted_; }
    void highlight(std::size_t index);
    // How many rows show at once: six, or five with a detail line under them.
    void set_shown(int rows);
    int shown() const { return shown_; }

    // The `lists` actions: "up", "down", "page_up", "page_down", "first", "last". True for one of these,
    // whether or not the highlight could move; false for any other action.
    bool on_action(const std::string& action);

    // Draws the rows that show, from `y` in `parent`, each `kRowHeight` high and `width` wide.
    void draw(lv_obj_t* parent, int y, int width = kScreenWidth) const;

    // "n/total", or "" for an empty list.
    std::string position() const;
    Json describe_rows() const;
    Json describe_list() const;

private:
    void keep_in_view();

    std::vector<Row> rows_;
    std::size_t highlighted_ = 0;
    std::size_t first_ = 0;
    int shown_ = 6;
};

}  // namespace spz::ui
