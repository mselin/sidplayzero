// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, HVSC: the listing (SPEC-142), the notice and its gate (SPEC-152), jobs (SPEC-147), the
// fetcher of single tunes (SPEC-141) with Releases as its ChangedTunes (SPEC-148), setting HVSC up
// (SPEC-140) and the complete download (SPEC-144), and what they do at start.
#include <atomic>
#include <memory>

#include "app/composition.h"
#include "platform/guarded.h"
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
    HvscEvents(Composition* owner, RootScope* of) : parts(owner), scope(of) {}
    void hvsc_ready(int) override {
        // SPEC-140: the listing is new; if the HVSC screen or the empty screen shows, the browser opens at the
        // root, once no panel and no help is open (SPEC-113). A job of a root that is no longer the root
        // changes nothing it shows.
        changed();
        if (!scope->live) return;
        parts->listing_changed(scope);
        App* app = parts->app();
        const bool showing = app->top_name() == "hvsc" || app->top_name() == "empty";
        if (showing || app->bottom() == "empty") {
            // Elsewhere the screen is left alone; only the empty screen under it gives its place to the browser.
            Composition* p = parts;
            RootScope* s = scope;
            app->when_no_overlay([p, s, showing] {
                if (!s->live) return;
                if (s->browser) s->browser->show("/");
                p->app()->set_bottom("browser");
                if (showing) p->app()->open("browser");
            });
        }
    }
    void hvsc_failed(int) override { changed(); }
    void download_done() override {
        changed();
        if (scope->live) parts->listing_changed(scope);
    }
    void download_failed(int) override { changed(); }
    // The HVSC screen looks at HVSC again (Services::hvsc_changes). Playing is switched off and on (below) for
    // the player of the moment, whichever root's download asks it.
    void changed() {
        ++parts->services().hvsc_changes;
        parts->app()->redraw();
    }
    void playing_off() override {
        if (parts->scope_->player) parts->scope_->player->set_playing_off(true);
    }
    void playing_on() override {
        if (parts->scope_->player) parts->scope_->player->set_playing_off(false);
    }
    Composition* parts;
    RootScope* scope;
};

