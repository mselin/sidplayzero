// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/playlist_screen.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "spz/job.h"
#include "spz/keymap.h"
#include "spz/playlist.h"
#include "spz/playlist_export.h"
#include "spz/playlist_fetch.h"
#include "ui/draw.h"
#include "ui/panel.h"

namespace spz::ui {

PlaylistScreen::PlaylistScreen(Shell* shell) : Screen(shell), name_(shell->services().open_playlist) {}

PlaylistScreen::~PlaylistScreen() { *alive_ = false; }

bool PlaylistScreen::ready() {
    const Playlists* playlists = shell()->services().playlists;
    return playlists != nullptr && playlists->find(name_) != nullptr;
}

void PlaylistScreen::refresh() {
    const Services& s = shell()->services();
    changes_seen_ = s.lists_changes;
    std::vector<Row> rows;
    if (s.playlists != nullptr) {
        for (const PlaylistRow& entry : s.playlists->rows(name_)) {
            Row row;
            // SPEC-172: the title, or the file's name; the subtune number if the entry has one; the mark.
            row.text = entry.text;
            if (entry.subtune > 0) row.text += " (" + std::to_string(entry.subtune) + ")";
            row.kind = "entry";
            if (entry.found == Found::Absent) row.marks.push_back("absent");
            else if (entry.found == Found::Missing) row.marks.push_back("missing");
            rows.push_back(row);
        }
    }
    list_.set_rows(std::move(rows));
}

void PlaylistScreen::shown() {
    refresh();
    shell()->redraw();
}

void PlaylistScreen::tick() {
    if (shell()->services().lists_changes == changes_seen_) return;
    refresh();
    shell()->redraw();
}

std::array<std::string, 5> PlaylistScreen::actions() const {
    std::array<std::string, 5> labels = action_labels(place());
    if (list_.size() == 0) {
        // Nothing to move or take out.
        labels[1].clear();
        labels[2].clear();
        labels[3].clear();
    }
    return labels;
}

void PlaylistScreen::draw(lv_obj_t* content) {
    if (list_.size() == 0) {
        make_label(content, "note", "(empty)", 4, 0, kWidth - 8, kRowHeight, LabelStyle{Align::Left, nullptr, true});
        return;
    }
    list_.draw(content, 0);
}

void PlaylistScreen::move(int by) {
    Playlists* playlists = shell()->services().playlists;
    if (playlists == nullptr || list_.size() == 0) return;
    const std::size_t at = list_.highlighted();
    const ListsResult result = playlists->move_entry(name_, at, by);
    if (!result.ok) {
        if (!result.message.empty()) shell()->notice(result.message, result.about);
        return;
    }
    // The highlight goes with the entry; the rows change places without being resolved again.
    std::vector<Row> rows = list_.rows();
    const bool moves = by < 0 ? at > 0 : at + 1 < rows.size();
    if (moves) {
        const std::size_t other = by < 0 ? at - 1 : at + 1;
        std::swap(rows[at], rows[other]);
        list_.set_rows(std::move(rows));
        list_.highlight(other);
    }
    shell()->redraw();
}

void PlaylistScreen::remove_entry() {
    Playlists* playlists = shell()->services().playlists;
    if (playlists == nullptr || list_.size() == 0) return;
    const ListsResult result = playlists->remove_entry(name_, list_.highlighted());
    if (!result.message.empty()) shell()->notice(result.message, result.about);
    refresh();
    shell()->redraw();
}

void PlaylistScreen::play() {
    const Services& s = shell()->services();
    if (s.playlists == nullptr || !s.play || list_.size() == 0) return;
    // SPEC-062, SPEC-131: the queue is the playlist's entries; the state file records the playlist.
    const std::vector<Entry> queue = s.playlists->queue(name_);
    if (queue.empty()) return;
    const std::size_t chosen = std::min(list_.highlighted(), queue.size() - 1);
    const std::string name = name_;
    s.play(queue, chosen, "playlist", name);
}

void PlaylistScreen::open_more() {
    const Services& s = shell()->services();
    std::vector<Row> rows;
    std::vector<std::string> what;
    // SPEC-140: while a job runs, nothing that would start another is offered.
    if (s.fetch_missing != nullptr && (s.jobs == nullptr || !s.jobs->running()) && !s.fetch_missing->state().running) {
        Row row;
        row.text = "Fetch missing";
        row.kind = "action";
        rows.push_back(row);
        what.push_back("fetch");
    }
    if (part_built("export")) {
        Row row;
        row.text = "Export";
        row.kind = "action";
        rows.push_back(row);
        what.push_back("export");
    }
    Shell* frame = shell();
    const std::string name = name_;
    std::weak_ptr<bool> alive = alive_;
    frame->open_panel(std::make_unique<ListPanel>(frame, "more-playlist", "More", rows,
                                                  [this, frame, what, name, alive](std::size_t i) {
        if (i >= what.size()) return;
        const Services& now = frame->services();
        if (what[i] == "fetch") {
            // SPEC-174: the playlist's absent entries, one after another, once the notice gate has passed.
            if (now.fetch_missing == nullptr || now.playlists == nullptr) return;
            now.fetch_missing->start(now.playlists->queue(name));
        } else if (what[i] == "export") {
            if (alive.expired() || !*alive.lock()) return;
            export_playlist();
        }
    }));
}

void PlaylistScreen::export_playlist() {
    // SPEC-179: written by the quick worker, from a copy of the playlist as it is now.
    const Services& s = shell()->services();
    if (s.playlists == nullptr || s.files == nullptr) return;
    const Playlist* playlist = s.playlists->find(name_);
    if (playlist == nullptr) return;
    const Playlist copy = *playlist;
    Files* files = s.files;
    const std::string path = s.export_folder + "/" + playlist_file_name(name_);
    Shell* frame = shell();
    frame->quick()->post([frame, files, path, copy] {
        const FileStatus status = lists_files::write_safely(files, path, exported_playlist(copy));
        frame->ui()->post([frame, status, path] {
            if (status == FileStatus::Ok) frame->notice("MSG_EXPORTED", {{"path", path}});
            else if (status == FileStatus::NoSpace) frame->notice("MSG_CARD_FULL");
        });
    });
}

bool PlaylistScreen::on_action(const KeyEvent& event) {
    const std::string& a = event.action;
    if (list_.on_action(a)) {
        shell()->redraw();
        return true;
    }
    if (a == "play_entry") {
        play();                          // the now-playing screen opens over this one
        return true;
    }
    if (a == "move_up") move(-1);
    else if (a == "move_down") move(1);
    else if (a == "remove_entry") remove_entry();
    else if (a == "open_more_playlist") open_more();
    else return false;
    return true;
}

void PlaylistScreen::describe(Json* description) const {
    description->set("rows", list_.describe_rows());
    description->set("list", list_.describe_list());
}

}  // namespace spz::ui
