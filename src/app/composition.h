// SPDX-License-Identifier: GPL-3.0-or-later
// The composition (SPEC-020 to SPEC-024): the one place that makes every object of `core`, gives it its
// seams, joins the objects together, and starts and stops them in the order of SPEC-200 and SPEC-201.
//
// It is made by the App, on the UI thread, once the App's threads run; the screens reach what it makes
// through ui::Services, which it fills in. Each area has its own file, and its own section below:
//
//   compose_keep.cpp     settings, state, folders, the screen while playing, the measurements  (SPEC-130 ...)
//   compose_hvsc.cpp     the listing, the notice gate, jobs, the fetcher, set-up, releases, the download
//   compose_browse.cpp   the collection, the browser's model, the empty screen's watch       (SPEC-040 ...)
//   compose_play.cpp     the sound backend, the player and its fetcher, the volume, the mini player
//   compose_search.cpp   the index                                                            (SPEC-160 ...)
//   compose_lists.cpp    playlists, favourites, "Fetch missing"                               (SPEC-170 ...)
//
// HOW TO ADD TO IT (wave 3b and later; see also the guide at the top of ui/screen.h)
//
// - An object of `core`: a member in its area's section below, made in its area's make_...() function, and
//   given to the screens by a field of ui::Services (ui/screen.h, its area's section). Make it after what it
//   needs: the make_...() functions run in the order of Composition::Composition().
// - An action that is the same on every screen ("play_pause", "volume_up"): app()->on_action(), in the
//   area's make_...() function. An action of one screen belongs to that screen's on_action().
// - Something that shows on every screen: app()->add_describer() for its fields of the description,
//   app()->add_top_right() for its piece of the top line, app()->every() to redraw what changes by itself.
// - A job (SPEC-147): it takes `jobs_` while it runs; its notices go through `notices_` (a NoticeSink); the
//   job mark is the HVSC area's describer and top-right piece. Its worker is app()->long_worker() (set-up,
//   update, download) or the fetcher (single tunes).
// - Work at start (SPEC-200, step 5): add it to start() in its place in the order, on its worker. Work at
//   exit (SPEC-201): add it to stop() in its place.
// - Something to do when the listing changes (HVSC set up, updated or downloaded): add it to
//   listing_changed().
// Each area edits its own file and its own section; the order of the calls in composition.cpp is the one
// place they share.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "spz/browser_model.h"
#include "spz/collection.h"
#include "spz/complete_download.h"
#include "spz/engine_choice.h"
#include "spz/favourites.h"
#include "spz/fetcher.h"
#include "spz/folders.h"
#include "spz/hvsc_setup.h"
#include "spz/index.h"
#include "spz/job.h"
#include "spz/listing.h"
#include "spz/metrics.h"
#include "spz/notice_gate.h"
#include "spz/player.h"
#include "spz/playlist_fetch.h"
#include "spz/playlist_resolve.h"
#include "spz/playlists.h"
#include "spz/releases.h"
#include "spz/screen_off.h"
#include "spz/settings_file.h"
#include "spz/state_file.h"

namespace spz::platform {
class SoftwareSound;
}

namespace spz::app {

class Composition {
public:
    // SPEC-200, steps 2 and 4: reads the settings and the state, and makes everything. Nothing here waits
    // for a file but the two kept files and the listing's header; nothing uses the network.
    Composition(App* app, const Seams& seams);
    ~Composition();
    Composition(const Composition&) = delete;
    Composition& operator=(const Composition&) = delete;

    // SPEC-200, step 5: what the workers do once the first screen is up.
    void start();
    // SPEC-201, its middle steps, before the App stops its threads: stops the sound, puts the brightness
    // back, writes state.ini, and tells whatever waits on the outside world to stop.
    void stop();
    // SPEC-241: after the threads have stopped: exit_ms, and the file --metrics names.
    void write_metrics(Millis exit_ms);
    // SPEC-070: as App::render_turn().
    void render();

private:
    struct RootScope;                 // everything that depends on the collection root: below
    App* app() const { return app_; }
    ui::Services& services() const { return app_->services(); }

    // ---- keep: compose_keep.cpp ---------------------------------------------------------------------------
    void make_keep();
    CollectionRoot find_root() const; // SPEC-040: the collection root, from HVSC_BASE, the setting or ~/C64Music
    void keep_at_start();             // ROMs, measurements that every run writes
    void setting_changed(const std::string& key);
    Folders folders_;
    std::unique_ptr<SettingsFile> settings_;
    std::unique_ptr<StateFile> state_;
    Metrics metrics_;
    std::unique_ptr<ScreenOff> screen_off_;

