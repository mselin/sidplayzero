// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, playing (SPEC-060 to SPEC-080): the sound backend (the software one, made here and only
// here through platform::SoftwareSound, SPEC-025; or the simulator's fake), the player with its TuneFetcher
// on the fetcher of SPEC-141, the keys that work everywhere (SPEC-092: play or pause, previous and next
// tune, the volume of SPEC-076, the light engine of SPEC-079), the player's part of every screen
// description, the mini player (SPEC-109), and what is played again at start (SPEC-131, SPEC-135).
#include <memory>

#include "app/composition.h"
#include "platform/software_sound.h"
#include "spz/keep_start.h"
#include "spz/keymap.h"
#include "spz/messages.h"
#include "spz/version.h"
#include "spz/volume.h"

namespace spz::app {

namespace {

const char* state_word(PlayerState state) {
    switch (state) {
        case PlayerState::Idle: return "idle";
        case PlayerState::Waiting: return "waiting";
        case PlayerState::Fetching: return "fetching";
        case PlayerState::Loading: return "loading";
        case PlayerState::Playing: return "playing";
        case PlayerState::Paused: return "paused";
        case PlayerState::Failed: return "failed";
    }
    return "idle";
}

const char* mode_word(WhenSongEnds mode) {
    switch (mode) {
        case WhenSongEnds::Subtunes: return "subtunes";
        case WhenSongEnds::NextTune: return "next_tune";
        case WhenSongEnds::Repeat: return "repeat";
        case WhenSongEnds::Stop: return "stop";
    }
    return "subtunes";
}

// SPEC-109: the mark of each state in the mini player.
const char* state_mark(PlayerState state) {
    switch (state) {
        case PlayerState::Playing: return ">";
        case PlayerState::Paused: return "||";
        case PlayerState::Idle: return "[]";
        case PlayerState::Failed: return "!";
        default: return "...";
    }
}

std::string file_name(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool network_failure(const std::string& message) {
    return message == "MSG_FETCH_NETWORK" || message == "MSG_FETCH_TOO_LARGE" || message == "MSG_CARD_FULL" ||
           message == "MSG_CANNOT_WRITE";
}

}  // namespace

// ---- the player's news ---------------------------------------------------------------------------------------

struct Composition::PlayerEvents : PlayerListener {
    explicit PlayerEvents(Composition* owner) : parts(owner) {}
    void notice(const std::string& id, const std::map<std::string, std::string>& about) override {
        parts->app()->notice(id, about);
    }
    void changed() override {
        Player* player = parts->player_.get();
        if (!player) return;
        const PlayerStatus status = player->status();
        // SPEC-131: the current entry and its place in the queue are remembered.
        if (status.has_entry) {
            parts->state_->set_current(status.entry.path, status.entry.subtune);
            const State state = parts->state_->state();
            if (!state.queue_kind.empty()) {
                parts->state_->set_queue(state.queue_kind, state.queue_source, static_cast<int>(player->place()) + 1);
            }
        }
        const bool playing = status.state == PlayerState::Playing;
        if (parts->screen_off_ && part_built("screen-off") && parts->screen_off_->available()) {
            parts->screen_off_->set_playing(playing);
        }
        if (parts->index_) parts->index_->set_playing(playing);
        parts->app()->redraw();
    }
    Composition* parts;
};

// ---- the player's TuneFetcher, on the fetcher of SPEC-141 -----------------------------------------------------

struct Composition::PlayerFetcher : TuneFetcher, FetchListener {
    explicit PlayerFetcher(Composition* owner) : parts(owner) {}

    UnattendedFetch unattended(Millis* wait_ms) override {
        *wait_ms = 0;
        if (!parts->gate_->accepted()) return UnattendedFetch::NoticeNeeded;
        if (rest_until >= 0 && parts->seams_.clock->now_ms() < rest_until) return UnattendedFetch::NotNow;
        return UnattendedFetch::Allowed;
    }

    void fetch(const std::string& path, bool attended, Done done) override {
        if (ahead_path == path) {                 // the fetch-ahead is taken over
            ahead_path.clear();
            ahead_done = nullptr;
        }
        play_path = path;
        play_done = std::move(done);
        // The player waits the 700 ms of a step itself: what it asks for now is wanted now.
        const FetchAnswer answer = parts->fetcher_->request(path, attended ? FetchWhy::Chosen : FetchWhy::Unattended);
        switch (answer) {
            case FetchAnswer::Present: answer_play(path, true, ""); break;
            case FetchAnswer::Fetching:
            case FetchAnswer::Waiting:
            case FetchAnswer::AskingNotice: break;
            case FetchAnswer::Failed: answer_play(path, false, parts->fetcher_->ahead_failure(path)); break;
            case FetchAnswer::NoticeNeeded:
            case FetchAnswer::Resting:
            case FetchAnswer::OverLimit: answer_play(path, false, "MSG_FETCH_NETWORK"); break;
        }
    }

