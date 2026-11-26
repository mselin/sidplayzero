// SPDX-License-Identifier: GPL-3.0-or-later
// The Playlist screen (SPEC-172, SPEC-173, SPEC-174, SPEC-179, part `playlists`): the entries of the
// playlist that Enter in Playlists chose (ui::Services::open_playlist), each with its title or file name,
// its subtune number if it has one, and the mark of an absent or missing entry. Enter plays from the entry;
// the keys are "Back", "Up", "Down", "Remove" and "More", whose panel offers "Fetch missing" and "Export".
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "spz/playlists.h"
#include "ui/rows.h"
#include "ui/screen.h"

namespace spz::ui {

class PlaylistScreen : public Screen {
public:
    explicit PlaylistScreen(Shell* shell);
    ~PlaylistScreen() override;
    std::string name() const override { return "playlist"; }
    // It needs the playlists, and the playlist chosen.
    bool ready() override;
    std::string title() const override { return name_; }
    std::string position() const override { return list_.position(); }
    std::array<std::string, 5> actions() const override;
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;
    void shown() override;
    void tick() override;

private:
    // The rows again, each entry resolved (SPEC-173). Asked when the screen shows and after a change, not at
    // every drawing: resolving looks at a file for each entry.
    void refresh();
    void move(int by);
    void remove_entry();
    void play();
    void open_more();
    void export_playlist();

    std::string name_;                 // the playlist shown
    int changes_seen_ = 0;             // Services::lists_changes when the rows were made
    RowList list_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace spz::ui
