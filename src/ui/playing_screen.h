// SPDX-License-Identifier: GPL-3.0-or-later
// The now-playing screen (SPEC-096): six lines that say what the player is doing, as its state table has
// them; the voice display of the part `bars` (SPEC-080); the voices and filter of the part `voices`
// (SPEC-074); its keys; and the `more-playing` panel (SPEC-114) with "Notes" from STIL (SPEC-097).
#pragma once

#include <array>
#include <string>
#include <vector>

#include "spz/player.h"
#include "ui/screen.h"

namespace spz::ui {

class PlayingScreen : public Screen {
public:
    explicit PlayingScreen(Shell* shell);
    std::string name() const override { return "playing"; }
    bool ready() override { return shell()->services().player != nullptr; }
    std::string title() const override { return "Now playing"; }
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;
    void tick() override;
    void shown() override;

    // The six lines of SPEC-096 for a status, as the screen draws them (without the scrolling).
    static std::array<std::string, 6> lines_for(const PlayerStatus& status, const Settings* settings,
                                                bool favourite);

private:
    struct Voice {
        int height = 0;
        bool lit = false;
    };
    std::array<std::string, 6> lines() const;
    bool read_voices();          // SPEC-080: the chosen chip's registers; true if a bar has changed
    std::string signature() const;
    void open_more();
    void show_notes();
    void step_mode();

    std::vector<Voice> voices_ = std::vector<Voice>(3);
    std::string drawn_;          // what was drawn last, to know when to draw again
};

}  // namespace spz::ui
