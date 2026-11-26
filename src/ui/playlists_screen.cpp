// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/playlists_screen.h"

#include <memory>

#include "spz/keymap.h"
#include "spz/playlist.h"
#include "ui/draw.h"
#include "ui/lists_panels.h"
#include "ui/panel.h"

namespace spz::ui {

namespace {

// Highlights the playlist of that name in `list`, if `summaries` has it.
void highlight_name(RowList* list, const std::vector<PlaylistSummary>& summaries, const std::string& name) {
    for (std::size_t i = 0; i < summaries.size(); ++i) {
        if (summaries[i].name == name) list->highlight(i);
    }
}

}  // namespace

PlaylistsScreen::PlaylistsScreen(Shell* shell) : Screen(shell) {}

bool PlaylistsScreen::ready() { return shell()->services().playlists != nullptr; }

void PlaylistsScreen::refresh() {
    const Playlists* playlists = shell()->services().playlists;
    summaries_ = playlists ? playlists->list() : std::vector<PlaylistSummary>();
    std::vector<Row> rows;
    for (const PlaylistSummary& summary : summaries_) {
        Row row;
        row.text = summary.name;
        row.kind = "folder";
        row.value = std::to_string(summary.entries);
        row.has_value = true;
        rows.push_back(row);
    }
    list_.set_rows(std::move(rows));
}

void PlaylistsScreen::shown() { refresh(); }

const PlaylistSummary* PlaylistsScreen::highlighted() const {
    if (summaries_.empty() || list_.highlighted() >= summaries_.size()) return nullptr;
    return &summaries_[list_.highlighted()];
}

std::array<std::string, 5> PlaylistsScreen::actions() const {
    std::array<std::string, 5> labels = action_labels(place());
    // SPEC-171: Favourites cannot be renamed or deleted, and those two keys have no label on it.
    const PlaylistSummary* chosen = highlighted();
    if (chosen == nullptr || chosen->favourites) {
        labels[2].clear();
        labels[3].clear();
    }
    return labels;
}

void PlaylistsScreen::draw(lv_obj_t* content) {
    refresh();
    list_.draw(content, 0);
}

void PlaylistsScreen::rename() {
    const PlaylistSummary* chosen = highlighted();
    if (chosen == nullptr || chosen->favourites) return;
    Shell* frame = shell();
    const std::string old_name = chosen->name;
    frame->open_panel(std::make_unique<PromptPanel>(frame, "Rename", old_name, kPlaylistNameLimit,
                                                    [frame, this, old_name](const std::string& text) {
        Playlists* playlists = frame->services().playlists;
        if (playlists == nullptr) return true;
        if (text == old_name) return true;
        const ListsResult result = playlists->rename(old_name, text);
        if (!result.ok) {
            frame->notice(result.message.empty() ? "MSG_INVALID_VALUE" : result.message, result.about);
            return false;
        }
        // The playlists screen is under the prompt while it is open: the highlight follows the playlist.
        refresh();
        highlight_name(&list_, summaries_, text);
        frame->redraw();
        return true;
    }));
}

void PlaylistsScreen::remove() {
    const PlaylistSummary* chosen = highlighted();
    if (chosen == nullptr || chosen->favourites) return;
    Shell* frame = shell();
    const std::string name = chosen->name;
    frame->open_panel(std::make_unique<ConfirmPanel>(frame, "Delete the playlist \"" + name + "\"?", [frame, name] {
        Playlists* playlists = frame->services().playlists;
        if (playlists == nullptr) return;
        const ListsResult result = playlists->remove(name);
        if (!result.ok && !result.message.empty()) frame->notice(result.message, result.about);
        frame->redraw();
    }));
}

bool PlaylistsScreen::on_action(const KeyEvent& event) {
    const std::string& a = event.action;
    if (list_.on_action(a)) {
        shell()->redraw();
        return true;
    }
    if (a == "open_playlist") {
        const PlaylistSummary* chosen = highlighted();
        if (chosen == nullptr) return true;
        shell()->services().open_playlist = chosen->name;
        Shell* frame = shell();
        frame->open("playlist");
        return true;
    }
    if (a == "new_playlist") {
        open_new_playlist(shell(), [this](const std::string& made) {
            refresh();
            highlight_name(&list_, summaries_, made);
        });
        return true;
    }
    if (a == "rename_playlist") {
        rename();
        return true;
    }
    if (a == "delete_playlist") {
        remove();
        return true;
    }
    if (a == "open_import") {
        open_import(shell());
        return true;
    }
    return false;
}

void PlaylistsScreen::describe(Json* description) const {
    description->set("rows", list_.describe_rows());
    description->set("list", list_.describe_list());
}

}  // namespace spz::ui
