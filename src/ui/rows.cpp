// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/rows.h"

#include <algorithm>
#include <utility>

#include "ui/draw.h"

namespace spz::ui {

void RowList::set_rows(std::vector<Row> rows) {
    rows_ = std::move(rows);
    if (highlighted_ >= rows_.size()) highlighted_ = rows_.empty() ? 0 : rows_.size() - 1;
    keep_in_view();
}

void RowList::highlight(std::size_t index) {
    highlighted_ = rows_.empty() ? 0 : std::min(index, rows_.size() - 1);
    keep_in_view();
}

void RowList::set_shown(int rows) {
    shown_ = std::max(1, rows);
    keep_in_view();
}

void RowList::keep_in_view() {
    const std::size_t shown = static_cast<std::size_t>(shown_);
    if (highlighted_ < first_) first_ = highlighted_;
    if (highlighted_ >= first_ + shown) first_ = highlighted_ + 1 - shown;
    const std::size_t most = rows_.size() > shown ? rows_.size() - shown : 0;
    if (first_ > most) first_ = most;
}

bool RowList::on_action(const std::string& action) {
    const std::size_t last = rows_.empty() ? 0 : rows_.size() - 1;
    const std::size_t page = static_cast<std::size_t>(shown_);
    if (action == "up") highlighted_ = highlighted_ > 0 ? highlighted_ - 1 : 0;
    else if (action == "down") highlighted_ = std::min(highlighted_ + 1, last);
    else if (action == "page_up") highlighted_ = highlighted_ > page ? highlighted_ - page : 0;
    else if (action == "page_down") highlighted_ = std::min(highlighted_ + page, last);
    else if (action == "first") highlighted_ = 0;
    else if (action == "last") highlighted_ = last;
    else return false;
    keep_in_view();
    return true;
}

void RowList::draw(lv_obj_t* parent, int y, int width) const {
    const std::size_t end = std::min(rows_.size(), first_ + static_cast<std::size_t>(shown_));
    for (std::size_t i = first_; i < end; ++i) {
        const int top = y + static_cast<int>(i - first_) * kRowHeight;
        lv_obj_t* row = make_area(parent, 0, top, width, kRowHeight);
        if (i == highlighted_) {
            lv_obj_set_style_bg_color(row, colour_highlight(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        }
        const Row& r = rows_[i];
        const std::string id = "row" + std::to_string(i - first_);
        std::string marks;
        for (const std::string& mark : r.marks) {
            if (mark == "favourite") marks += "*";
            else if (mark == "playing") marks += ">";
            else if (mark == "absent") marks += "~";
            else if (mark == "fetching") marks += "+";
            else if (mark == "missing") marks += "!";
        }
        const int mark_w = marks.empty() ? 0 : 16;
        if (!marks.empty()) make_label(row, id + ".marks", marks, 2, 0, mark_w, kRowHeight, LabelStyle{Align::Left, nullptr, true});
        if (r.has_value) {
            const int name_w = (width - 12) / 2;
            make_label(row, id, r.text, 4 + mark_w, 0, name_w - mark_w, kRowHeight);
            make_label(row, id + ".value", r.value, 8 + name_w, 0, width - 12 - name_w, kRowHeight,
                       LabelStyle{Align::Right, nullptr, false});
        } else {
            make_label(row, id, r.text, 4 + mark_w, 0, width - 8 - mark_w, kRowHeight);
        }
    }
}

std::string RowList::position() const {
    if (rows_.empty()) return "";
    return std::to_string(highlighted_ + 1) + "/" + std::to_string(rows_.size());
}

Json RowList::describe_rows() const {
    Json rows = Json::array();
    const std::size_t end = std::min(rows_.size(), first_ + static_cast<std::size_t>(shown_));
    for (std::size_t i = first_; i < end; ++i) {
        const Row& r = rows_[i];
        Json row = Json::object();
        row.set("text", r.text);
        row.set("kind", r.kind);
        row.set("value", r.has_value ? Json(r.value) : Json());
        Json marks = Json::array();
        for (const std::string& mark : r.marks) marks.push(mark);
        row.set("marks", marks);
        row.set("highlighted", i == highlighted_);
        rows.push(row);
    }
    return rows;
}

Json RowList::describe_list() const {
    Json list = Json::object();
    list.set("total", static_cast<std::int64_t>(rows_.size()));
    list.set("first", static_cast<std::int64_t>(first_));
    // The LVGL objects of the list: for each row shown, its area, its label, and a value's label.
    std::int64_t objects = 0;
    const std::size_t end = std::min(rows_.size(), first_ + static_cast<std::size_t>(shown_));
    for (std::size_t i = first_; i < end; ++i) objects += 2 + (rows_[i].has_value ? 1 : 0) + (rows_[i].marks.empty() ? 0 : 1);
    list.set("objects", objects);
    return list;
}

}  // namespace spz::ui
