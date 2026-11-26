// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/filter_panel.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "ui/draw.h"
#include "ui/screen.h"

namespace spz::ui {

namespace {

const int kPanelX = 6;                       // as the other panels (ui/panel.cpp)
const int kPanelW = kWidth - 2 * kPanelX;

}  // namespace

std::string filter_model_text(SidModel model) {
    switch (model) {
        case SidModel::Mos6581: return "6581";
        case SidModel::Mos8580: return "8580";
        default: return "any";
    }
}

std::string filter_clock_text(C64Clock clock) {
    switch (clock) {
        case C64Clock::Pal: return "PAL";
        case C64Clock::Ntsc: return "NTSC";
        default: return "any";
    }
}

std::string filter_year_text(int year) { return year == 0 ? "any" : std::to_string(year); }

FilterPanel::FilterPanel(Shell* shell, const SearchFilter& filter, int present_year,
                         std::function<void(const SearchFilter&)> on_apply)
    : shell_(shell), filter_(filter), present_year_(std::max(present_year, kFirstYear)),
      on_apply_(std::move(on_apply)) {
    list_.set_shown(4);
    refresh();
}

void FilterPanel::refresh() {
    const std::pair<const char*, std::string> lines[] = {
        {"Chip model", filter_model_text(filter_.model)},
        {"Clock", filter_clock_text(filter_.clock)},
        {"First year", filter_year_text(filter_.first_year)},
        {"Last year", filter_year_text(filter_.last_year)},
    };
    std::vector<Row> rows;
    for (const auto& line : lines) {
        Row row;
        row.text = line.first;
        row.kind = "setting";
        row.value = line.second;
        row.has_value = true;
        rows.push_back(row);
    }
    const std::size_t highlighted = list_.highlighted();
    list_.set_rows(std::move(rows));
    list_.highlight(highlighted);
}

// Left (-1) and Right (+1) on the highlighted line. Each line stops at its ends.
void FilterPanel::step(int by) {
    switch (list_.highlighted()) {
        case 0: {
            const SidModel order[] = {SidModel::FromTune, SidModel::Mos6581, SidModel::Mos8580};
            int at = 0;
            for (int i = 0; i < 3; ++i) if (order[i] == filter_.model) at = i;
            filter_.model = order[std::clamp(at + by, 0, 2)];
            break;
        }
        case 1: {
            const C64Clock order[] = {C64Clock::FromTune, C64Clock::Pal, C64Clock::Ntsc};
            int at = 0;
            for (int i = 0; i < 3; ++i) if (order[i] == filter_.clock) at = i;
            filter_.clock = order[std::clamp(at + by, 0, 2)];
            break;
        }
        default: {
            // "any", then the years from 1982 to the present one.
            int& year = list_.highlighted() == 2 ? filter_.first_year : filter_.last_year;
            int at = year == 0 ? 0 : std::clamp(year, kFirstYear, present_year_) - kFirstYear + 1;
            at = std::clamp(at + by, 0, present_year_ - kFirstYear + 1);
            year = at == 0 ? 0 : kFirstYear + at - 1;
            break;
        }
    }
    refresh();
}

void FilterPanel::draw(lv_obj_t* box) const {
    const int top = draw_frame(box, title());
    lv_obj_t* area = make_area(box, kPanelX, top, kPanelW, kContentHeight - top);
    list_.draw(area, 0, kPanelW);
}

void FilterPanel::on_action(const KeyEvent& event) {
    const std::string& a = event.action;
    if (list_.on_action(a)) {
        shell_->redraw();
        return;
    }
    if (a == "previous_value" || a == "next_value") {
        step(a == "next_value" ? 1 : -1);
        shell_->redraw();
        return;
    }
    if (a == "apply") {
        std::function<void(const SearchFilter&)> apply = on_apply_;
        const SearchFilter filter = filter_;
        shell_->close_panel();                 // this panel is gone after this line
        if (apply) apply(filter);
        return;
    }
    if (a == "back") shell_->close_panel();
}

Json FilterPanel::describe() const {
    Json panel = Panel::describe();
    panel.set("rows", list_.describe_rows());
    return panel;
}

}  // namespace spz::ui
