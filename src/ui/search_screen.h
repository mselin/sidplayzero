// SPDX-License-Identifier: GPL-3.0-or-later
// The Search screen (SPEC-100): a text field above a result list. It is two places of the key map,
// `search-field` while the field is in use and `search-results` while the list is. A search (SPEC-162) runs
// on the quick worker over the index of ui::Services; the top line shows the number of results, or "No
// results", "Indexing n %" while the index is being built (SPEC-161), and that a filter is set (SPEC-164).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "spz/search.h"
#include "spz/text_field.h"
#include "ui/rows.h"
#include "ui/screen.h"

namespace spz::ui {

class SearchScreen : public Screen {
public:
    explicit SearchScreen(Shell* shell);
    ~SearchScreen() override;
    std::string name() const override { return "search"; }
    std::string place() const override { return in_field_ ? "search-field" : "search-results"; }
    bool ready() override;
    std::string title() const override;
    std::string position() const override;
    std::array<std::string, 5> actions() const override;
    bool typing() const override { return in_field_; }
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;
    void tick() override;

private:
    bool field_action(const KeyEvent& event);
    bool results_action(const KeyEvent& event);
    // Runs `query` with the filter of the moment, on the quick worker; the results replace the list.
    void run(const std::string& query);
    void open_filter();
    void browse_here();
    // The rows of the results, with the marks of the moment (absent, fetching, favourite, playing).
    RowList rows() const;
    bool highlighted_entry(Entry* out) const;
    // Whether the index is being built, and how far it is: what "Indexing n %" shows.
    bool indexing(int* percent) const;
    void draw_field(lv_obj_t* content) const;

    TextField field_;
    bool in_field_ = true;
    SearchFilter filter_;
    std::string query_;                 // the query last run
    bool searched_ = false;             // a search has been run, and its results are those below
    bool searching_ = false;            // one runs on the quick worker now
    std::vector<SearchHit> hits_;
    int total_ = 0;                     // the true number of matches (SPEC-162)
    RowList list_;                      // the results' rows, and the highlight among them
    std::uint64_t request_ = 0;         // the latest search asked for; older answers are dropped
    bool shown_indexing_ = false;       // what the top line last showed of the index build
    int shown_percent_ = -1;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace spz::ui
