// SPDX-License-Identifier: GPL-3.0-or-later
// The player (SPEC-060 to SPEC-069, SPEC-079): one state machine, on the UI thread. It owns the queue,
// decides what plays next, and makes sound only through a SoundBackend (SPEC-025). It reads files on a
// worker, asks a TuneFetcher for tunes that are not on the device, and tells a listener what changed.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "spz/clock.h"
#include "spz/engine.h"
#include "spz/engine_choice.h"
#include "spz/executor.h"
#include "spz/fetcher.h"
#include "spz/files.h"
#include "spz/lifetime.h"
#include "spz/listing.h"
#include "spz/player_fetcher.h"
#include "spz/player_roms.h"
#include "spz/queue.h"
#include "spz/settings.h"
#include "spz/shuffle.h"
#include "spz/sound_backend.h"
#include "spz/tune.h"
#include "spz/types.h"

namespace spz {

// SPEC-060's states.
enum class PlayerState {
    Idle,       // nothing is loaded; there may be a current entry
    Waiting,    // the current entry is absent and was reached by stepping: the app waits before fetching
    Fetching,   // the current entry's file is being downloaded
    Loading,    // the file is being read, checked (SPEC-203) and given to the backend
    Playing,    // the backend is producing sound
    Paused,     // the backend is loaded and stopped at a known sample
    Failed,     // the current entry could not be fetched or loaded; a message says why
};

// The numbers of SPEC-060, SPEC-069, SPEC-079 and SPEC-141 that the player keeps time by.
const Millis kStepWaitMs = 700;        // a tune stepped to is requested once it has been current this long
const Millis kFailedWaitMs = 2000;     // unattended, a failed entry is left after this long
const int kBadFilesInARow = 5;         // after this many, unattended playback stops
// kFetchAheadMs (spz/fetcher.h): the next entry is fetched when the song has this long to go, or less. One
// definition for the two headers, so that a file may include both.
const Millis kLightOfferMs = 10000;    // how long MSG_TOO_HEAVY's offer of the L key stands
const int kSlowSeconds = 3;            // underruns in this many separate seconds ...
const Millis kSlowWindowMs = 10000;    // ... within this long mean the engine cannot keep up

// What the now-playing screen and the mini player show (SPEC-096, SPEC-109), and what the tests read.
struct PlayerStatus {
    PlayerState state = PlayerState::Idle;
    bool has_entry = false;         // false until something has been played or restored
    Entry entry;                    // the current entry; it stays after playback stops
    std::string message;            // while Failed: the identifier of the failure's message (SPEC-069)

    bool tune_known = false;        // the tune's header has been read: `info` holds it, as when it last played
    TuneInfo info;
    int subtune = 0;                // the subtune playing or to be played, from 1; 0 while it is not known
    int subtunes = 0;               // how many the tune has: from its header, or for an absent tune from the
                                    // listing; 0 while it is not known
    Millis length_ms = 0;           // the subtune's length (SPEC-063); for an absent tune, the listing's first;
                                    // 0: no known length
    Millis elapsed_ms = 0;          // the backend's position() divided by the rate (SPEC-064)
    int sample_rate = 0;            // the rate the subtune was loaded with (SPEC-073)

    EngineKind engine = EngineKind::Light;      // the engine the subtune was loaded with (SPEC-004)
    int chips = 1;                              // SPEC-075
    SidModel model = SidModel::Mos6581;         // the model and clock in effect: the header's, the library's
    C64Clock clock = C64Clock::Pal;             // default (6581, PAL) when it does not say, or the forced one
    bool model_forced = false;                  // SPEC-077
    bool clock_forced = false;

    bool muted[3][3] = {};                      // [chip][voice], both from 0: true for a muted voice (SPEC-074)
    bool filter_on[3] = {true, true, true};     // for each chip
    int chip = 1;                               // the chip the voice keys and the voice display are on, from 1
};

// What the player tells the rest of the app, on the UI thread.
class PlayerListener {
public:
    virtual ~PlayerListener() = default;
    // A notice to show (SPEC-113): an identifier of SPEC-116, and what it is about.
    virtual void notice(const std::string& id, const std::map<std::string, std::string>& about) = 0;
    // Something that status() reports has changed.
    virtual void changed() = 0;
};

// What the player works through. None of these is owned by the player.
//
// Whether an entry can be played is found out when it is to be played, and off the UI thread (SPEC-022):
// it is present when a file is at its path (under the collection root, or at the full path of a
// `file://` entry); else absent when the listing has its path, and then the fetcher is asked for it; else
// missing (SPEC-069). A playlist resolves its entries (SPEC-173) before it gives them to play().
struct PlayerSeams {
    SoundBackend* backend = nullptr;
    Clock* clock = nullptr;
    Files* files = nullptr;
    Executor* worker = nullptr;         // the quick worker (SPEC-022): a tune's file is read, checked and given
                                        // to the backend by a job posted here
    Executor* ui = nullptr;             // the UI thread: a worker's result comes back as a job posted here
    TuneFetcher* fetcher = nullptr;     // null: absent tunes cannot be fetched
    PlayerListener* listener = nullptr;
};

struct PlayerOptions {
    std::string root;                   // the collection root, as a full path (SPEC-040)
    bool has_seed = false;              // the test option --seed N (SPEC-253)
    std::uint64_t seed = 0;             // the shuffle seed; without has_seed it is clock->wall_time_s() at start
};

class Player : public SoundBackendListener {
public:
    // Makes itself the backend's listener. `settings` is read whenever a setting is needed, so a change
    // takes effect at the next thing it governs; settings_changed() is for what takes effect at once.
    Player(const PlayerSeams& seams, const PlayerOptions& options, const Settings* settings);
    ~Player() override;