    // ---- HVSC: compose_hvsc.cpp -----------------------------------------------------------------------------
    void make_hvsc();
    // SPEC-142: the listing at `listing_path` opened into `listing`, or built from `database` first if it
    // does not fit it. On the long worker. False only if the database is there and refused
    // (MSG_LENGTHS_UNREADABLE); with no database the root has no listing.
    static bool open_listing(Files* files, Listing* listing, const std::string& listing_path,
                             const std::string& database, const std::string& cache);
    // Where a scope's fetcher's answers go: those for the tune to be played and the fetch-ahead, and those
    // for "Fetch missing" (FetchWhy::Playlist).
    void route_fetches_to_player(RootScope* scope, FetchListener* listener);
    void route_fetches_to_lists(RootScope* scope, FetchListener* listener);
    void hvsc_at_start();            // the marker of SPEC-144, then the listing (SPEC-142), on the long worker
    void cleanup_at_start();          // SPEC-202, and SPEC-145's conditions for the measurements
    // The listing at listing.bin has changed (set up, updated, downloaded): opened again, on the long
    // worker, in place (readers on other threads keep the listing they took, SPEC-022), and then every part
    // that shows what it holds is told.
    void listing_changed(RootScope* scope);
    std::string listing_file() const;
    std::string database_file(const RootScope* scope) const;
    void make_job_mark();             // SPEC-147: `job`, `mini.job` and the job mark on every screen
    struct GateRecord;                // NoticeRecord: notice_accepted in state.ini
    struct GatePanel;                 // NoticePanel: the `notice-panel` panel
    struct Notices;                   // NoticeSink: the app's notices
    struct HvscEvents;                // HvscListener and DownloadListener
    struct Arrivals;                  // NewTuneListener: the index and the browser hear of a tune fetched
    struct FetchRouter;               // FetchListener: the fetcher's answers, to the player's or the playlists'
    // The parts of HVSC that do not depend on the root.
    std::shared_ptr<GateRecord> gate_record_;
    std::shared_ptr<GatePanel> gate_panel_;
    std::unique_ptr<NoticeGate> gate_;
    std::shared_ptr<Notices> notices_;
    Jobs jobs_;
    // The guard of SPEC-147's jobs: a job of set-up, update or download whose work on the long worker throws
    // is ended there all the same, so that it never stays running (A8). Given to those three as their
    // long worker.
    struct JobGuard;
    std::shared_ptr<JobGuard> job_guard_;
    void end_failed_job();            // on the UI thread: the job of set-up, update or download whose work threw
    // The scope's HVSC parts: its listing, fetcher, set-up, releases and download.
    void make_hvsc_scope(RootScope* scope);

    // ---- the collection and the browser: compose_browse.cpp -----------------------------------------------
    void make_browse();
    void browse_at_start();           // the browser's place (SPEC-131), and its first folder
    void watch_empty_root();          // SPEC-048: every 5 s while the empty screen shows
    void refresh_browser();           // what the collection shows has changed: the browser lists afresh
    void collection_changed();        // SPEC-040: the setting `collection` has changed
    void make_browse_scope(RootScope* scope);
    struct BrowserEvents;             // BrowserListener
    bool first_listing_seen_ = false;
    bool watching_root_ = false;

    // ---- playing: compose_play.cpp ----------------------------------------------------------------------------
    void make_play();
    void make_player(RootScope* scope);   // the scope's player
    // What every player is given as it is made, kept here for the players of later roots.
    void set_roms(const Roms& roms);
    void set_speed_factors(const SpeedFactors& factors);
    Roms roms_;
    SpeedFactors speed_factors_;
    void play_at_start();             // the current entry and the queue (SPEC-131), and SPEC-135
    void measure_engines();           // SPEC-072, once the browser has shown for 3 s
    void measure_engines_now(Files* files, const std::string& cache, int rate);
    struct PlayerEvents;              // PlayerListener
    struct PlayerFetcher;             // TuneFetcher, on the fetcher of SPEC-141
    struct StartSources;              // QueueSources and PlayerAtStart, for SPEC-131 and SPEC-135
    std::unique_ptr<platform::SoftwareSound> sound_;
    SoundBackend* backend_ = nullptr;
    std::shared_ptr<PlayerEvents> player_events_;

