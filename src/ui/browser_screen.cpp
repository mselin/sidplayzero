// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/browser_screen.h"

#include <algorithm>
#include <vector>

#include "spz/browser_model.h"
#include "spz/collection.h"
#include "spz/favourites.h"
#include "spz/fetcher.h"
#include "spz/keymap.h"
#include "spz/player.h"
#include "spz/settings_file.h"
#include "ui/draw.h"

namespace spz::ui {

namespace {

const Millis kDetailAfterMs = 150;     // SPEC-046: the highlight rests this long before the line is read

std::string time_of(Millis ms) {
    const Millis seconds = ms / 1000;  // cut down, never rounded (FIND-024)
    char text[32];
    std::snprintf(text, sizeof text, "%lld:%02lld", static_cast<long long>(seconds / 60),
                  static_cast<long long>(seconds % 60));
    return text;
}

bool playing_now(const PlayerStatus& status) {
    return status.has_entry && status.state != PlayerState::Idle && status.state != PlayerState::Failed;
}

}  // namespace

BrowserScreen::BrowserScreen(Shell* shell) : Screen(shell) {}

BrowserScreen::~BrowserScreen() {
    lifetime_.end();
    if (detail_timer_ != 0) shell()->clock()->cancel(detail_timer_);
}

BrowserModel* BrowserScreen::model() const { return shell()->services().browser; }

bool BrowserScreen::ready() { return model() != nullptr; }

int BrowserScreen::rows_shown() const { return part_built("detail") ? kRowsShown - 1 : kRowsShown; }

std::string BrowserScreen::title() const {
    const BrowserModel* m = model();
    return m ? m->folder() : "/";
}

std::string BrowserScreen::position() const {
    const BrowserModel* m = model();
    if (!m) return "";
    if (m->count() > 0) return m->position();
    return m->note() == ListNote::Empty ? "0/0" : "";
}

std::array<std::string, 5> BrowserScreen::actions() const {
    std::array<std::string, 5> labels = action_labels(place());
    // SPEC-095: "Playing" has no label while the player has no current entry; "Fav" and "Add" none while a
    // folder, or a tune whose path is not a collection path (SPEC-041), is highlighted.
    const Player* player = shell()->services().player;
    if (!player || !player->status().has_entry) labels[0].clear();
    Entry entry;
    if (!highlighted_tune(&entry) || entry.path.compare(0, 7, "file://") == 0) {
        labels[3].clear();
        labels[4].clear();
    }
    return labels;
}

bool BrowserScreen::highlighted_tune(Entry* out) const {
    const BrowserModel* m = model();
    if (!m || m->highlight() < 0) return false;
    const FolderEntry& row = m->row(m->highlight());
    if (row.is_folder) return false;
    const Collection* tree = shell()->services().tree;
    out->path = !row.path.empty() ? row.path : "file://" + (tree ? tree->file_path(m->folder(), row.name) : row.name);
    out->subtune = 0;
    return true;
}

void BrowserScreen::keep_in_view() {
    const BrowserModel* m = model();
    const int count = m ? m->count() : 0;
    const std::size_t shown = static_cast<std::size_t>(rows_shown());
    const std::size_t highlight = m && m->highlight() > 0 ? static_cast<std::size_t>(m->highlight()) : 0;
    if (highlight < first_) first_ = highlight;
    if (highlight >= first_ + shown) first_ = highlight + 1 - shown;
    const std::size_t total = static_cast<std::size_t>(count);
    const std::size_t most = total > shown ? total - shown : 0;
    if (first_ > most) first_ = most;
}

RowList BrowserScreen::visible() const {
    RowList list;
    list.set_shown(rows_shown());
    const BrowserModel* m = model();
    if (!m) return list;
    const Services& s = shell()->services();
    const PlayerStatus status = s.player ? s.player->status() : PlayerStatus();
    const std::size_t total = static_cast<std::size_t>(m->count());
    const std::size_t end = std::min(total, first_ + static_cast<std::size_t>(rows_shown()));
    std::vector<Row> rows;
    for (std::size_t i = first_; i < end; ++i) {
        const FolderEntry& entry = m->row(static_cast<int>(i));
        Row row;
        row.text = entry.shown;
        row.kind = entry.is_folder ? "folder" : "tune";
        if (!entry.is_folder) {
            const bool fetching = s.fetcher && !entry.path.empty() && s.fetcher->fetching(entry.path);
            if (fetching) row.marks.push_back("fetching");
            else if (!entry.present) row.marks.push_back("absent");
            if (s.favourites && !entry.path.empty() && s.favourites->has_tune(entry.path)) {
                row.marks.push_back("favourite");
            }
            if (playing_now(status) && !entry.path.empty() && status.entry.path == entry.path) {
                row.marks.push_back("playing");
            }
        }
        rows.push_back(row);
    }
    list.set_rows(std::move(rows));
    if (m->highlight() >= 0) list.highlight(static_cast<std::size_t>(m->highlight()) - first_);
    return list;
}

void BrowserScreen::draw(lv_obj_t* content) {
    BrowserModel* m = model();
    if (!m) return;
    keep_in_view();
    watch_detail();
    const int list_height = rows_shown() * kRowHeight;
    if (m->note() == ListNote::Reading) {
        make_label(content, "note", "Reading\xE2\x80\xA6", 4, 0, kWidth - 8, kRowHeight, LabelStyle{Align::Left, nullptr, true});
    } else if (m->note() == ListNote::Empty) {
        make_label(content, "note", "(empty)", 4, 0, kWidth - 8, kRowHeight, LabelStyle{Align::Left, nullptr, true});
    } else {
        visible().draw(content, 0);
    }
    if (part_built("detail") && !detail_.empty()) {
        make_label(content, "detail", detail_, 4, list_height, kWidth - 8, kRowHeight, LabelStyle{Align::Left, nullptr, true});
    }
}

void BrowserScreen::watch_detail() {
    if (!part_built("detail")) return;
    const BrowserModel* m = model();
    std::string key;
    if (m && m->highlight() >= 0 && m->note() != ListNote::Reading) {
        key = m->folder() + "\n" + m->row(m->highlight()).name;
    }
    if (key == detail_key_) return;
    // The highlight has moved: the line is empty until it has rested for 150 ms.
    detail_key_ = key;
    detail_.clear();
    if (detail_timer_ != 0) shell()->clock()->cancel(detail_timer_);
    detail_timer_ = 0;
    if (key.empty()) return;
    const Lifetime::Token alive = lifetime_.token();
    detail_timer_ = shell()->clock()->call_after(kDetailAfterMs, [this, alive, key] {
        if (!alive.alive()) return;
        detail_timer_ = 0;
        ask_detail(key);
    });
}

void BrowserScreen::ask_detail(const std::string& key) {
    const BrowserModel* m = model();
    const Collection* tree = shell()->services().tree;
    if (!m || !tree || m->highlight() < 0) return;
    const FolderEntry entry = m->row(m->highlight());
    if (entry.is_folder) return;
    const std::string folder = m->folder();
    const SettingsFile* settings = shell()->services().settings;
    const Millis default_ms = settings ? static_cast<Millis>(settings->settings().default_length_s) * 1000 : 0;
    Shell* frame = shell();
    const Lifetime::Token alive = lifetime_.token();
    // The quick worker reads the file: the header, and the MD5 for the length (SPEC-046).
    frame->quick()->post([frame, alive, tree, folder, entry, default_ms, key, this] {
        const EntryDetail detail = tree->detail(folder, entry, default_ms);
        std::string text;
        if (!detail.is_folder) {
            if (detail.present) {
                text = detail.title + " \xC2\xB7 " + detail.author + " \xC2\xB7 " + time_of(detail.length_ms);
            } else {
                text = "not on device \xC2\xB7 " + std::to_string(detail.subtunes) +
                       (detail.subtunes == 1 ? " subtune" : " subtunes") + " \xC2\xB7 " + time_of(detail.length_ms);
            }
        }
        frame->ui()->post([frame, alive, key, text, this] {
            if (!alive.alive() || key != detail_key_) return;
            detail_ = text;
            frame->redraw();
        });
    });
}

void BrowserScreen::play_highlighted() {
    const Services& s = shell()->services();
    BrowserModel* m = model();
    if (!m || !s.play || m->highlight() < 0) return;
    const std::string chosen = m->row(m->highlight()).name;
    const std::vector<FolderEntry> tunes = m->tunes();
    std::vector<Entry> queue;
    std::size_t at = 0;
    for (const FolderEntry& tune : tunes) {
        if (tune.name == chosen) at = queue.size();
        Entry entry;
        entry.path = !tune.path.empty() ? tune.path
                                        : "file://" + (s.tree ? s.tree->file_path(m->folder(), tune.name) : tune.name);
        queue.push_back(entry);
    }
    // SPEC-062: the queue is the folder's tunes, in the order shown; SPEC-095: the now-playing screen opens.
    s.play(queue, at, "folder", m->folder());
}

bool BrowserScreen::on_action(const KeyEvent& event) {
    BrowserModel* m = model();
    if (!m) return false;
    const std::string& a = event.action;
    if (a == "up") m->up();
    else if (a == "down") m->down();
    else if (a == "page_up") m->page_up();
    else if (a == "page_down") m->page_down();
    else if (a == "first") m->first();
    else if (a == "last") m->last();
    else if (a == "open_or_play") {
        if (m->open() == BrowserModel::Opened::Tune) play_highlighted();
    } else if (a == "parent" || a == "back") {
        m->parent();
    } else if (a == "toggle_favourite") {
        Entry entry;
        Favourites* favourites = shell()->services().favourites;
        if (!favourites || !highlighted_tune(&entry) || entry.path.compare(0, 7, "file://") == 0) return true;
        const ListsResult result = favourites->toggle(entry);
        if (!result.message.empty()) shell()->notice(result.message, result.about);
    } else if (a == "add_to_playlist") {
        Entry entry;
        const Services& s = shell()->services();
        if (s.add_to_playlist && highlighted_tune(&entry) && entry.path.compare(0, 7, "file://") != 0) {
            s.add_to_playlist(entry);
        }
    } else {
        return false;
    }
    shell()->redraw();
    return true;
}

void BrowserScreen::describe(Json* description) const {
    const BrowserModel* m = model();
    if (!m) return;
    const RowList list = visible();
    description->set("rows", list.describe_rows());
    Json shown = list.describe_list();
    Json info = Json::object();
    info.set("total", static_cast<std::int64_t>(m->count()));
    info.set("first", static_cast<std::int64_t>(first_));
    const Json* objects = shown.get("objects");
    info.set("objects", objects ? *objects : Json(static_cast<std::int64_t>(0)));
    description->set("list", info);
    if (part_built("detail") && !detail_.empty()) description->set("detail", detail_);
}

}  // namespace spz::ui