    // What the player needs from elsewhere, each of which can arrive or change while the app runs.
    void set_listing(const Listing* listing);             // null or not open: no listing, every length the default
    void set_speed_factors(const SpeedFactors& factors);  // SPEC-072
    void set_roms(const Roms& roms);                      // SPEC-068
    // The complete download switches playing off (SPEC-144): while it is, every event that would load a
    // tune raises the notice MSG_UNPACKING and does nothing else.
    void set_playing_off(bool off);
    // A setting has been changed. `shuffle` takes effect on the queue at once (SPEC-065).
    void settings_changed();

    // ---- The events of SPEC-060's table that a key makes -------------------------------------------------
    // "Play an entry" (Enter on a tune): the queue becomes a copy of `queue` (SPEC-062) and the entry at
    // `chosen`, counted from 0, is played: loaded if it is present, and fetched first, at once, if it is
    // absent.
    void play(const std::vector<Entry>& queue, std::size_t chosen);
    void play_pause();
    void stop();
    void next_tune();                   // SPEC-062
    void previous_tune();
    void next_subtune();                // SPEC-061
    void previous_subtune();
    void restart();                     // the part `restart` (SPEC-066)
    void use_light_engine();            // the L key (SPEC-079, the part `too-slow`)

    // ---- Sound controls ------------------------------------------------------------------------------------
    // The part `voices` (SPEC-074). They act on the chip status().chip names, while a tune is loaded, and
    // hold for the tune: across its subtunes and a restart. Another tune puts everything on again.
    void toggle_voice(int voice);       // 1 to 3
    void toggle_filter();
    void next_chip();                   // round the chips the tune uses
    // The part `skip` (SPEC-066): on while the key is held.
    void set_skip(bool on);

    // ---- What is remembered between runs (SPEC-131) --------------------------------------------------------
    // Sets the queue and the current entry without playing: the player is Idle with that entry.
    void restore(const std::vector<Entry>& queue, std::size_t place);

    // ---- What it reports -----------------------------------------------------------------------------------
    PlayerStatus status() const;
    // The queue in the order it is played, and the place of the current entry in it, from 0.
    const std::vector<Entry>& queue() const;
    std::size_t place() const;

    // ---- The backend's events (SPEC-025), delivered on the UI thread ---------------------------------------
    void song_ended() override;
    void device_lost() override;
    void device_back() override;
    void device_gone() override;
    void failed(const std::string& reason) override;

private:
    // How the current entry was reached, which decides what is done when it is absent or fails (SPEC-069,
    // SPEC-141): a key that plays it at once (Enter, Play/Pause, Restart), a key that steps to it (it is
    // fetched after the wait), or playback by itself (a song's end, or the unattended wait).
    enum class Reach { Key, Step, Unattended };

    // What became of a fetch-ahead (SPEC-141), for the entry it was for.
    struct Ahead {
        std::string path;                 // "" when there is none
        bool in_flight = false;
        bool done = false;                // answered: `fetched` and `message` say how
        bool fetched = false;
        std::string message;
        std::uint64_t token = 0;
    };

    struct LoadJob;                       // what the worker is given, and what it gives back
    struct LoadResult;
    // The worker's part of a load. False, with nothing to post, when the player has moved on before it began.
    static bool run_load(const LoadJob& job, Files* files, SoundBackend* backend,
                         const std::atomic<std::uint64_t>& current, LoadResult* out);

    void notify();
    void notice(const std::string& id);
    // Moves to a new generation: every job, answer and timer of the one before is left to come to nothing.
    void new_generation();
    void cancel_timer(Clock::TimerId* id);
    Clock::TimerId timer(Millis ms, void (Player::*fn)());
    bool loaded() const;                  // Playing or Paused: the backend has the subtune
    void unload();                        // stop() to the backend if it has a subtune
    void give_up_fetches();               // cancels the fetches in flight, and forgets the fetch-ahead
    bool refuse_while_off();              // SPEC-144: true, with MSG_UNPACKING, while playing is off

