// SPDX-License-Identifier: GPL-3.0-or-later
// The `filter` panel of Search (SPEC-164, part `filters`): four lines, chip model, clock, first year and
// last year, each changed with Left and Right. Enter applies them; ESC leaves them as they were.
#pragma once

#include <functional>

#include "spz/search.h"
#include "ui/panel.h"

namespace spz::ui {

class FilterPanel : public Panel {
public:
    // `filter` is what is applied now; `present_year` the last year a year line offers. `on_apply` runs
    // after the panel has closed, with the filter as Enter left it.
    FilterPanel(Shell* shell, const SearchFilter& filter, int present_year,
                std::function<void(const SearchFilter&)> on_apply);
    std::string id() const override { return "filter"; }
    void draw(lv_obj_t* box) const override;
    void on_action(const KeyEvent& event) override;
    Json describe() const override;

    static constexpr int kFirstYear = 1982;     // SPEC-164: the first year a year line offers

private:
    std::string title() const override { return "Filter"; }
    void refresh();
    void step(int by);

    Shell* shell_;
    SearchFilter filter_;
    int present_year_;
    std::function<void(const SearchFilter&)> on_apply_;
    RowList list_;
};

// The words of SPEC-164's values, as the panel shows them.
std::string filter_model_text(SidModel model);
std::string filter_clock_text(C64Clock clock);
std::string filter_year_text(int year);

}  // namespace spz::ui