    void fetch_ahead(const std::string& path, Done done) override {
        ahead_path = path;
        ahead_done = std::move(done);
        const FetchAnswer answer = parts->fetcher_->request(path, FetchWhy::Ahead);
        if (answer == FetchAnswer::Present) {
            answer_ahead(path, true, "");
        } else if (answer != FetchAnswer::Fetching) {
            // Not started: nothing is said, and the entry is asked for when it is reached.
            ahead_path.clear();
            ahead_done = nullptr;
        }
    }

    void cancel() override {
        parts->fetcher_->cancel_play();
        play_path.clear();
        play_done = nullptr;
        ahead_path.clear();
        ahead_done = nullptr;
    }

    // FetchListener
    void fetch_started(const std::string&) override {}
    void fetched(const std::string& path, FetchWhy why) override { answered(path, why, true, ""); }
    void fetch_failed(const std::string& path, FetchWhy why, const std::string& message) override {
        if (why != FetchWhy::Chosen && network_failure(message)) {
            rest_until = parts->seams_.clock->now_ms() + kFetchRestMs;      // SPEC-143
        }
        answered(path, why, false, message);
    }
    void fetch_cancelled(const std::string& path) override {
        // ESC in HVSC's notice: what was asked for does not happen (SPEC-152).
        if (path != play_path) return;
        play_path.clear();
        play_done = nullptr;
        if (parts->player_) parts->player_->stop();
    }

    void answered(const std::string& path, FetchWhy why, bool fetched, const std::string& message) {
        if (why == FetchWhy::Ahead) {
            if (path == ahead_path) answer_ahead(path, fetched, message);
        } else if (path == play_path) {
            answer_play(path, fetched, message);
        }
    }
    // The answers reach the player as a message of their own, never inside a call it made.
    void answer_play(const std::string& path, bool fetched, const std::string& message) {
        Done done = std::move(play_done);
        play_done = nullptr;
        if (play_path == path) play_path.clear();
        if (!done) return;
        FetchResult result;
        result.fetched = fetched;
        result.message = message;
        parts->on_ui([done, result] { done(result); });
    }
    void answer_ahead(const std::string& path, bool fetched, const std::string& message) {
        Done done = std::move(ahead_done);
        ahead_done = nullptr;
        if (ahead_path == path) ahead_path.clear();
        if (!done) return;
        FetchResult result;
        result.fetched = fetched;
        result.message = message;
        parts->on_ui([done, result] { done(result); });
    }

    Composition* parts;
    std::string play_path, ahead_path;
    Done play_done, ahead_done;
    Millis rest_until = -1;
};

// ---- what start-up asks of the player and the collection (SPEC-131, SPEC-135) -------------------------------

struct Composition::StartSources : QueueSources, PlayerAtStart {
    StartSources(Collection* tree, Playlists* lists) : collection(tree), playlists(lists) {}
    bool folder_tunes(const std::string& folder, std::vector<Entry>* out) override {
        const FolderListing listing = collection->list(folder);
        if (!listing.notice.empty()) return false;
        for (const FolderEntry& entry : listing.entries) {
            if (!entry.is_folder && !entry.path.empty()) out->push_back(Entry{entry.path, 0, ""});
        }
        return true;
    }
    bool playlist_entries(const std::string& name, std::vector<Entry>* out) override {
        if (!playlists || !playlists->find(name)) return false;
        *out = playlists->queue(name);
        return true;
    }
    void restore(const Entry& current, const std::vector<Entry>& queue, int place) override {
        restored = true;
        now = current;
        entries = queue;
        at = place;
    }
    void play_current() override { play = true; }

