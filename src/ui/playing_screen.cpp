// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/playing_screen.h"

#include <cstdio>
#include <memory>

#include "spz/browser_model.h"
#include "spz/favourites.h"
#include "spz/keymap.h"
#include "spz/messages.h"
#include "spz/settings_file.h"
#include "spz/sound_backend.h"
#include "spz/stil.h"
#include "ui/draw.h"
#include "ui/panel.h"

namespace spz::ui {

namespace {

const int kLineX = 4;
const int kLineW = kWidth - 8;
const int kBarsW = 40;                    // the voice display, right of lines 4 and 5 (SPEC-080)
const int kBarW = 8;
const int kBarsTop = 2 * kRowHeight + 4;  // the bars stand on the bottom of line 5
const int kBarsBottom = 5 * kRowHeight - 2;

std::string time_of(Millis ms) {
    if (ms < 0) ms = 0;
    const Millis seconds = ms / 1000;     // cut down, never rounded (FIND-024)
    char text[32];
    std::snprintf(text, sizeof text, "%lld:%02lld", static_cast<long long>(seconds / 60),
                  static_cast<long long>(seconds % 60));
    return text;
}

std::string file_name(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string folder_of(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return path.substr(0, slash);
}

std::string model_text(const PlayerStatus& s) {
    return std::string(s.model == SidModel::Mos8580 ? "8580" : "6581") + (s.model_forced ? " (forced)" : "");
}

std::string clock_text(const PlayerStatus& s) {
    return std::string(s.clock == C64Clock::Ntsc ? "NTSC" : "PAL") + (s.clock_forced ? " (forced)" : "");
}

// SPEC-065: the values of `when_song_ends`, in words, for MSG_MODE.
std::string mode_words(WhenSongEnds mode) {
    switch (mode) {
        case WhenSongEnds::Subtunes: return "next subtune";
        case WhenSongEnds::NextTune: return "next tune";
        case WhenSongEnds::Repeat: return "the same again";
        case WhenSongEnds::Stop: return "stop";
    }
    return "";
}

}  // namespace

PlayingScreen::PlayingScreen(Shell* shell) : Screen(shell) {}

std::array<std::string, 6> PlayingScreen::lines_for(const PlayerStatus& s, const Settings* settings, bool favourite) {
    std::array<std::string, 6> out;
    if (!s.has_entry) {
        out[0] = "Nothing is playing";
        return out;
    }
    const std::string name = file_name(s.entry.path);
    const bool live = s.state == PlayerState::Playing || s.state == PlayerState::Paused;
    const std::string total = s.length_ms > 0 ? time_of(s.length_ms) : "--:--";
    switch (s.state) {
        case PlayerState::Idle:
            if (s.tune_known) {
                out[0] = s.info.title;
                out[1] = s.info.author;
                out[2] = s.info.released;
            } else {
                out[0] = name;
            }
            if (s.subtune > 0) out[3] = std::to_string(s.subtune) + " of " + std::to_string(s.subtunes) + "  0:00 / " + total;
            if (s.tune_known) {
                out[4] = model_text(s) + " " + clock_text(s) + "  " + std::to_string(s.chips) + " SID  " +
                         (s.engine == EngineKind::Accurate ? "accurate" : "light");
            }
            out[5] = "Stopped";
            break;
        case PlayerState::Waiting:
        case PlayerState::Fetching:
            out[0] = name;
            if (s.subtunes > 0) {
                out[3] = std::to_string(s.subtunes) + (s.subtunes == 1 ? " subtune  " : " subtunes  ") +
                         (s.length_ms > 0 ? time_of(s.length_ms) : "--:--");
            }
            out[5] = "Fetching\xE2\x80\xA6";
            break;
        case PlayerState::Loading:
            out[0] = name;
            out[5] = "Loading\xE2\x80\xA6";
            break;
        case PlayerState::Playing:
        case PlayerState::Paused:
            out[0] = s.info.title;
            out[1] = s.info.author;
            out[2] = s.info.released;
            out[3] = std::to_string(s.subtune) + " of " + std::to_string(s.subtunes) + "  " + time_of(s.elapsed_ms) +
                     " / " + total;
            out[4] = model_text(s) + " " + clock_text(s) + "  " + std::to_string(s.chips) + " SID  " +
                     (s.engine == EngineKind::Accurate ? "accurate" : "light");
            out[5] = s.state == PlayerState::Playing ? "Playing" : "Paused";
            break;
        case PlayerState::Failed:
            out[0] = name;
            out[5] = message_words(s.message);
            break;
    }
    if (live || (s.state == PlayerState::Idle && s.tune_known)) {
        // Line 6's marks: the chip, the voices and the filter (part `voices`), a star, repeat-all, shuffle.
        if (s.chips > 1) out[5] += "  Chip " + std::to_string(s.chip);
        if (part_built("voices")) {
            out[5] += "  Voices";
            for (int voice = 0; voice < 3; ++voice) {
                out[5] += " " + std::to_string(voice + 1);
                if (s.muted[s.chip - 1][voice]) out[5] += "\xCC\xB6";    // struck through (U+0336)
            }
            if (!s.filter_on[s.chip - 1]) out[5] += "  Filter off";
        }
    }
    if (live || s.state == PlayerState::Idle) {
        if (favourite) out[5] += "  *";
        if (settings && part_built("modes") && settings->repeat_all) out[5] += "  Repeat all";
        if (settings && part_built("shuffle") && settings->shuffle) out[5] += "  Shuffle";
    }
    return out;
}

std::array<std::string, 6> PlayingScreen::lines() const {
    const Services& s = shell()->services();
    const PlayerStatus status = s.player ? s.player->status() : PlayerStatus();
    bool favourite = false;
    if (s.favourites && status.has_entry) {
        favourite = s.favourites->has_playing(status.entry.path, status.subtune, status.info.default_song);
    }
    return lines_for(status, s.settings ? &s.settings->settings() : nullptr, favourite);
}

bool PlayingScreen::read_voices() {
    // SPEC-080: the chosen chip's registers, read only while a tune plays, so that the bars stand still
    // while it is paused.
    const Services& s = shell()->services();
    if (!s.player || !s.sound) return false;
    const PlayerStatus status = s.player->status();
    if (status.state != PlayerState::Playing || !s.sound->can().registers) return false;
    std::uint8_t registers[Engine::kRegisters] = {};
    if (!s.sound->registers(status.chip - 1, registers)) return false;
    bool changed = false;
    for (int voice = 0; voice < 3; ++voice) {
        Voice now;
        now.height = registers[7 * voice + 1] / 8;
        now.lit = (registers[7 * voice + 4] & 1) != 0;
        Voice& was = voices_[static_cast<std::size_t>(voice)];
        if (was.height != now.height || was.lit != now.lit) changed = true;
        was = now;
    }
    return changed;
}

std::string PlayingScreen::signature() const {
    // What the screen shows: its lines, and where a long line has scrolled to.
    std::string text;
    const std::array<std::string, 6> all = lines();
    for (std::size_t i = 0; i < all.size(); ++i) {
        text += all[i] + "\n";
        if (i < 3) {
            const lv_font_t* font = i == 0 ? title_font() : text_font();
            const int overflow = text_width(all[i], font) - kLineW;
            if (overflow > 0) text += std::to_string(scroll_offset(overflow, shell()->clock()->now_ms())) + "\n";
        }
    }
    return text;
}

void PlayingScreen::shown() {
    read_voices();
    shell()->redraw();
}

void PlayingScreen::tick() {
    const bool voices = read_voices();
    if (voices || signature() != drawn_) shell()->redraw();
}

void PlayingScreen::draw(lv_obj_t* content) {
    drawn_ = signature();
    const std::array<std::string, 6> all = lines();
    const Services& s = shell()->services();
    const PlayerStatus status = s.player ? s.player->status() : PlayerStatus();
    const bool bars = part_built("bars");
    for (std::size_t i = 0; i < all.size(); ++i) {
        const int y = static_cast<int>(i) * kRowHeight;
        LabelStyle style;
        int w = kLineW;
        if (i == 0) style.font = title_font();
        if (i == 3 || i == 4) w = bars ? kLineW - kBarsW : kLineW;
        if (i < 3) {
            // SPEC-098: lines 1 to 3 scroll sideways when they are too long.
            const int overflow = text_width(all[i], style.font ? style.font : text_font()) - w;
            style.scroll = overflow > 0 ? scroll_offset(overflow, shell()->clock()->now_ms()) : -1;
        }
        if (i == 5) style.dim = status.state == PlayerState::Failed;
        make_label(content, "line" + std::to_string(i + 1), all[i], kLineX, y, w, kRowHeight, style);
    }
    // Line 4's progress bar, under its text.
    if ((status.state == PlayerState::Playing || status.state == PlayerState::Paused) && status.length_ms > 0) {
        const int bar_w = (bars ? kLineW - kBarsW : kLineW) - 4;
        const Millis done = std::min(status.elapsed_ms, status.length_ms);
        const int filled = static_cast<int>(static_cast<long long>(bar_w) * done / status.length_ms);
        make_fill(content, kLineX, 4 * kRowHeight - 2, bar_w, 1, colour_dim());
        if (filled > 0) make_fill(content, kLineX, 4 * kRowHeight - 3, filled, 2, colour_highlight());
    }
    if (bars) {
        for (int voice = 0; voice < 3; ++voice) {
            const Voice& v = voices_[static_cast<std::size_t>(voice)];
            const int height = std::min(v.height, kBarsBottom - kBarsTop);
            const int x = kWidth - kBarsW + voice * (kBarW + 4);
            if (height > 0) {
                make_fill(content, x, kBarsBottom - height, kBarW, height, v.lit ? colour_text() : colour_dim());
            }
        }
    }
}

void PlayingScreen::open_more() {
    // SPEC-096, SPEC-114: what "More" offers, each as its part is built.
    std::vector<Row> rows;
    std::vector<std::string> what;
    const auto add = [&](const char* part, const char* text, const char* id) {
        if (!part_built(part)) return;
        Row row;
        row.text = text;
        row.kind = "action";
        rows.push_back(row);
        what.push_back(id);
    };
    add("playlists", "Add to playlist", "add");
    add("modes", "Repeat all", "repeat_all");
    add("shuffle", "Shuffle", "shuffle");
    add("notes", "Notes", "notes");
    if (rows.empty()) return;
    Shell* frame = shell();
    frame->open_panel(std::make_unique<ListPanel>(frame, "more-playing", "More", rows, [this, frame, what](std::size_t i) {
        if (i >= what.size()) return;
        const Services& s = frame->services();
        const std::string& choice = what[i];
        if (choice == "add") {
            if (!s.add_to_playlist || !s.player) return;
            const PlayerStatus status = s.player->status();
            if (!status.has_entry || status.entry.path.compare(0, 7, "file://") == 0) return;
            s.add_to_playlist(Entry{status.entry.path, status.subtune, ""});
        } else if (choice == "repeat_all" || choice == "shuffle") {
            if (!s.settings) return;
            const bool on = choice == "repeat_all" ? s.settings->settings().repeat_all : s.settings->settings().shuffle;
            if (s.settings->set(choice, on ? "off" : "on") && s.setting_changed) s.setting_changed(choice);
            frame->redraw();
        } else if (choice == "notes") {
            show_notes();
        }
    }));
}

void PlayingScreen::show_notes() {
    // SPEC-097: the playing tune's entry in DOCUMENTS/STIL.txt, read by the quick worker.
    Shell* frame = shell();
    const Services& s = frame->services();
    if (!s.player) return;
    const PlayerStatus status = s.player->status();
    if (!status.has_entry) return;
    Files* files = s.files;
    const std::string stil = s.collection + "/DOCUMENTS/STIL.txt";
    const std::string path = status.entry.path;
    const int subtune = status.subtune;
    frame->quick()->post([frame, files, stil, path, subtune] {
        const StilNotes notes = stil_notes(files, stil, path, subtune);
        frame->ui()->post([frame, notes] {
            if (!notes.notice.empty()) frame->notice(notes.notice);
            frame->open_panel(std::make_unique<TextPanel>(frame, "text", "Notes", notes.found ? notes.text : kNoNotes));
        });
    });
}

void PlayingScreen::step_mode() {
    // SPEC-096, SPEC-065 (part `modes`): "Mode" steps `when_song_ends` round its four values.
    const Services& s = shell()->services();
    if (!s.settings) return;
    static const char* const kModes[] = {"subtunes", "next_tune", "repeat", "stop"};
    const std::string now = setting_text(s.settings->settings(), "when_song_ends");
    std::size_t at = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (now == kModes[i]) at = i;
    }
    const std::string next = kModes[(at + 1) % 4];
    if (!s.settings->set("when_song_ends", next)) return;
    if (s.setting_changed) s.setting_changed("when_song_ends");
    shell()->notice("MSG_MODE", {{"name", mode_words(s.settings->settings().when_song_ends)}});
}

bool PlayingScreen::on_action(const KeyEvent& event) {
    const Services& s = shell()->services();
    Player* player = s.player;
    if (!player) return false;
    const std::string& a = event.action;
    if (a == "previous_subtune") player->previous_subtune();
    else if (a == "next_subtune") player->next_subtune();
    else if (a == "restart") player->restart();
    else if (a == "toggle_voice_1") player->toggle_voice(1);
    else if (a == "toggle_voice_2") player->toggle_voice(2);
    else if (a == "toggle_voice_3") player->toggle_voice(3);
    else if (a == "toggle_filter") player->toggle_filter();
    else if (a == "next_chip") player->next_chip();
    else if (a == "skip_ahead") player->set_skip(!event.ended);
    else if (a == "stop") player->stop();
    else if (a == "next_song_end_mode") step_mode();
    else if (a == "open_more_playing") open_more();
    else if (a == "toggle_favourite") {
        const PlayerStatus status = player->status();
        if (!s.favourites || !status.has_entry || status.entry.path.compare(0, 7, "file://") == 0) return true;
        const ListsResult result = s.favourites->toggle(Entry{status.entry.path, status.subtune, ""});
        if (!result.message.empty()) shell()->notice(result.message, result.about);
    } else if (a == "browse_here") {
        // SPEC-096: the browser, at this tune; it plays on.
        const PlayerStatus status = player->status();
        if (status.has_entry && s.browser && status.entry.path.compare(0, 7, "file://") != 0) {
            s.browser->show(folder_of(status.entry.path), file_name(status.entry.path));
        }
        Shell* frame = shell();
        frame->open("browser");          // this screen is closed now: nothing of it may be touched
        return true;
    } else {
        return false;
    }
    shell()->redraw();
    return true;
}

void PlayingScreen::describe(Json* description) const {
    Json lines = Json::array();
    for (const std::string& line : this->lines()) lines.push(line);
    description->set("lines", lines);
    // SPEC-080: what the bars show. The registers are read in every build; the bars are drawn only with
    // the part `bars` (see draw()).
    Json voices = Json::array();
    for (const Voice& v : voices_) {
        Json voice = Json::object();
        voice.set("height", v.height);
        voice.set("lit", v.lit);
        voices.push(voice);
    }
    description->set("voices", voices);
}

}  // namespace spz::ui
