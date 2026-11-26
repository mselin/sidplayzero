// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, HVSC: the listing (SPEC-142), the notice and its gate (SPEC-152), jobs (SPEC-147), the
// fetcher of single tunes (SPEC-141) with Releases as its ChangedTunes (SPEC-148), setting HVSC up
// (SPEC-140) and the complete download (SPEC-144), and what they do at start.
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "app/composition.h"
#include "spz/keep_start.h"
#include "spz/keymap.h"
#include "ui/hvsc_screen.h"
#include "ui/panel.h"

namespace spz::app {

// SPEC-152: notice_accepted is kept in state.ini.
struct Composition::GateRecord : NoticeRecord {
    explicit GateRecord(StateFile* file) : state(file) {}
    bool notice_accepted() const override { return state->state().notice_accepted; }
    void accept_notice() override { state->set_notice_accepted(true); }
    StateFile* state;
};

// SPEC-114: the notice panel. Enter accepts, ESC cancels.
struct Composition::GatePanel : NoticePanel {
    explicit GatePanel(App* frame) : app(frame) {}
    void open_notice_panel(const std::string& text) override {
        NoticeGate* const* slot = &this->gate;
        app->open_panel(std::make_unique<ui::TextPanel>(
            app, "notice-panel", "HVSC's notice", text, [slot] { if (*slot) (*slot)->accept(); },
            [slot] { if (*slot) (*slot)->cancel(); }));
    }
    App* app;
    NoticeGate* gate = nullptr;
};

// Where the jobs tell what happened (SPEC-113).
struct Composition::Notices : NoticeSink {
    explicit Notices(App* frame) : app(frame) {}
    void raise(const std::string& id, const std::map<std::string, std::string>& about) override {
        app->notice(id, about);
        app->redraw();
    }
    App* app;
};

// How a set-up, an update or the complete download ended, and the player's side of the download.
struct Composition::HvscEvents : HvscListener, DownloadListener, PlaySwitch {
    explicit HvscEvents(Composition* owner) : parts(owner) {}
    void hvsc_ready(int) override {
        // SPEC-140: the listing is new; if the HVSC screen or the empty screen shows, the browser opens at the
        // root, once no panel and no help is open (SPEC-113).
        parts->listing_changed();
        changed();
        App* app = parts->app();
        const bool showing = app->top_name() == "hvsc" || app->top_name() == "empty";
        if (showing || app->bottom() == "empty") {
            // Elsewhere the screen is left alone; only the empty screen under it gives its place to the browser.
            Composition* p = parts;
            app->when_no_overlay([p, showing] {
                if (p->browser_) p->browser_->show("/");
                p->app()->set_bottom("browser");
                if (showing) p->app()->open("browser");
            });
        }
    }
    void hvsc_failed(int) override { changed(); }
    void download_done() override {
        parts->listing_changed();
        changed();
    }
    void download_failed(int) override { changed(); }
    // The HVSC screen looks at HVSC again (Services::hvsc_changes).
    void changed() {
        ++parts->services().hvsc_changes;
        parts->app()->redraw();
    }
    void playing_off() override {
        if (parts->player_) parts->player_->set_playing_off(true);
    }
    void playing_on() override {
        if (parts->player_) parts->player_->set_playing_off(false);
    }
    Composition* parts;
};

// SPEC-141, SPEC-161, SPEC-043: a tune's file has arrived at its HVSC path.
struct Composition::Arrivals : NewTuneListener {
    explicit Arrivals(Composition* owner) : parts(owner) {}
    void tune_arrived(const std::string& path) override {
        if (parts->browser_) parts->browser_->tune_changed(path);
        if (Index* index = parts->index_.get()) {
            parts->app()->quick()->post([index, path] { index->tune_arrived(path); });
        }
        parts->app()->redraw();
    }
    Composition* parts;
};

// The fetcher has one listener: its answers are passed on by why they were asked.
struct Composition::FetchRouter : FetchListener {
    void fetch_started(const std::string& path) override {
        if (player) player->fetch_started(path);
    }
    void fetched(const std::string& path, FetchWhy why) override {
        if (FetchListener* to = why == FetchWhy::Playlist ? lists : player) to->fetched(path, why);
    }
    void fetch_failed(const std::string& path, FetchWhy why, const std::string& message) override {
        if (FetchListener* to = why == FetchWhy::Playlist ? lists : player) to->fetch_failed(path, why, message);
    }
    void fetch_cancelled(const std::string& path) override {
        if (player) player->fetch_cancelled(path);
    }
    FetchListener* player = nullptr;
    FetchListener* lists = nullptr;
};

void Composition::make_fetch_router() {
    fetch_router_.reset(new FetchRouter());
    fetcher_->set_listener(fetch_router_.get());
}

void Composition::route_fetches_to_player(FetchListener* listener) { fetch_router_->player = listener; }

void Composition::route_fetches_to_lists(FetchListener* listener) { fetch_router_->lists = listener; }

std::string Composition::listing_file() const { return folders_.cache + "/listing.bin"; }

std::string Composition::database_file() const { return root_.path + "/DOCUMENTS/Songlengths.md5"; }

void Composition::make_hvsc() {
    // The listing: opened on the long worker at start (hvsc_at_start), before anything reads it.
    listing_.reset(new Listing());

    gate_record_.reset(new GateRecord(state_.get()));
    gate_panel_.reset(new GatePanel(app()));
    gate_.reset(new NoticeGate(gate_record_.get(), gate_panel_.get()));
    gate_panel_->gate = gate_.get();
    notices_.reset(new Notices(app()));
    hvsc_events_.reset(new HvscEvents(this));
    arrivals_.reset(new Arrivals(this));

    ReleasesParts releases;
    releases.files = seams_.files;
    releases.http = seams_.http;
    releases.long_worker = app()->long_worker();
    releases.ui = app()->ui();
    releases.gate = gate_.get();
    releases.jobs = &jobs_;
    releases.notices = notices_.get();
    releases.settings = &settings_->settings();
    releases.root = root_.path;
    releases.cache = folders_.cache;
    releases.listener = hvsc_events_.get();
    releases_.reset(new Releases(releases));

    FetcherParts fetcher;
    fetcher.files = seams_.files;
    fetcher.http = seams_.http;
    fetcher.clock = seams_.clock;
    fetcher.fetch_worker = app()->fetch_worker();
    fetcher.ui = app()->ui();
    fetcher.gate = gate_.get();
    fetcher.listing = listing_.get();
    fetcher.settings = &settings_->settings();
    fetcher.root = root_.path;
    fetcher.arrivals = arrivals_.get();
    fetcher.changed = part_built("releases") ? releases_.get() : nullptr;
    fetcher_.reset(new Fetcher(fetcher));
    // The fetcher's answers go to the player's side or the playlists' (compose_play.cpp, compose_lists.cpp).
    make_fetch_router();

    HvscSetupParts setup;
    setup.files = seams_.files;
    setup.http = seams_.http;
    setup.long_worker = app()->long_worker();
    setup.ui = app()->ui();
    setup.gate = gate_.get();
    setup.jobs = &jobs_;
    setup.notices = notices_.get();
    setup.settings = &settings_->settings();
    setup.root = root_.path;
    setup.cache = folders_.cache;
    setup.listener = hvsc_events_.get();
    setup_.reset(new HvscSetup(setup));

    CompleteDownloadParts download;
    download.files = seams_.files;
    download.http = seams_.http;
    download.unpacker = seams_.unpacker;
    download.memory = seams_.memory;
    download.long_worker = app()->long_worker();
    download.ui = app()->ui();
    download.gate = gate_.get();
    download.jobs = &jobs_;
    download.notices = notices_.get();
    download.settings = &settings_->settings();
    download.root = root_.path;
    download.cache = folders_.cache;
    download.play = hvsc_events_.get();
    download.listener = hvsc_events_.get();
    download_.reset(new CompleteDownload(download));

    ui::Services& s = services();
    s.listing = listing_.get();
    s.notice_gate = gate_.get();
    s.jobs = &jobs_;
    s.fetcher = fetcher_.get();
    s.setup = setup_.get();
    s.releases = part_built("releases") ? releases_.get() : nullptr;
    s.download = part_built("download-all") ? download_.get() : nullptr;
    s.unpacker = seams_.unpacker;
    s.memory = seams_.memory;
    s.rebuild_index = [this] { rebuild_index(); };
    make_job_mark();
}

void Composition::rebuild_index() {
    // SPEC-161: "Rebuild the index", on the long worker; the notice MSG_INDEX_READY tells its end.
    Index* index = index_.get();
    if (!index) return;
    app()->long_worker()->post([this, index] {
        const IndexRun run = index->build(IndexReason::User, [this] { return running(); });
        if (!run.notice.empty()) on_ui([this, run] { app()->notice(run.notice); });
        on_ui([this] { app()->redraw(); });
    });
    app()->redraw();
}

void Composition::make_job_mark() {
    // SPEC-147, SPEC-254: the running job's `job` in every description, and the job mark, an arrow and the
    // job's percentage, at the start of the top line's right part and in `mini.job`, on every screen but
    // the HVSC screen, which shows the job itself. It is always built.
    App* a = app();
    Jobs* jobs = &jobs_;
    a->add_describer([a, jobs](Json* d) {
        const JobState state = jobs->state();
        d->set("job", ui::describe_job(state));
        if (state.kind == JobKind::None || a->top_name() == "hvsc") return;
        const Json* had = d->get("mini");
        Json mini = had && !had->is_null() ? *had : Json::object();
        mini.set("job", ui::job_mark(state));
        d->set("mini", mini);
    });
    a->add_top_right([a, jobs]() -> std::string {
        if (a->top_name() == "hvsc") return "";
        return ui::job_mark(jobs->state());
    });
    // The job's progress changes by itself: everything is drawn again when what it shows changes.
    auto shown = std::make_shared<std::string>();
    a->every(100, [a, jobs, shown] {
        const JobState state = jobs->state();
        const std::string now = std::to_string(static_cast<int>(state.kind)) + " " + std::to_string(state.step) +
                                " " + std::to_string(state.done) + " " + std::to_string(state.total) +
                                (state.can_stop ? "" : " no stop");
        if (now == *shown) return;
        *shown = now;
        a->redraw();
    });
}

void Composition::hvsc_at_start() {
    // SPEC-200, step 5, on the long worker: SPEC-144's marker before anything reads the song length database,
    // then the listing (SPEC-142): opened, or built again if it does not fit the database. Only once it is
    // ready do the browser, the player and the index start reading it, so that nothing reads it while it
    // changes.
    Files* files = seams_.files;
    Listing* listing = listing_.get();
    CompleteDownload* download = part_built("download-all") ? download_.get() : nullptr;
    const std::string listing_path = listing_file();
    const std::string database = database_file();
    const std::string cache = folders_.cache;
    app()->long_worker()->post([this, files, listing, download, listing_path, database, cache] {
        if (download) download->recover();
        if (!listing->open(files, listing_path, database)) {
            FileInfo info;
            if (files->info(database, &info) == FileStatus::Ok && !info.is_folder) {
                files->make_folders(cache);
                const ListingBuild built = Listing::build(files, database, listing_path);
                if (built.ok) {
                    listing->open(files, listing_path, database);
                } else {
                    on_ui([this] { app()->notice("MSG_LENGTHS_UNREADABLE"); });
                }
            }
        }
        on_ui([this] {
            // The listing is ready: the browser shows its folder, the player gets its entry back, and the
            // workers go on with SPEC-200's step 5.
            browse_at_start();      // then, once its first folder is there, the playlists and the player
            cleanup_at_start();
            search_at_start();
            measure_engines();
        });
    });
}

void Composition::cleanup_at_start() {
    // SPEC-202: temporary files and old partial downloads, on the long worker; and what SPEC-145's conditions
    // find, which every run writes to its measurements (SPEC-241, FIND-024).
    Files* files = seams_.files;
    Clock* clock = seams_.clock;
    const Folders folders = folders_;
    Metrics* metrics = &metrics_;
    Unpacker* unpacker = seams_.unpacker;
    Memory* memory = seams_.memory;
    const Listing* listing = listing_.get();
    const std::string root = root_.path;
    const bool offer = unpacker && memory;   // SPEC-241 (FIND-024): written at every run
    app()->long_worker()->post([files, clock, folders, metrics, unpacker, memory, listing, root, offer] {
        clean_up_at_start(files, clock, folders);
        if (!offer) return;
        const HvscStatus status = hvsc_status(files, *listing, root);
        const DownloadOffer found = download_offer(status, files, unpacker, memory, root);
        metrics->set_number("memory_check_mib", static_cast<std::int64_t>(found.memory_mib));
        metrics->set("unpacker", found.unpacker ? "yes" : "no");
        metrics->set_number("card_free_mib", static_cast<std::int64_t>(found.free_mib));
        metrics->set("download_everything_offered", found.offered && part_built("download-all") ? "yes" : "no");
    });
}

void Composition::listing_changed() {
    // The listing file is new (SPEC-140, SPEC-144, SPEC-148). The Listing object is shared by pointer with
    // the collection (read on the quick worker), the player (the UI thread), the fetcher (the fetch worker)
    // and the index (the long worker), so it is opened again on the UI thread while the three workers are
    // parked between their jobs; then every part that shows what it holds is told.
    with_workers_parked([this] {
        listing_->open(seams_.files, listing_file(), "");
        if (player_) player_->set_listing(listing_.get());
        refresh_browser();
        Index* index = index_.get();
        if (!index) return;
        app()->long_worker()->post([this, index] {
            const IndexRun run = index->build(IndexReason::AfterSetup, [this] { return running(); });
            if (!run.notice.empty()) on_ui([this, run] { app()->notice(run.notice); });
        });
    });
}

void Composition::with_workers_parked(std::function<void()> fn) {
    struct Park {
        std::mutex mutex;
        std::condition_variable wake;
        int parked = 0;
        bool done = false;
    };
    auto park = std::make_shared<Park>();
    Executor* const workers[] = {app()->quick(), app()->fetch_worker(), app()->long_worker()};
    const int count = static_cast<int>(sizeof workers / sizeof workers[0]);
    for (Executor* worker : workers) {
        worker->post([this, park, fn, count] {
            std::unique_lock<std::mutex> lock(park->mutex);
            if (++park->parked == count) {
                // The last to park: the UI thread does the work, then lets the three go on.
                on_ui([park, fn] {
                    fn();
                    std::lock_guard<std::mutex> done(park->mutex);
                    park->done = true;
                    park->wake.notify_all();
                });
            }
            while (!park->done && running()) park->wake.wait_for(lock, std::chrono::milliseconds(20));
        });
    }
}

}  // namespace spz::app
