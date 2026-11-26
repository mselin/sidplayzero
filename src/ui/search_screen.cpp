// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/search_screen.h"

#include <algorithm>
#include <ctime>
#include <utility>

#include "spz/browser_model.h"
#include "spz/collection.h"
#include "spz/favourites.h"
#include "spz/fetcher.h"
#include "spz/index.h"
#include "spz/keymap.h"
#include "spz/player.h"
#include "ui/draw.h"
#include "ui/filter_panel.h"

namespace spz::ui {

namespace {

const int kResultRows = kRowsShown - 1;     // the field takes the first row of the content band
const char* const kDot = " \xC2\xB7 ";

bool playing_now(const PlayerStatus& status) {
    return status.has_entry && status.state != PlayerState::Idle && status.state != PlayerState::Failed;
}

// Splits UTF-8 text into its characters.
std::vector<std::string> characters(const std::string& text) {
    std::vector<std::string> chars;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        n = std::min(n, text.size() - i);
        chars.push_back(text.substr(i, n));
        i += n;
    }
    return chars;
}

std::string file_name(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

SearchScreen::SearchScreen(Shell* shell) : Screen(shell), field_(kSearchFieldLimit) {
    list_.set_shown(kResultRows);
}

SearchScreen::~SearchScreen() { *alive_ = false; }

bool SearchScreen::ready() { return shell()->services().index != nullptr; }

bool SearchScreen::indexing(int* percent) const {
    const Index* index = shell()->services().index;
    if (index == nullptr || index->complete()) return false;
    *percent = std::clamp(index->percent(), 0, 100);
    return true;
}

std::string SearchScreen::title() const {
    // SPEC-100: the number of results, or "No results"; "Indexing n %" while the index is built; and
    // SPEC-164: that a filter is set.
    std::string text = "Search";
    if (searched_ && !searching_) {
        text = total_ == 0 ? "No results" : std::to_string(total_) + (total_ == 1 ? " result" : " results");
    }
    int percent = 0;
    if (indexing(&percent)) text += kDot + std::string("Indexing ") + std::to_string(percent) + " %";
    if (filter_.is_set()) text += kDot + std::string("Filter set");
    return text;
}

std::string SearchScreen::position() const {
    if (!searched_ || searching_ || hits_.empty()) return "";
    return std::to_string(list_.highlighted() + 1) + "/" + std::to_string(total_);
}

std::array<std::string, 5> SearchScreen::actions() const {
    std::array<std::string, 5> labels = action_labels(place());
    if (in_field_) return labels;
    // "Folder", "Fav" and "Add" act on a result: with none, they have no label (SPEC-095's rule).
    if (hits_.empty() || searching_) {
        labels[1].clear();
        labels[3].clear();
        labels[4].clear();
    }
    if (!part_built("filters")) labels[2].clear();
    return labels;
}

RowList SearchScreen::rows() const {
    RowList list = list_;
    const Services& s = shell()->services();
    const PlayerStatus status = s.player ? s.player->status() : PlayerStatus();
    std::vector<Row> rows;
    rows.reserve(hits_.size());
    for (const SearchHit& hit : hits_) {
        Row row;
        row.text = shown_name(file_name(hit.path));
        row.kind = "tune";
        if (s.fetcher && s.fetcher->fetching(hit.path)) row.marks.push_back("fetching");
        else if (!hit.present) row.marks.push_back("absent");
        if (s.favourites && s.favourites->has_tune(hit.path)) row.marks.push_back("favourite");
        if (playing_now(status) && status.entry.path == hit.path) row.marks.push_back("playing");
        rows.push_back(std::move(row));
    }
    list.set_rows(std::move(rows));
    list.highlight(list_.highlighted());
    return list;
}

void SearchScreen::draw_field(lv_obj_t* content) const {
    const int x = 4, w = kWidth - 8, y = 1, h = kRowHeight - 2;
    make_fill(content, x - 2, y - 1, w + 4, h + 2, in_field_ ? colour_line() : colour_dim());
    make_fill(content, x - 1, y, w + 2, h, colour_background());
    // The part of the text that shows: as much as fits, with the cursor in it.
    const std::vector<std::string> chars = characters(field_.text());
    auto joined = [&](std::size_t from, std::size_t to) {
        std::string s;
        for (std::size_t i = from; i < to && i < chars.size(); ++i) s += chars[i];
        return s;
    };
    const std::size_t cursor = std::min(field_.cursor(), chars.size());
    std::size_t start = 0;
    while (start < cursor && text_width(joined(start, cursor), text_font()) > w - 4) ++start;
    std::size_t end = cursor;
    while (end < chars.size() && text_width(joined(start, end + 1), text_font()) <= w - 4) ++end;
    make_label(content, "field", joined(start, end), x, y, w, h);
    if (in_field_) {
        const int cursor_x = x + text_width(joined(start, cursor), text_font());
        make_fill(content, cursor_x, y + 3, 1, h - 6, colour_text());
    }
}

void SearchScreen::draw(lv_obj_t* content) {
    draw_field(content);
    if (searching_) {
        make_label(content, "note", "Searching\xE2\x80\xA6", 4, kRowHeight, kWidth - 8, kRowHeight,
                   LabelStyle{Align::Left, nullptr, true});
        return;
    }
    if (!searched_) return;
    lv_obj_t* area = make_area(content, 0, kRowHeight, kWidth, kResultRows * kRowHeight);
    rows().draw(area, 0);
}

void SearchScreen::tick() {
    int percent = 0;
    const bool now = indexing(&percent);
    if (now != shown_indexing_ || (now && percent != shown_percent_)) {
        shown_indexing_ = now;
        shown_percent_ = now ? percent : -1;
        shell()->redraw();
    }
}

bool SearchScreen::on_action(const KeyEvent& event) {
    const bool done = in_field_ ? field_action(event) : results_action(event);
    return done;
}

bool SearchScreen::field_action(const KeyEvent& event) {
    const std::string& a = event.action;
    if (a == "run_search") {
        // SPEC-100: with an empty field (a query with no word, SPEC-162) Enter does nothing.
        if (query_words(field_.text()).empty()) return true;
        in_field_ = false;
        run(field_.text());
        return true;
    }
    if (a == "to_results") {
        if (searched_) {
            in_field_ = false;
            shell()->redraw();
        }
        return true;
    }
    if (field_.apply(a, event.character)) {
        shell()->redraw();
        return true;
    }
    return false;
}

bool SearchScreen::results_action(const KeyEvent& event) {
    const std::string& a = event.action;
    const Services& s = shell()->services();
    if (a == "up" && list_.highlighted() == 0) {
        // SPEC-100: Up from the first result returns to the field.
        in_field_ = true;
    } else if (list_.on_action(a)) {
        // moved in the list
    } else if (a == "edit_query") {
        in_field_ = true;
    } else if (a == "play_result") {
        if (hits_.empty() || searching_ || !s.play) return true;
        // SPEC-062: the queue is the results, in the order shown, with no subtune numbers.
        std::vector<Entry> queue;
        for (const SearchHit& hit : hits_) {
            Entry entry;
            entry.path = hit.path;
            queue.push_back(entry);
        }
        s.play(queue, list_.highlighted(), "search", query_);
        return true;                 // the now-playing screen is on top now
    } else if (a == "browse_here") {
        browse_here();
        return true;                 // this screen may be closed now
    } else if (a == "open_filter") {
        open_filter();
        return true;
    } else if (a == "toggle_favourite") {
        Entry entry;
        if (!s.favourites || !highlighted_entry(&entry)) return true;
        const ListsResult result = s.favourites->toggle(entry);
        if (!result.message.empty()) shell()->notice(result.message, result.about);
    } else if (a == "add_to_playlist") {
        Entry entry;
        if (s.add_to_playlist && highlighted_entry(&entry)) s.add_to_playlist(entry);
        return true;
    } else {
        return false;
    }
    shell()->redraw();
    return true;
}

bool SearchScreen::highlighted_entry(Entry* out) const {
    if (hits_.empty() || searching_) return false;
    out->path = hits_[std::min(list_.highlighted(), hits_.size() - 1)].path;
    out->subtune = 0;
    return true;
}

void SearchScreen::browse_here() {
    // SPEC-100: "Folder" shows the result in the browser, at its folder, with the tune highlighted.
    Entry entry;
    BrowserModel* browser = shell()->services().browser;
    if (!browser || !highlighted_entry(&entry)) return;
    const std::size_t slash = entry.path.rfind('/');
    const std::string folder = slash == 0 || slash == std::string::npos ? "/" : entry.path.substr(0, slash);
    const std::string name = slash == std::string::npos ? entry.path : entry.path.substr(slash + 1);
    browser->show(folder, name);
    shell()->open("browser");        // closes this screen: nothing of `this` after this line
}

void SearchScreen::open_filter() {
    if (!part_built("filters")) return;
    const std::time_t seconds = static_cast<std::time_t>(shell()->clock()->wall_time_s());
    std::tm when;
    gmtime_r(&seconds, &when);
    std::weak_ptr<bool> alive = alive_;
    shell()->open_panel(std::unique_ptr<Panel>(new FilterPanel(
        shell(), filter_, when.tm_year + 1900, [this, alive](const SearchFilter& filter) {
            if (alive.expired() || !*alive.lock()) return;
            filter_ = filter;
            if (searched_) run(query_);
            else shell()->redraw();
        })));
}

void SearchScreen::run(const std::string& query) {
    Index* index = shell()->services().index;
    if (!index) return;
    query_ = query;
    searching_ = true;
    const std::uint64_t request = ++request_;
    const SearchFilter filter = filter_;
    Shell* frame = shell();
    std::weak_ptr<bool> alive = alive_;
    frame->quick()->post([this, frame, alive, index, query, filter, request] {
        // SPEC-162: one pass over the index on the quick worker, limited to 2 s.
        SearchResults results = search(*index, query, filter);
        frame->ui()->post([this, frame, alive, request, results] {
            if (alive.expired() || !*alive.lock() || request != request_) return;
            searching_ = false;
            searched_ = true;
            hits_ = results.hits;
            total_ = results.total;
            std::vector<Row> rows(hits_.size());
            list_.set_rows(std::move(rows));
            list_.highlight(0);
            frame->redraw();
        });
    });
    shell()->redraw();
}

void SearchScreen::describe(Json* description) const {
    Json one = Json::object();
    one.set("text", field_.text());
    one.set("cursor", static_cast<std::int64_t>(field_.cursor()));
    Json fields = Json::object();
    fields.set("search", one);
    description->set("fields", fields);
    if (!searched_ || searching_) return;
    const RowList list = rows();
    description->set("rows", list.describe_rows());
    Json shown = list.describe_list();
    Json info = Json::object();
    info.set("total", static_cast<std::int64_t>(total_));
    const Json* first = shown.get("first");
    info.set("first", first ? *first : Json(static_cast<std::int64_t>(0)));
    const Json* objects = shown.get("objects");
    info.set("objects", objects ? *objects : Json(static_cast<std::int64_t>(0)));
    description->set("list", info);
}

}  // namespace spz::ui
