// SPDX-License-Identifier: GPL-3.0-or-later
// The Playlists screen (SPEC-171, SPEC-172, part `playlists`): the playlists with their numbers of entries,
// Favourites first. Enter opens one in the Playlist screen; the keys are "Back", "New", "Rename", "Delete"
// and "Import", and Favourites has no label for Rename and Delete. The playlists are the composition's
// (ui::Services::playlists).
#pragma once

#include <string>
#include <vector>

#include "spz/playlists.h"
#include "ui/rows.h"
#include "ui/screen.h"

namespace spz::ui {

class PlaylistsScreen : public Screen {
public:
    explicit PlaylistsScreen(Shell* shell);
    std::string name() const override { return "playlists"; }
    // It needs the playlists, which are read on the quick worker at start.
    bool ready() override;
    std::string title() const override { return "Playlists"; }
    std::string position() const override { return list_.position(); }
    std::array<std::string, 5> actions() const override;
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;
    void shown() override;

private:
    void refresh();
    // The highlighted playlist, or null for none.
    const PlaylistSummary* highlighted() const;
    void rename();
    void remove();

    std::vector<PlaylistSummary> summaries_;
    RowList list_;
};

}  // namespace spz::ui
