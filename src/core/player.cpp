// SPDX-License-Identifier: GPL-3.0-or-later
// The player (SPEC-060 to SPEC-069, SPEC-079): one state machine, on the UI thread.
//
// Everything here runs on the UI thread except the load job (run_load), which the quick worker runs: it
// reads the file, checks it (SPEC-203), reads its header and length, and gives it to the backend. Its
// result comes back as a job on the UI thread. Every job, timer and answer carries the generation it
// belongs to; a key or an event that moves the player on starts a new one, so that whatever belonged to
// the old one comes to nothing when it arrives.
#include "spz/player.h"

#include <algorithm>
#include <utility>

#include "spz/md5.h"

namespace spz {

namespace {

bool is_bad_file(const std::string& message) {
    return message == "MSG_INVALID_SID" || message == "MSG_ENGINE_REFUSED" || message == "MSG_NEEDS_BASIC" ||
           message == "MSG_FETCH_NOT_FOUND" || message == "MSG_FETCH_MISMATCH";
}

// SPEC-143's failures: after them unattended playback moves on to the next present entry.
bool is_network_failure(const std::string& message) {
    return message == "MSG_FETCH_NETWORK" || message == "MSG_FETCH_TOO_LARGE" || message == "MSG_CARD_FULL" ||
           message == "MSG_CANNOT_WRITE";
}

std::uint64_t seed_of(const PlayerOptions& options, const Clock* clock) {
    if (options.has_seed) return options.seed;
    return clock ? static_cast<std::uint64_t>(clock->wall_time_s()) : 0;
}

}  // namespace

// What the worker is given: everything it needs, copied, so that it touches nothing of the player's.
struct Player::LoadJob {
    std::uint64_t generation = 0;
    std::string file;                   // the full path of the file
    std::string path;                   // the entry's path, for the listing
    int subtune = 0;                    // asked for; 0, or one the tune does not have: its default
    EngineChoice choice = EngineChoice::Auto;
    SpeedFactors factors;
    int light_from_chips = 0;
    bool light = false;                 // sidlite for this tune, whatever the setting
    unsigned rate = 48000;
    SidModel model = SidModel::FromTune;
    C64Clock clock = C64Clock::FromTune;
    Roms roms;
    Millis default_ms = 0;
    bool has_listing = false;
    Listing listing;                    // a copy shares the mapping
};

struct Player::LoadResult {
    std::uint64_t generation = 0;
    bool present = true;                // the file is on the device
    bool listed = false;                // not present: the listing has its path (absent), else it is missing
    std::vector<Millis> listed_lengths;
    bool tune_known = false;
    TuneInfo info;
    int subtune = 0;
    Millis length_ms = 0;
    EngineSettings settings;
    std::string message;                // the failure, if any (SPEC-069)
    bool loaded = false;                // the backend has it
};

// ---- Making and wiring ---------------------------------------------------------------------------------------------

Player::Player(const PlayerSeams& seams, const PlayerOptions& options, const Settings* settings)
    : seams_(seams), options_(options), settings_(settings), alive_(std::make_shared<bool>(true)),
      generation_shared_(std::make_shared<std::atomic<std::uint64_t>>(0)),
      generator_(seed_of(options, seams.clock)) {
    if (seams_.backend) seams_.backend->set_listener(this);
}

Player::~Player() {
    *alive_ = false;
    for (Clock::TimerId* id : {&wait_timer_, &failed_timer_, &ahead_timer_, &tick_timer_}) cancel_timer(id);
}

void Player::set_listing(const Listing* listing) { listing_ = listing; }
void Player::set_speed_factors(const SpeedFactors& factors) { factors_ = factors; }
void Player::set_roms(const Roms& roms) { roms_ = roms; }
void Player::set_playing_off(bool off) { playing_off_ = off; }

void Player::settings_changed() {
#if SPZ_PART_SHUFFLE
    if (!queue_.empty() && settings_ && settings_->shuffle != queue_.shuffled()) queue_.set_shuffle(settings_->shuffle);
#endif
    notify();
}

bool Player::shuffle_on() const {
#if SPZ_PART_SHUFFLE
    return settings_ && settings_->shuffle;
#else
    return false;
#endif
}

bool Player::repeat_all() const {
#if SPZ_PART_MODES
    return settings_ && settings_->repeat_all;
#else
    return false;
#endif
}

WhenSongEnds Player::when_song_ends() const {
#if SPZ_PART_MODES
    return settings_ ? settings_->when_song_ends : WhenSongEnds::Subtunes;
#else
    return WhenSongEnds::Subtunes;
#endif
}

// ---- Small things ----------------------------------------------------------------------------------------------------

void Player::notify() {
    if (seams_.listener) seams_.listener->changed();
}

void Player::notice(const std::string& id) {
    if (seams_.listener) seams_.listener->notice(id, {});
}

void Player::cancel_timer(Clock::TimerId* id) {
    if (*id != 0 && seams_.clock) seams_.clock->cancel(*id);
    *id = 0;
}

Clock::TimerId Player::timer(Millis ms, void (Player::*fn)()) {
    if (!seams_.clock) return 0;
    const std::shared_ptr<bool> alive = alive_;
    const std::uint64_t generation = generation_;
    return seams_.clock->call_after(ms < 0 ? 0 : ms, [this, alive, generation, fn] {
        if (*alive && generation == generation_) (this->*fn)();
    });
}

void Player::new_generation() {
    ++generation_;
    generation_shared_->store(generation_);
    for (Clock::TimerId* id : {&wait_timer_, &failed_timer_, &ahead_timer_, &tick_timer_}) cancel_timer(id);
}

bool Player::loaded() const { return state_ == PlayerState::Playing || state_ == PlayerState::Paused; }

void Player::unload() {
    if (loaded() && seams_.backend) seams_.backend->stop();
    skipping_ = false;
}

void Player::give_up_fetches() {
    if (fetch_in_flight_ && seams_.fetcher) seams_.fetcher->cancel();
    fetch_in_flight_ = false;
    ahead_ = Ahead();
}

bool Player::refuse_while_off() {
    if (!playing_off_) return false;
    notice("MSG_UNPACKING");
    return true;
}

std::string Player::full_path(const std::string& path) const {
    const std::string file_scheme = "file://";
    if (path.compare(0, file_scheme.size(), file_scheme) == 0) return path.substr(file_scheme.size());
    std::string root = options_.root;
    while (!root.empty() && root.back() == '/') root.pop_back();
    return root + path;
}

int Player::entry_subtune() const {
    return entry_.subtune >= 1 && (!tune_known_ || entry_.subtune <= subtunes_) ? entry_.subtune : 0;
}

bool Player::next_is_subtune() const {
    return when_song_ends() == WhenSongEnds::Subtunes && entry_subtune() == 0 && tune_known_ && subtune_ >= 1 &&
           subtune_ < subtunes_;
}

void Player::reset_tune_choices() {
    for (int chip = 0; chip < 3; ++chip) {
        filter_on_[chip] = true;
        for (int voice = 0; voice < 3; ++voice) muted_[chip][voice] = false;
    }
    chip_ = 1;
    light_for_tune_ = false;
    acted_slow_ = false;
    offer_until_ = -1;
    slow_seconds_.clear();
}

// ---- Loading ---------------------------------------------------------------------------------------------------------

void Player::go_to_entry(Reach reach, int subtune) {
    entry_ = queue_.current();
    has_entry_ = true;
    reach_ = reach;
    just_fetched_ = false;
    if (entry_.path != loaded_path_) {
        loaded_path_ = entry_.path;
        reset_tune_choices();
        tune_known_ = false;
        subtune_ = 0;
        subtunes_ = 0;
        length_ms_ = 0;
    }
    if (reach != Reach::Unattended) {
        bad_files_ = 0;
        present_only_ = false;
        passed_ = 0;
        notice_needed_said_ = false;
    }
    load_current(subtune);
}

void Player::load_current(int subtune) {
    new_generation();
    state_ = PlayerState::Loading;
    message_.clear();
    want_subtune_ = subtune;
    ahead_asked_ = false;

    LoadJob job;
    job.generation = generation_;
    job.file = full_path(entry_.path);
    job.path = entry_.path;
    job.subtune = subtune > 0 ? subtune : entry_.subtune;
    if (settings_) {
        job.choice = settings_->engine;
        job.rate = static_cast<unsigned>(settings_->sample_rate);
#if SPZ_PART_FORCE
        job.model = settings_->sid_model;
        job.clock = settings_->clock;
#endif
        job.default_ms = static_cast<Millis>(settings_->default_length_s) * 1000;
    }
#if !SPZ_PART_RATE
    job.rate = 48000;
#endif
    job.factors = factors_;
    job.light_from_chips = light_from_chips_;
    job.light = light_for_tune_;
    job.roms = roms_;
    if (listing_ && listing_->is_open()) {
        job.has_listing = true;
        job.listing = *listing_;
    }

    const std::shared_ptr<bool> alive = alive_;
    const std::shared_ptr<std::atomic<std::uint64_t>> current = generation_shared_;
    Files* files = seams_.files;
    SoundBackend* backend = seams_.backend;
    Executor* ui = seams_.ui;
    seams_.worker->post([this, alive, current, files, backend, ui, job] {
        LoadResult result;
        if (!run_load(job, files, backend, *current, &result)) return;
        ui->post([this, alive, result] {
            if (*alive) loaded_result(result);
        });
    });
    notify();
}

bool Player::run_load(const LoadJob& job, Files* files, SoundBackend* backend,
                      const std::atomic<std::uint64_t>& current, LoadResult* out) {
    if (current.load() != job.generation) return false;
    LoadResult& result = *out;
    result.generation = job.generation;

    Bytes bytes;
    const FileStatus status = files ? files->read(job.file, kTuneSizeLimit, &bytes) : FileStatus::NotFound;
    if (status == FileStatus::NotFound) {
        result.present = false;
        ListedTune listed;
        if (job.has_listing && job.listing.by_path(job.path, &listed)) {
            result.listed = true;
            result.listed_lengths = listed.lengths;
        }
        return true;
    }
    if (status != FileStatus::Ok) {
        result.message = "MSG_INVALID_SID";
        return true;
    }
    result.message = check_tune(bytes);
    if (!result.message.empty()) return true;
    if (!read_tune_info(bytes, &result.info)) {
        result.message = "MSG_INVALID_SID";
        return true;
    }
    result.tune_known = true;
    const TuneInfo& info = result.info;
    result.subtune = job.subtune >= 1 && job.subtune <= info.songs ? job.subtune : info.default_song;

    // SPEC-063: by the file's MD5, never by its path; else the setting.
    result.length_ms = job.default_ms;
    if (job.has_listing) {
        ListedTune listed;
        if (job.listing.by_md5(md5_hex(bytes), &listed) &&
            static_cast<std::size_t>(result.subtune) <= listed.lengths.size()) {
            result.length_ms = listed.lengths[static_cast<std::size_t>(result.subtune) - 1];
        }
    }

    EngineSettings& settings = result.settings;
    settings.sample_rate = job.rate;
    settings.model = job.model;
    settings.clock = job.clock;
    settings.kernal = job.roms.kernal;
    settings.basic = job.roms.basic;
    settings.chargen = job.roms.chargen;
    if (job.light) {
        settings.kind = EngineKind::Light;
    } else if (job.choice == EngineChoice::Auto && job.light_from_chips > 0 && info.chips >= job.light_from_chips) {
        settings.kind = EngineKind::Light;      // SPEC-079: for the rest of this run
    } else {
        settings.kind = choose_engine(job.choice, job.factors, info.chips);
    }
    if (info.needs_basic && job.roms.basic.empty()) {
        result.message = "MSG_NEEDS_BASIC";
        return true;
    }
    if (current.load() != job.generation) return false;
    std::string why;
    if (!backend || !backend->load(bytes, result.subtune, settings, &why)) {
        result.message = why.empty() ? "MSG_ENGINE_REFUSED" : why;
        return true;
    }
    result.loaded = true;
    return true;
}

void Player::loaded_result(const LoadResult& result) {
    if (result.generation != generation_ || state_ != PlayerState::Loading) {
        // A load that was left behind. If it got as far as the backend, and nothing newer is on its way
        // there, what it loaded is taken away again.
        if (result.loaded && state_ != PlayerState::Loading && !loaded() && seams_.backend) seams_.backend->stop();
        return;
    }
    if (!result.present) {
        absent(result);
        return;
    }
    if (fetch_in_flight_) give_up_fetches();    // the entry is here: nothing is waited for any more
    ahead_ = Ahead();

    tune_known_ = result.tune_known;
    if (result.tune_known) {
        info_ = result.info;
        subtune_ = result.subtune;
        subtunes_ = result.info.songs;
        length_ms_ = result.length_ms;
        chips_ = std::max(1, std::min(3, result.info.chips));
        if (chip_ > chips_) chip_ = 1;
        engine_ = result.settings.kind;
        sample_rate_ = static_cast<int>(result.settings.sample_rate);
        model_forced_ = result.settings.model != SidModel::FromTune;
        clock_forced_ = result.settings.clock != C64Clock::FromTune;
        model_ = model_forced_ ? result.settings.model
                               : (result.info.model == SidModel::FromTune ? SidModel::Mos6581 : result.info.model);
        c64_clock_ = clock_forced_ ? result.settings.clock
                                   : (result.info.clock == C64Clock::FromTune ? C64Clock::Pal : result.info.clock);
    }
    if (!result.message.empty()) {
        fail(result.message);
        return;
    }

    // Loaded: start() and Playing.
    SoundBackend* backend = seams_.backend;
    length_samples_ = length_ms_ > 0 ? static_cast<std::uint64_t>(length_ms_) * result.settings.sample_rate / 1000 : 0;
    backend->set_length(length_samples_);
    send_controls();
    backend->start();
    state_ = PlayerState::Playing;
    message_.clear();
    bad_files_ = 0;
    present_only_ = false;
    passed_ = 0;
    notice_needed_said_ = false;
    just_fetched_ = false;
    started(true);
    notify();
}

void Player::send_controls() {
#if SPZ_PART_VOICES
    bool off = false;
    for (int chip = 0; chip < chips_; ++chip) {
        off = off || !filter_on_[chip];
        for (int voice = 0; voice < 3; ++voice) off = off || muted_[chip][voice];
    }
    if (!off && !controls_sent_) return;
    const BackendAbilities can = seams_.backend->can();
    for (int chip = 0; chip < chips_; ++chip) {
        if (can.mute) {
            for (int voice = 0; voice < 3; ++voice) seams_.backend->set_muted(chip, voice, muted_[chip][voice]);
        }
        if (can.filter) seams_.backend->set_filter(chip, filter_on_[chip]);
    }
    controls_sent_ = off;
#endif
}

// ---- An entry that is not on the device --------------------------------------------------------------------------

void Player::absent(const LoadResult& result) {
    tune_known_ = false;
    subtune_ = 0;
    if (!result.listed) {
        missing();
        return;
    }
    subtunes_ = static_cast<int>(result.listed_lengths.size());
    length_ms_ = result.listed_lengths.empty() ? 0 : result.listed_lengths.front();
    if (just_fetched_) {            // fetched, and yet not there: it could not be put in its place
        fail("MSG_CANNOT_WRITE");
        return;
    }
    const bool ahead_is_ours = ahead_.path == entry_.path;
    if (!ahead_is_ours) {
        // Whatever was being fetched was for another tune. A key that plays this one at once asks for it
        // (fetch() then cancels the other); otherwise it is given up now.
        if (reach_ != Reach::Key && fetch_in_flight_) give_up_fetches();
        ahead_ = Ahead();
    }
    switch (reach_) {
        case Reach::Key:
            request(true);
            break;
        case Reach::Step:
            state_ = PlayerState::Waiting;
            wait_timer_ = timer(kStepWaitMs, &Player::wait_over);
            notify();
            break;
        case Reach::Unattended:
            if (present_only_) {
                pass_over();
            } else if (ahead_is_ours && ahead_.in_flight) {
                request(false);         // the fetch-ahead is still running: its request is taken over
            } else if (ahead_is_ours && ahead_.done && !ahead_.fetched) {
                const std::string message = ahead_.message;
                ahead_ = Ahead();
                fail(message);          // it failed while the song played: that is this entry's failure
            } else {
                ask_unattended();
            }
            break;
    }
}

void Player::missing() {
    subtunes_ = 0;
    length_ms_ = 0;
    if (reach_ == Reach::Unattended) {
        pass_over();                    // and its message is not shown
        return;
    }
    if (fetch_in_flight_) give_up_fetches();
    fail("MSG_ENTRY_MISSING");
}

void Player::request(bool attended) {
    if (!seams_.fetcher) {
        fail("MSG_FETCH_NETWORK");
        return;
    }
    state_ = PlayerState::Fetching;
    if (ahead_.path == entry_.path) ahead_ = Ahead();
    fetch_in_flight_ = true;
    const std::uint64_t token = ++fetch_token_;
    const std::uint64_t generation = generation_;
    const std::shared_ptr<bool> alive = alive_;
    notify();
    seams_.fetcher->fetch(entry_.path, attended, [this, alive, generation, token](const FetchResult& result) {
        if (*alive) fetch_answer(generation, token, result);
    });
}

void Player::ask_unattended() {
    if (!seams_.fetcher) {
        pass_over();
        return;
    }
    Millis wait = 0;
    switch (seams_.fetcher->unattended(&wait)) {
        case UnattendedFetch::Allowed:
            request(false);
            break;
        case UnattendedFetch::Later:
            state_ = PlayerState::Waiting;
            wait_timer_ = timer(wait, &Player::wait_over);
            notify();
            break;
        case UnattendedFetch::NotNow:
            pass_over();
            break;
        case UnattendedFetch::NoticeNeeded:
            if (!notice_needed_said_) {
                notice_needed_said_ = true;
                notice("MSG_NOTICE_NEEDED");
            }
            pass_over();
            break;
    }
}

void Player::fetch_answer(std::uint64_t generation, std::uint64_t token, const FetchResult& result) {
    if (token == fetch_token_) fetch_in_flight_ = false;
    if (generation != generation_ || state_ != PlayerState::Fetching) return;
    if (result.fetched) {
        just_fetched_ = true;
        load_current(want_subtune_);
    } else {
        fail(result.message.empty() ? "MSG_FETCH_NETWORK" : result.message);
    }
}

void Player::wait_over() {
    wait_timer_ = 0;
    if (state_ != PlayerState::Waiting) return;
    if (reach_ == Reach::Unattended) {
        ask_unattended();
        return;
    }
    if (ahead_.path == entry_.path && ahead_.done && ahead_.fetched) {
        ahead_ = Ahead();
        load_current(want_subtune_);    // it came meanwhile
        return;
    }
    request(true);                      // a key reached it; a fetch-ahead still in flight is taken over
}

// ---- Failing, and moving on by itself ------------------------------------------------------------------------------

void Player::fail(const std::string& message) {
    state_ = PlayerState::Failed;
    message_ = message;
    if (is_bad_file(message)) ++bad_files_;
    failed_for_network_ = is_network_failure(message);
    cancel_timer(&failed_timer_);
    if (reach_ == Reach::Unattended && message != "MSG_NO_SOUND" && message != "MSG_ENTRY_MISSING") {
        failed_timer_ = timer(kFailedWaitMs, &Player::unattended_wait_over);
    }
    notify();
}

void Player::unattended_wait_over() {
    failed_timer_ = 0;
    if (state_ != PlayerState::Failed) return;
    if (bad_files_ >= kBadFilesInARow) {
        stop_playback("MSG_STOPPED_BAD_FILES");
        return;
    }
    present_only_ = failed_for_network_;
    passed_ = 0;
    next_unattended();
}

void Player::stop_playback(const std::string& notice_id) {
    unload();
    give_up_fetches();
    new_generation();
    state_ = PlayerState::Idle;
    message_.clear();
    if (!notice_id.empty()) notice(notice_id);
    notify();
}

void Player::pass_over() {
    ++passed_;
    if (passed_ > queue_.size()) {      // round the whole queue once, and nothing to play
        stop_playback(present_only_ ? "MSG_REST_ABSENT" : "");
        return;
    }
    next_unattended();
}

void Player::next_unattended() {
    if (playing_off_) {
        stop_playback("MSG_UNPACKING");
        return;
    }
    if (!queue_.next(repeat_all())) {
        stop_playback(present_only_ ? "MSG_REST_ABSENT" : "");
        return;
    }
    go_to_entry(Reach::Unattended, 0);
}

// ---- Playing: the fetch-ahead and the underrun counter -----------------------------------------------------------

void Player::started(bool fresh) {
    if (fresh) {
        ahead_asked_ = false;
        slow_seconds_.clear();
    }
    last_underruns_ = seams_.backend->underruns();
    arm_ahead();
#if SPZ_PART_TOO_SLOW
    cancel_timer(&tick_timer_);
    tick_timer_ = timer(1000, &Player::tick);
#endif
}

void Player::paused() {
    cancel_timer(&ahead_timer_);
    cancel_timer(&tick_timer_);
}

void Player::arm_ahead() {
    cancel_timer(&ahead_timer_);
    if (ahead_asked_ || state_ != PlayerState::Playing || length_samples_ == 0 || sample_rate_ <= 0) return;
    const WhenSongEnds mode = when_song_ends();
    if (mode == WhenSongEnds::Repeat || mode == WhenSongEnds::Stop || next_is_subtune()) return;
    const std::uint64_t at = seams_.backend->position();
    const std::uint64_t left = length_samples_ > at ? length_samples_ - at : 0;
    const Millis left_ms = static_cast<Millis>(left * 1000 / static_cast<std::uint64_t>(sample_rate_));
    if (left_ms <= kFetchAheadMs) {
        ahead_due();
    } else {
        ahead_timer_ = timer(left_ms - kFetchAheadMs, &Player::arm_ahead);
    }
}

void Player::ahead_due() {
    ahead_asked_ = true;
    Entry next;
    if (!queue_.peek_next(repeat_all(), &next)) return;
    // Whether it is on the device is found out on the worker.
    const std::shared_ptr<bool> alive = alive_;
    const std::uint64_t generation = generation_;
    const std::string file = full_path(next.path);
    Files* files = seams_.files;
    Executor* ui = seams_.ui;
    const bool has_listing = listing_ && listing_->is_open();
    Listing listing;
    if (has_listing) listing = *listing_;
    const std::string path = next.path;
    seams_.worker->post([this, alive, generation, file, files, ui, has_listing, listing, path] {
        FileInfo info;
        const bool present = files && files->info(file, &info) == FileStatus::Ok;
        ListedTune listed;
        const bool in_listing = has_listing && listing.by_path(path, &listed);
        ui->post([this, alive, generation, path, present, in_listing] {
            if (*alive) ahead_checked(generation, path, present, in_listing);
        });
    });
}

void Player::ahead_checked(std::uint64_t generation, const std::string& path, bool present, bool listed) {
    if (generation != generation_ || !loaded() || present || !listed || !seams_.fetcher) return;
    if (fetch_in_flight_) return;
    Millis wait = 0;
    if (seams_.fetcher->unattended(&wait) != UnattendedFetch::Allowed) return;   // and nothing is said now
    ahead_ = Ahead();
    ahead_.path = path;
    ahead_.in_flight = true;
    ahead_.token = ++fetch_token_;
    fetch_in_flight_ = true;
    const std::uint64_t token = ahead_.token;
    const std::shared_ptr<bool> alive = alive_;
    seams_.fetcher->fetch_ahead(path, [this, alive, token](const FetchResult& result) {
        if (*alive) ahead_answer(token, result);
    });
}

void Player::ahead_answer(std::uint64_t token, const FetchResult& result) {
    if (token == fetch_token_) fetch_in_flight_ = false;
    if (token != ahead_.token || !ahead_.in_flight) return;
    // Remembered for the entry it is for; the player's state is not touched.
    ahead_.in_flight = false;
    ahead_.done = true;
    ahead_.fetched = result.fetched;
    ahead_.message = result.fetched ? "" : (result.message.empty() ? "MSG_FETCH_NETWORK" : result.message);
}

void Player::tick() {
    tick_timer_ = 0;
#if SPZ_PART_TOO_SLOW
    if (state_ != PlayerState::Playing) return;
    const std::uint64_t underruns = seams_.backend->underruns();
    const Millis now = seams_.clock->now_ms();
    if (underruns > last_underruns_) slow_seconds_.push_back(now);
    last_underruns_ = underruns;
    slow_seconds_.erase(std::remove_if(slow_seconds_.begin(), slow_seconds_.end(),
                                       [now](Millis at) { return at < now - kSlowWindowMs; }),
                        slow_seconds_.end());
    const std::uint64_t generation = generation_;
    if (!acted_slow_ && static_cast<int>(slow_seconds_.size()) >= kSlowSeconds) too_slow();
    if (generation == generation_ && state_ == PlayerState::Playing && tick_timer_ == 0) {
        tick_timer_ = timer(1000, &Player::tick);
    }
#endif
}

void Player::too_slow() {
    acted_slow_ = true;
    slow_seconds_.clear();
    const EngineChoice choice = settings_ ? settings_->engine : EngineChoice::Auto;
    if (engine_ == EngineKind::Accurate && choice == EngineChoice::Auto) {
        light_from_chips_ = light_from_chips_ == 0 ? chips_ : std::min(light_from_chips_, chips_);
        notice("MSG_SWITCHED_LIGHT");
        reload_light();
    } else if (engine_ == EngineKind::Accurate) {
        notice("MSG_TOO_HEAVY");
        offer_until_ = seams_.clock->now_ms() + kLightOfferMs;
    } else {
        notice("MSG_CANNOT_KEEP_UP");
    }
}

void Player::reload_light() {
    light_for_tune_ = true;
    offer_until_ = -1;
    unload();
    load_current(subtune_);
}

// ---- The keys ----------------------------------------------------------------------------------------------------------

void Player::play(const std::vector<Entry>& queue, std::size_t chosen) {
    if (queue.empty() || chosen >= queue.size()) return;
    if (refuse_while_off()) return;
    unload();
    queue_.set(queue, chosen, shuffle_on());
    go_to_entry(Reach::Key, 0);
}

void Player::play_pause() {
    switch (state_) {
        case PlayerState::Idle:
        case PlayerState::Failed:
            if (!has_entry_ || queue_.empty()) return;
            if (refuse_while_off()) return;
            reach_ = Reach::Key;
            bad_files_ = 0;
            present_only_ = false;
            passed_ = 0;
            notice_needed_said_ = false;
            just_fetched_ = false;
            load_current(tune_known_ && subtune_ > 0 ? subtune_ : 0);
            break;
        case PlayerState::Playing:
            seams_.backend->pause();
            state_ = PlayerState::Paused;
            paused();
            notify();
            break;
        case PlayerState::Paused:
            seams_.backend->resume();
            state_ = PlayerState::Playing;
            started(false);
            notify();
            break;
        default:
            break;
    }
}

void Player::stop() {
    if (!has_entry_ || state_ == PlayerState::Idle) return;
    unload();
    give_up_fetches();
    new_generation();
    state_ = PlayerState::Idle;
    message_.clear();
    notify();
}

void Player::next_tune() {
    if (queue_.empty() || !has_entry_) return;
    if (refuse_while_off()) return;
    if (!queue_.next(repeat_all())) {           // at the last entry: playback stops
        if (state_ == PlayerState::Idle) return;
        unload();
        give_up_fetches();
        new_generation();
        state_ = PlayerState::Idle;
        message_.clear();
        notify();
        return;
    }
    unload();
    go_to_entry(Reach::Step, 0);
}

void Player::previous_tune() {
    if (queue_.empty() || !has_entry_) return;
    if (refuse_while_off()) return;
    queue_.previous();
    unload();
    go_to_entry(Reach::Step, 0);
}

void Player::next_subtune() {
    if (queue_.empty() || !has_entry_) return;
    const int at = state_ == PlayerState::Loading && want_subtune_ > 0 ? want_subtune_ : subtune_;
    if (!tune_known_ || at < 1 || at >= subtunes_) {
        next_tune();
        return;
    }
    if (refuse_while_off()) return;
    unload();
    reach_ = Reach::Step;
    bad_files_ = 0;
    load_current(at + 1);
}

void Player::previous_subtune() {
    if (queue_.empty() || !has_entry_) return;
    const int at = state_ == PlayerState::Loading && want_subtune_ > 0 ? want_subtune_ : subtune_;
    if (!tune_known_ || at <= 1) {
        previous_tune();
        return;
    }
    if (refuse_while_off()) return;
    unload();
    reach_ = Reach::Step;
    bad_files_ = 0;
    load_current(at - 1);
}

void Player::restart() {
#if SPZ_PART_RESTART
    if (!has_entry_ || queue_.empty()) return;
    switch (state_) {
        case PlayerState::Idle:
        case PlayerState::Failed:
        case PlayerState::Playing:
        case PlayerState::Paused:
            if (refuse_while_off()) return;
            unload();
            reach_ = Reach::Key;
            bad_files_ = 0;
            just_fetched_ = false;
            load_current(tune_known_ && subtune_ > 0 ? subtune_ : 0);
            break;
        default:
            break;
    }
#endif
}

void Player::use_light_engine() {
#if SPZ_PART_TOO_SLOW
    if (state_ != PlayerState::Playing || offer_until_ < 0 || seams_.clock->now_ms() >= offer_until_) return;
    reload_light();
#endif
}

void Player::toggle_voice(int voice) {
#if SPZ_PART_VOICES
    if (!loaded() || voice < 1 || voice > 3) return;
    bool& muted = muted_[chip_ - 1][voice - 1];
    muted = !muted;
    if (seams_.backend->can().mute) seams_.backend->set_muted(chip_ - 1, voice - 1, muted);
    controls_sent_ = true;
    notify();
#else
    (void)voice;
#endif
}

void Player::toggle_filter() {
#if SPZ_PART_VOICES
    if (!loaded()) return;
    bool& on = filter_on_[chip_ - 1];
    on = !on;
    if (seams_.backend->can().filter) seams_.backend->set_filter(chip_ - 1, on);
    controls_sent_ = true;
    notify();
#endif
}

void Player::next_chip() {
#if SPZ_PART_VOICES
    if (!tune_known_ || chips_ <= 1) return;
    chip_ = chip_ % chips_ + 1;
    notify();
#endif
}

void Player::set_skip(bool on) {
#if SPZ_PART_SKIP
    if (on == skipping_) return;
    if (on && state_ != PlayerState::Playing) return;
    if (!seams_.backend->can().skip) return;
    skipping_ = on;
    seams_.backend->set_skip(on);
#else
    (void)on;
#endif
}

void Player::restore(const std::vector<Entry>& queue, std::size_t place) {
    if (queue.empty()) return;
    if (place >= queue.size()) place = 0;
    unload();
    give_up_fetches();
    new_generation();
    queue_.set(queue, place, false);
    if (shuffle_on()) queue_.set_shuffle(true);
    entry_ = queue_.current();
    has_entry_ = true;
    state_ = PlayerState::Idle;
    message_.clear();
    loaded_path_ = entry_.path;
    reset_tune_choices();
    tune_known_ = false;
    subtune_ = 0;
    subtunes_ = 0;
    length_ms_ = 0;
    notify();
}

// ---- What it reports ---------------------------------------------------------------------------------------------------

PlayerStatus Player::status() const {
    PlayerStatus out;
    out.state = state_;
    out.has_entry = has_entry_;
    out.entry = entry_;
    out.message = state_ == PlayerState::Failed ? message_ : "";
    out.tune_known = tune_known_;
    out.info = info_;
    out.subtune = tune_known_ ? subtune_ : 0;
    out.subtunes = subtunes_;
    out.length_ms = length_ms_;
    if (loaded() && sample_rate_ > 0) {
        out.elapsed_ms =
            static_cast<Millis>(seams_.backend->position() * 1000 / static_cast<std::uint64_t>(sample_rate_));
    }
    out.sample_rate = sample_rate_;
    out.engine = engine_;
    out.chips = chips_;
    out.model = model_;
    out.clock = c64_clock_;
    out.model_forced = model_forced_;
    out.clock_forced = clock_forced_;
    for (int chip = 0; chip < 3; ++chip) {
        out.filter_on[chip] = filter_on_[chip];
        for (int voice = 0; voice < 3; ++voice) out.muted[chip][voice] = muted_[chip][voice];
    }
    out.chip = chip_;
    return out;
}

const std::vector<Entry>& Player::queue() const { return queue_.order(); }
std::size_t Player::place() const { return queue_.place(); }

// ---- The backend's events --------------------------------------------------------------------------------------------

void Player::song_ended() {
    if (state_ != PlayerState::Playing) return;
    if (playing_off_) {
        stop_playback("MSG_UNPACKING");
        return;
    }
    switch (when_song_ends()) {
        case WhenSongEnds::Repeat:
            reach_ = Reach::Unattended;
            load_current(subtune_);
            return;
        case WhenSongEnds::Stop:
            stop_playback("");
            return;
        default:
            break;
    }
    if (next_is_subtune()) {
        reach_ = Reach::Unattended;
        load_current(subtune_ + 1);
        return;
    }
    present_only_ = false;
    passed_ = 0;
    next_unattended();
}

void Player::device_lost() {}

void Player::device_back() {}

void Player::device_gone() {
    if (state_ != PlayerState::Playing) return;
    seams_.backend->pause();
    state_ = PlayerState::Paused;
    paused();
    notice("MSG_NO_SOUND");
    notify();
}

void Player::failed(const std::string& reason) {
    if (state_ != PlayerState::Loading && !loaded()) return;
    new_generation();
    skipping_ = false;
    fail(reason.empty() ? "MSG_ENGINE_REFUSED" : reason);
}

}  // namespace spz