// SPEC-141, SPEC-161, SPEC-043: a tune's file has arrived at its HVSC path.
struct Composition::Arrivals : NewTuneListener {
    Arrivals(Composition* owner, RootScope* of) : parts(owner), scope(of) {}
    void tune_arrived(const std::string& path) override {
        if (!scope->live) return;
        if (scope->browser) scope->browser->tune_changed(path);
        if (Index* index = scope->index.get()) {
            parts->app()->quick()->post([index, path] { index->tune_arrived(path); });
        }
        parts->app()->redraw();
    }
    Composition* parts;
    RootScope* scope;
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

// SPEC-147, A8: the long worker as set-up, update and the download see it. A job of theirs that throws
// is logged, and the job they began is ended, so that the job mark goes and another job can begin.
struct Composition::JobGuard : Executor {
    explicit JobGuard(Composition* owner) : parts(owner) {}
    void post(std::function<void()> job) override {
        Composition* p = parts;
        p->app()->long_worker()->post([p, job] {
            if (!platform::run_guarded("a job of HVSC", job)) p->on_ui([p] { p->end_failed_job(); });
        });
    }
    Composition* parts;
};

void Composition::end_failed_job() {
    const JobKind kind = jobs_.state().kind;
    if (kind != JobKind::Setup && kind != JobKind::Update && kind != JobKind::Download) return;
    jobs_.end();
    if (scope_->player) scope_->player->set_playing_off(false);
    ++services().hvsc_changes;
    app()->redraw();
}

void Composition::route_fetches_to_player(RootScope* scope, FetchListener* listener) {
    scope->fetch_router->player = listener;
}

void Composition::route_fetches_to_lists(RootScope* scope, FetchListener* listener) {
    scope->fetch_router->lists = listener;
}

std::string Composition::listing_file() const { return folders_.cache + "/listing.bin"; }

std::string Composition::database_file(const RootScope* scope) const {
    return scope->root.path + "/DOCUMENTS/Songlengths.md5";
}

bool Composition::open_listing(Files* files, Listing* listing, const std::string& listing_path,
                               const std::string& database, const std::string& cache) {
    // SPEC-142: opened, or built again if it does not fit the database. listing.bin is one file for every
    // root: one built from another root's database does not fit this one's, and is built again.
    if (listing->open(files, listing_path, database)) return true;
    FileInfo info;
    if (files->info(database, &info) != FileStatus::Ok || info.is_folder) return true;   // no database: no listing
    files->make_folders(cache);
    const ListingBuild built = Listing::build(files, database, listing_path);
    if (!built.ok) return false;
    listing->open(files, listing_path, database);
    return true;
}

void Composition::make_hvsc() {
    // What does not depend on the root. The rest is make_hvsc_scope().
    gate_record_.reset(new GateRecord(state_.get()));
    gate_panel_.reset(new GatePanel(app()));
    gate_.reset(new NoticeGate(gate_record_.get(), gate_panel_.get()));
    gate_panel_->gate = gate_.get();
    notices_.reset(new Notices(app()));
    job_guard_.reset(new JobGuard(this));

    ui::Services& s = services();
    s.notice_gate = gate_.get();
    s.jobs = &jobs_;
    s.unpacker = seams_.unpacker;
    s.memory = seams_.memory;
    make_job_mark();
}

void Composition::make_hvsc_scope(RootScope* scope) {
    // The listing: opened on the long worker (hvsc_at_start, collection_changed), before anything reads it.
    scope->listing.reset(new Listing());
    scope->hvsc_events.reset(new HvscEvents(this, scope));
    scope->arrivals.reset(new Arrivals(this, scope));

    ReleasesParts releases;
    releases.files = seams_.files;
    releases.http = seams_.http;
    releases.long_worker = job_guard_.get();
    releases.ui = app()->ui();
    releases.gate = gate_.get();
    releases.jobs = &jobs_;
    releases.notices = notices_.get();
    releases.settings = settings_.get();
    releases.root = scope->root.path;
    releases.cache = folders_.cache;
    releases.listener = scope->hvsc_events.get();
    scope->releases.reset(new Releases(releases));

    FetcherParts fetcher;
    fetcher.files = seams_.files;
    fetcher.http = seams_.http;
    fetcher.clock = seams_.clock;
    fetcher.fetch_worker = app()->fetch_worker();
    fetcher.ui = app()->ui();
    fetcher.gate = gate_.get();
    fetcher.listing = scope->listing.get();
    fetcher.settings = settings_.get();
    fetcher.root = scope->root.path;
    fetcher.arrivals = scope->arrivals.get();
    fetcher.changed = part_built("releases") ? scope->releases.get() : nullptr;
    scope->fetcher.reset(new Fetcher(fetcher));
    // The fetcher's answers go to the player's side or the playlists' (compose_play.cpp, compose_lists.cpp).
    scope->fetch_router.reset(new FetchRouter());
    scope->fetcher->set_listener(scope->fetch_router.get());

    HvscSetupParts setup;
    setup.files = seams_.files;
    setup.http = seams_.http;
    setup.long_worker = job_guard_.get();
    setup.ui = app()->ui();
    setup.gate = gate_.get();
    setup.jobs = &jobs_;
    setup.notices = notices_.get();
    setup.settings = settings_.get();
    setup.root = scope->root.path;
    setup.cache = folders_.cache;
    setup.listener = scope->hvsc_events.get();
    scope->setup.reset(new HvscSetup(setup));

    CompleteDownloadParts download;
    download.files = seams_.files;
    download.http = seams_.http;
    download.unpacker = seams_.unpacker;
    download.memory = seams_.memory;
    download.long_worker = job_guard_.get();
    download.ui = app()->ui();
    download.gate = gate_.get();
    download.jobs = &jobs_;
    download.notices = notices_.get();
    download.settings = settings_.get();
    download.root = scope->root.path;
    download.cache = folders_.cache;
    download.play = scope->hvsc_events.get();
    download.listener = scope->hvsc_events.get();
    scope->download.reset(new CompleteDownload(download));

    ui::Services& s = services();
    s.listing = scope->listing.get();
    s.fetcher = scope->fetcher.get();
    s.setup = scope->setup.get();
    s.releases = part_built("releases") ? scope->releases.get() : nullptr;
    s.download = part_built("download-all") ? scope->download.get() : nullptr;
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
    // ready do the browser, the player and the index start reading it.
    Files* files = seams_.files;
    RootScope* scope = scope_.get();
    Listing* listing = scope->listing.get();
    CompleteDownload* download = part_built("download-all") ? scope->download.get() : nullptr;
    const std::string listing_path = listing_file();
    const std::string database = database_file(scope);
    const std::string cache = folders_.cache;
    app()->long_worker()->post([this, files, listing, download, listing_path, database, cache] {
        // SPEC-144: a download that a killed run left past step 6 is finished now, as a job of its own.
        if (download && !platform::run_guarded("finishing the download", [download] { download->recover(); })) {
            on_ui([this] { end_failed_job(); });
        }
        // Whatever the listing comes to, start-up goes on: a listing that throws is as none.
        bool readable = true;
        platform::run_guarded("opening the listing",
                              [&] { readable = open_listing(files, listing, listing_path, database, cache); });
        if (!readable) on_ui([this] { app()->notice("MSG_LENGTHS_UNREADABLE"); });
        on_ui([this] {
            // The listing is ready: the browser shows its folder, the player gets its entry back, and the
            // workers go on with SPEC-200's step 5.
            browse_at_start();      // then, once its first folder is there, the playlists and the player
            cleanup_at_start();
            search_at_start();
            measure_engines();
            selftest_at_start();
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
    const Listing* listing = scope_->listing.get();
    const std::string root = scope_->root.path;
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

void Composition::listing_changed(RootScope* scope) {
    // The listing file is new (SPEC-140, SPEC-144, SPEC-148). The scope's Listing is opened again in place,
    // on the long worker: whoever reads it on another thread meanwhile reads the listing before or the one
    // after, never half of one (Listing, SPEC-022), so no worker waits for this. Then every part that shows
    // what it holds is told, and the index is brought up to date.
    Files* files = seams_.files;
    Listing* listing = scope->listing.get();
    const std::string listing_path = listing_file();
    app()->long_worker()->post([this, scope, files, listing, listing_path] {
        listing->open(files, listing_path, "");
        on_ui([this, scope] {
            if (!scope->live) return;
            if (scope->player) scope->player->set_listing(scope->listing.get());
            refresh_browser();
            Index* index = scope->index.get();
            if (!index) return;
            std::shared_ptr<std::atomic<bool>> live = scope->index_live;
            app()->long_worker()->post([this, index, live] {
                const IndexRun run =
                    index->build(IndexReason::AfterSetup, [this, live] { return running() && live->load(); });
                if (!run.notice.empty() && live->load()) on_ui([this, run] { app()->notice(run.notice); });
            });
        });
    });
}

}  // namespace spz::app
