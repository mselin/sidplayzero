// SPDX-License-Identifier: GPL-3.0-or-later
// The composition's order: what is made first, what starts first and what stops first (SPEC-200, SPEC-201).
// Each area's own part is in its compose_*.cpp.
#include "app/composition.h"

#include <cstdio>

#include "platform/software_sound.h"

namespace spz::app {

std::string time_text(Millis ms) {
    if (ms < 0) ms = 0;
    const Millis seconds = ms / 1000;
    char text[32];
    std::snprintf(text, sizeof text, "%lld:%02lld", static_cast<long long>(seconds / 60),
                  static_cast<long long>(seconds % 60));
    return text;
}

Composition::Composition(App* app, const Seams& seams) : app_(app), seams_(seams) {
    ui::Services& s = services();
    s.files = seams_.files;
    s.backlight = seams_.backlight;
    s.libraries = seams_.libraries;
    // SPEC-200, step 2, and what everything else is made from.
    make_keep();
    make_hvsc();
    make_browse();
    make_search();
    make_lists();
    make_play();
}

void Composition::on_ui(std::function<void()> fn) {
    app()->ui()->post([this, fn] {
        if (running()) fn();
    });
}

Composition::~Composition() {
    running_ = false;
    // The player first: it is the backend's listener, and the backend goes with the sound.
    player_.reset();
    sound_.reset();
}

void Composition::start() {
    // SPEC-200, step 5. Nothing here uses the network.
    keep_at_start();
    hvsc_at_start();    // the marker, then the listing; when it is ready, the browser and the player follow
}

void Composition::stop() {
    // SPEC-201: stop the sound backend, which silences it; the stream closes when the backend goes, once
    // the workers that may still hold it have stopped.
    if (player_) player_->stop();
    if (sound_) sound_->stop_render_thread();
    running_ = false;
    // Put the screen's brightness back if the app had turned it off (SPEC-207).
    if (screen_off_) screen_off_->at_exit();
    // Write state.ini (SPEC-132).
    if (state_) state_->save_now();
    // Tell the workers to stop: a download is cut short and keeps its partial file, `7z` and its group are
    // ended, the index keeps what it last saved.
    jobs_.stop();
    if (fetcher_) {
        fetcher_->cancel_play();
        fetcher_->cancel_playlist();
    }
    if (seams_.at_exit) seams_.at_exit();
}

void Composition::render() {
    if (!sound_) return;
    while (sound_->render_step()) {
    }
}

void Composition::write_metrics(Millis exit_ms) {
    metrics_.set_number("exit_ms", exit_ms < 0 ? 0 : exit_ms);
    if (!seams_.metrics.empty()) metrics_.write(seams_.files, seams_.metrics);
}

}  // namespace spz::app