    Collection* collection;
    Playlists* playlists;
    bool restored = false, play = false;
    Entry now;
    std::vector<Entry> entries;
    int at = 0;
};

// ---- making it ----------------------------------------------------------------------------------------------

void Composition::make_play() {
    // SPEC-025: the software backend is made in this one place, by platform::SoftwareSound.
    if (seams_.backend) {
        backend_ = seams_.backend;
    } else if (seams_.engine && seams_.sink) {
        sound_.reset(new platform::SoftwareSound(seams_.engine, seams_.sink, seams_.clock, app()->ui()));
        backend_ = sound_->backend();
        if (seams_.render_thread) sound_->start_render_thread();
    }
    player_events_.reset(new PlayerEvents(this));
    player_fetcher_.reset(new PlayerFetcher(this));
    route_fetches_to_player(player_fetcher_.get());
    make_player();

    ui::Services& s = services();
    s.sound = backend_;
    s.play =[this](const std::vector<Entry>& queue, std::size_t chosen, const std::string& kind,
                    const std::string& source) {
        if (!player_ || queue.empty()) return;
        player_->play(queue, chosen);
        state_->set_queue(kind, source, static_cast<int>(player_->place()) + 1);
        app()->open("playing");
    };

    // SPEC-092: what the keys of `always` and `screens` do for the player, wherever they are pressed.
    App* a = app();
    a->on_action("play_pause", [this](const KeyEvent&) { if (player_) player_->play_pause(); });
    a->on_action("next_tune", [this](const KeyEvent&) { if (player_) player_->next_tune(); });
    a->on_action("previous_tune", [this](const KeyEvent&) { if (player_) player_->previous_tune(); });
    a->on_action("use_light_engine", [this](const KeyEvent&) { if (player_) player_->use_light_engine(); });
    // SPEC-076: the volume, through SystemVolume on the quick worker.
    const auto volume = [this](VolumeKey key) {
        SystemVolume* system = seams_.volume;
        if (!system) return;
        app()->quick()->post([this, system, key] {
            const VolumeOutcome outcome = press_volume_key(system, key);
            on_ui([this, outcome] {
                if (outcome.notice != "MSG_VOLUME") app()->notice(outcome.notice);
                else if (outcome.muted) app()->notice("MSG_VOLUME");
                else app()->notice("MSG_VOLUME", {{"n", std::to_string(outcome.level)}});
            });
        });
    };
    a->on_action("volume_up", [volume](const KeyEvent&) { volume(VolumeKey::Up); });
    a->on_action("volume_down", [volume](const KeyEvent&) { volume(VolumeKey::Down); });
    a->on_action("volume_mute", [volume](const KeyEvent&) { volume(VolumeKey::Mute); });

    // SPEC-254: `player` on every screen; `mini` with the part `mini` (SPEC-109).
    a->add_describer([this](Json* d) {
        Json player = Json::object();
        const PlayerStatus st = player_ ? player_->status() : PlayerStatus();
        player.set("state", state_word(st.state));
        player.set("message", st.message.empty() ? Json() : Json(st.message));
        player.set("path", st.has_entry ? Json(st.entry.path) : Json());
        player.set("subtune", st.subtune > 0 ? Json(st.subtune) : Json());
        player.set("subtunes", st.subtunes > 0 ? Json(st.subtunes) : Json());
        player.set("elapsed_ms", st.has_entry ? Json(static_cast<std::int64_t>(st.elapsed_ms)) : Json());
        player.set("length_ms", st.has_entry ? Json(static_cast<std::int64_t>(st.length_ms)) : Json());
        const bool known = st.tune_known;
        player.set("engine", known ? Json(st.engine == EngineKind::Accurate ? "accurate" : "light") : Json());
        player.set("chips", known ? Json(st.chips) : Json());
        player.set("model", known ? Json(st.model == SidModel::Mos8580 ? "8580" : "6581") : Json());
        player.set("clock", known ? Json(st.clock == C64Clock::Ntsc ? "NTSC" : "PAL") : Json());
        if (known) {
            Json muted = Json::array();
            Json filter = Json::array();
            for (int chip = 0; chip < st.chips && chip < 3; ++chip) {
                Json voices = Json::array();
                for (int voice = 0; voice < 3; ++voice) voices.push(st.muted[chip][voice]);
                muted.push(voices);
                filter.push(st.filter_on[chip]);
            }
            player.set("muted", muted);
            player.set("filter", filter);
            player.set("chip", st.chip);
        } else {
            player.set("muted", Json());
            player.set("filter", Json());
            player.set("chip", Json());
        }
        player.set("mode", mode_word(settings_->settings().when_song_ends));
        player.set("underrun_frames", backend_ ? Json(backend_->underruns()) : Json());
        d->set("player", player);
        if (!part_built("mini") || !st.has_entry) return;
        const Json* had = d->get("mini");
        Json mini = had && !had->is_null() ? *had : Json::object();
        mini.set("state", state_word(st.state));
        mini.set("title", st.tune_known && !st.info.title.empty() ? st.info.title : file_name(st.entry.path));
        const bool timed = st.state == PlayerState::Playing || st.state == PlayerState::Paused;
        mini.set("elapsed", timed ? Json(time_text(st.elapsed_ms)) : Json());
        d->set("mini", mini);
    });
    if (part_built("mini")) {
        a->add_top_right([this]() -> std::string {
            if (!player_) return "";
            const PlayerStatus st = player_->status();
            if (!st.has_entry) return "";
            std::string text = state_mark(st.state);
            text += " ";
            text += st.tune_known && !st.info.title.empty() ? st.info.title : file_name(st.entry.path);
            if (st.state == PlayerState::Playing || st.state == PlayerState::Paused) {
                text += " " + time_text(st.elapsed_ms);
            }
            return text;
        });
        // The elapsed time changes by itself: the top line is drawn again when its second changes.
        auto shown = std::make_shared<std::string>();
        a->every(100, [this, shown] {
            if (!player_) return;
            const PlayerStatus st = player_->status();
            const std::string now = std::string(state_word(st.state)) + " " + time_text(st.elapsed_ms);
            if (now != *shown) {
                *shown = now;
                app()->redraw();
            }
        });
    }
}

void Composition::make_player() {
    PlayerSeams seams;
    seams.backend = backend_;
    seams.clock = seams_.clock;
    seams.files = seams_.files;
    seams.worker = app()->quick();
    seams.ui = app()->ui();
    seams.fetcher = player_fetcher_.get();
    seams.listener = player_events_.get();
    PlayerOptions options;
    options.root = root_.path;
    options.has_seed = seams_.has_seed;
    options.seed = seams_.seed;
    if (!backend_) return;
    player_.reset(new Player(seams, options, &settings_->settings()));
    player_->set_listing(listing_.get());
    services().player = player_.get();
}

void Composition::remake_player() {
    if (player_) {
        player_->stop();
        retired_players_.push_back(std::move(player_));
    }
    make_player();
    if (player_) player_->set_listing(listing_.get());
    app()->redraw();
}

void Composition::play_at_start() {
    // SPEC-131: the current entry, Idle, with its queue made again from its source, on the quick worker;
    // then SPEC-135 (part `resume`): the entry plays from its beginning if it is on the device.
    if (!player_) return;
    const State state = state_->state();
    const Settings settings = settings_->settings();
    Files* files = seams_.files;
    const std::string root = root_.path;
    auto sources = std::make_shared<StartSources>(collection_.get(), playlists_.get());
    app()->quick()->post([this, state, settings, files, root, sources] {
        if (!restore_player(state, sources.get(), sources.get())) return;
        if (part_built("resume")) resume_at_start(settings, state, files, root, sources.get());
        on_ui([this, sources] {
            if (!player_ || !sources->restored) return;
            std::vector<Entry> queue = sources->entries;
            std::size_t place = sources->at > 0 ? static_cast<std::size_t>(sources->at - 1) : 0;
            if (queue.empty() || place >= queue.size() || queue[place].path != sources->now.path) {
                queue = {sources->now};
                place = 0;
            }
            player_->restore(queue, place);
            if (sources->play) player_->play_pause();
        });
    });
}

void Composition::measure_engines() {
    // SPEC-072: once the browser has shown for 3 s, if the factors are not there for this version and rate,
    // the long worker measures the two engines.
    if (!seams_.make_engine || !player_) return;
    Files* files = seams_.files;
    const std::string cache = folders_.cache;
    const int rate = settings_->settings().sample_rate;
    app()->long_worker()->post([this, files, cache, rate] {
        const SpeedFactors kept = load_speed_factors(files, cache, app_version(), rate);
        on_ui([this, kept, files, cache, rate] {
            if (kept.measured) {
                if (player_) player_->set_speed_factors(kept);
                return;
            }
            seams_.clock->call_after(3000, [this, files, cache, rate] {
                if (running()) measure_engines_now(files, cache, rate);
            });
        });
    });
}

void Composition::measure_engines_now(Files* files, const std::string& cache, int rate) {
    {
        auto make = seams_.make_engine;
        Clock* clock = seams_.clock;
        app()->long_worker()->post([this, make, clock, files, cache, rate] {
            std::unique_ptr<Engine> engine = make();
            if (!engine || !running()) return;
            SpeedFactors factors;
            factors.accurate = measure_engine(engine.get(), EngineKind::Accurate, rate, clock);
            if (!running()) return;
            factors.light = measure_engine(engine.get(), EngineKind::Light, rate, clock);
            factors.measured = factors.accurate > 0 && factors.light > 0;
            if (!factors.measured) return;
            save_speed_factors(files, cache, factors, app_version(), rate);
            metrics_.set("speed_factor_accurate", std::to_string(factors.accurate));
            metrics_.set("speed_factor_light", std::to_string(factors.light));
            on_ui([this, factors] {
                if (player_) player_->set_speed_factors(factors);
            });
        });
    }
}

}  // namespace spz::app