    // The current entry becomes the queue's current one, and it is played: `subtune` 0 for the entry's own.
    void go_to_entry(Reach reach, int subtune);
    // Loads the current entry's file again: Loading, and a job for the worker.
    void load_current(int subtune);
    void loaded_result(const LoadResult& result);
    void absent(const LoadResult& result);
    void missing();
    void request(bool attended);          // asks the fetcher for the current entry: Fetching
    void ask_unattended();                // an unattended entry that is absent: may it be requested?
    void fetch_answer(std::uint64_t generation, std::uint64_t token, const FetchResult& result);
    void ahead_answer(std::uint64_t token, const FetchResult& result);
    void ahead_checked(std::uint64_t generation, const std::string& path, bool present, bool listed);
    void fail(const std::string& message);
    void stop_playback(const std::string& notice_id);   // → Idle, from playback by itself
    bool drives_backend() const;                        // the last player made on the backend is this one
    // Playback by itself moves on: to the next entry (SPEC-062, SPEC-065), passing over what it must.
    void pass_over();
    void next_unattended();
    void wait_over();                     // Waiting is over
    void unattended_wait_over();          // SPEC-069's 2 s in Failed are over
    void started(bool fresh);             // the subtune is playing (from its start, or again after a pause)
    void paused();                        // Playing is left: its timers stop
    bool shuffle_on() const;              // the settings, as far as their parts are built (SPEC-115)
    bool repeat_all() const;
    WhenSongEnds when_song_ends() const;
    void arm_ahead();
    void ahead_due();
    void tick();                          // SPEC-079: once a second while Playing
    void too_slow();
    void reload_light();
    void send_controls();                 // mutes and filters to the backend after a load
    void reset_tune_choices();
    int entry_subtune() const;            // the subtune the current entry names, if the tune has it; else 0
    bool next_is_subtune() const;         // what follows the song by SPEC-065 is the tune's next subtune
    std::string full_path(const std::string& path) const;

    PlayerSeams seams_;
    PlayerOptions options_;
    const Settings* settings_ = nullptr;
    Lifetime lifetime_;
    std::shared_ptr<std::atomic<std::uint64_t>> generation_shared_;   // read by the worker's jobs

    ShuffleGenerator generator_;
    Queue queue_{&generator_};
    const Listing* listing_ = nullptr;
    SpeedFactors factors_;
    Roms roms_;
    bool playing_off_ = false;

    PlayerState state_ = PlayerState::Idle;
    bool has_entry_ = false;
    Entry entry_;
    std::string message_;
    bool tune_known_ = false;
    TuneInfo info_;
    int subtune_ = 0;
    int subtunes_ = 0;
    Millis length_ms_ = 0;
    int sample_rate_ = 0;
    EngineKind engine_ = EngineKind::Light;
    int chips_ = 1;
    SidModel model_ = SidModel::Mos6581;
    C64Clock c64_clock_ = C64Clock::Pal;
    bool model_forced_ = false, clock_forced_ = false;

    std::uint64_t generation_ = 0;
    Reach reach_ = Reach::Key;
    int want_subtune_ = 0;                // the subtune the load in progress is for; 0: the entry's own
    bool just_fetched_ = false;           // the file was fetched, so it must be there now
    std::string loaded_path_;             // the tune whose choices (mutes, engine) hold
    std::uint64_t length_samples_ = 0;    // what set_length() was given
    bool skipping_ = false;
    Clock::TimerId wait_timer_ = 0, failed_timer_ = 0, ahead_timer_ = 0, tick_timer_ = 0;

    // Fetching.
    bool fetch_in_flight_ = false;        // fetch() or fetch_ahead() asked, not answered or cancelled
    std::uint64_t fetch_token_ = 0;       // of the latest fetch() or fetch_ahead()
    Ahead ahead_;
    bool ahead_asked_ = false;            // the fetch-ahead of this song has been decided

    // Playback by itself (SPEC-069, SPEC-143).
    int bad_files_ = 0;
    bool failed_for_network_ = false;     // the failure in Failed is one of SPEC-143's
    bool present_only_ = false;           // after a fetch that failed for the network: absent entries are passed over
    std::size_t passed_ = 0;              // entries passed over in a row
    bool notice_needed_said_ = false;

    // Choices that hold for the tune (SPEC-074, SPEC-079).
    bool muted_[3][3] = {};
    bool filter_on_[3] = {true, true, true};
    int chip_ = 1;
    bool controls_sent_ = false;          // the backend was told something is off
    bool light_for_tune_ = false;
    bool acted_slow_ = false;
    int light_from_chips_ = 0;            // after MSG_SWITCHED_LIGHT, `auto` uses sidlite from this many chips
    Millis offer_until_ = -1;             // MSG_TOO_HEAVY's offer of the L key stands until then
    std::uint64_t last_underruns_ = 0;
    std::vector<Millis> slow_seconds_;    // when the underrun counter was seen to rise
};

}  // namespace spz