    // ---- search: compose_search.cpp -------------------------------------------------------------------------
    void make_search();
    void search_at_start();           // SPEC-161: the index checked and brought up to date
    void make_search_scope(RootScope* scope);
    // The index of the scope in use built on the long worker (SPEC-161), the cache folder made first. It
    // stops at its next batch when the app leaves or its scope is retired (a new root); the next build, of
    // the new root's index, rebuilds. Then `done`, if given, has how it went, on the long worker; without
    // it, the build's notice (MSG_INDEX_READY when the reason calls for it) is raised and everything is
    // drawn again, unless the scope has been retired meanwhile.
    void build_index(IndexReason reason, std::function<void(const IndexRun&)> done = nullptr);
    struct OwnTunesInIndex;           // OwnTunes, for the playlists' resolver

    // ---- playlists and favourites: compose_lists.cpp -------------------------------------------------------
    void make_lists();
    void lists_at_start();            // the playlists, read on the quick worker
    void make_lists_scope(RootScope* scope);
    struct ListsFetcher;             // PlaylistTuneFetcher, on the fetcher of SPEC-141
    struct ListsEvents;               // FetchMissingListener

    // ---- the self-test and the measurements: compose_selftest.cpp -------------------------------------------
    void make_selftest();             // the request file (SPEC-260), the results file, the job mark of the benchmark
    void selftest_at_start();         // in the ready job: the step asked for begins
    void selftest_at_exit();          // in stop(): the keys of SPEC-241 that belong to no step
    void device_measurements();       // SPEC-241: what only the device can say
    struct SelfTest;                  // ui::SelfTestView: the steps, the key script, the measurements they take
    std::shared_ptr<SelfTest> selftest_;

    // ---- the collection root's scope (SPEC-040) ---------------------------------------------------------------
    // Everything that depends on the collection root is made for one root, in one RootScope. When the setting
    // `collection` names another root (collection_changed()), a new scope is made and the old one is retired
    // whole: its player stopped, its fetches and its index build cancelled, its job asked to stop, and its
    // listeners deaf. A retired scope is kept until the app ends, since jobs on the workers may still hold
    // what it made. Each part of it is made by its area's make_..._scope(), in the order of make_scope().
    struct RootScope {
        CollectionRoot root;
        bool live = true;                                     // false once retired (UI thread only)
        // HVSC: compose_hvsc.cpp. The listing is opened again in place when it changes (SPEC-142).
        std::unique_ptr<Listing> listing;
        std::shared_ptr<HvscEvents> hvsc_events;
        std::shared_ptr<Arrivals> arrivals;
        std::unique_ptr<Releases> releases;
        std::unique_ptr<Fetcher> fetcher;
        std::shared_ptr<FetchRouter> fetch_router;
        std::unique_ptr<HvscSetup> setup;
        std::unique_ptr<CompleteDownload> download;
        // The collection and the browser: compose_browse.cpp.
        std::unique_ptr<Collection> collection;
        std::shared_ptr<BrowserEvents> browser_events;
        std::unique_ptr<BrowserModel> browser;
        // Search: compose_search.cpp.
        std::unique_ptr<Index> index;
        std::shared_ptr<std::atomic<bool>> index_live;       // false once the scope is retired
        std::shared_ptr<OwnTunes> own_tunes;
        // Playlists: compose_lists.cpp. The resolver of entries works on the root, and so the playlists do.
        std::unique_ptr<EntryResolver> resolver;
        std::unique_ptr<Playlists> playlists;
        std::unique_ptr<Favourites> favourites;
        std::shared_ptr<ListsFetcher> lists_fetcher;
        std::shared_ptr<ListsEvents> lists_events;
        std::unique_ptr<FetchMissing> fetch_missing;
        // Playing: compose_play.cpp.
        std::shared_ptr<PlayerFetcher> player_fetcher;
        std::unique_ptr<Player> player;
    };
    std::unique_ptr<RootScope> make_scope(const CollectionRoot& root);
    void retire(RootScope* scope);
    std::unique_ptr<RootScope> scope_;
    std::vector<std::unique_ptr<RootScope>> retired_scopes_;

    // Runs `fn` on the UI thread, unless the app has begun to stop by then. The composition outlives the
    // workers (the App stops them before it goes), so a job may hold `this`.
    void on_ui(std::function<void()> fn);
    bool running() const { return running_.load(); }
    // Runs `fn` on the UI thread once `ms` of the clock have passed, unless the app has begun to stop: a
    // timer of the composition's own, which stop() cancels.
    void after(Millis ms, std::function<void()> fn);
    std::vector<Clock::TimerId> timers_;

    App* app_;
    Seams seams_;
    std::atomic<bool> running_{true};
};

// What the composition's parts share: words for the screen description.
std::string time_text(Millis ms);          // "3:55": minutes and seconds, cut down (FIND-024)

}  // namespace spz::app
