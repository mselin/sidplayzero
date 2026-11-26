// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, playlists and favourites (SPEC-170 to SPEC-179), part `playlists`: the resolver of
// entries, the playlists and Favourites, and "Fetch missing" (SPEC-174) with its PlaylistTuneFetcher on the
// fetcher of SPEC-141. The screens and panels of the part are src/ui/playlists_screen.*, playlist_screen.*
// and lists_panels.*. The playlists are read from their folder on the quick worker at start, before the
// screens are given them (lists_at_start()); from then on they are touched only on the UI thread (the
// browser's "Fav" too), so they need no lock. What takes long with a file (an import, an export) works on
// the quick worker on what it has read, or on a copy of a playlist, and its result comes back to the UI
// thread.
#include <cstdint>
#include <map>
#include <vector>

#include "app/composition.h"
#include "spz/keymap.h"
#include "ui/lists_panels.h"

namespace spz::app {

// SPEC-174: "Fetch missing" asks the fetcher for one tune at a time, as a playlist's file.
struct Composition::ListsFetcher : PlaylistTuneFetcher, FetchListener {
    ListsFetcher(Composition* owner, RootScope* of) : parts(owner), scope(of) {}
    void fetch_for_playlist(const std::string& hvsc_path, Done done) override {
        const FetchAnswer answer = scope->fetcher->request(hvsc_path, FetchWhy::Playlist);
        if (answer == FetchAnswer::Present) {
            parts->on_ui([done] { done(""); });
        } else if (answer == FetchAnswer::Fetching || answer == FetchAnswer::Waiting ||
                   answer == FetchAnswer::AskingNotice) {
            waiting[hvsc_path].push_back(std::move(done));
        } else {
            parts->on_ui([done] { done("MSG_FETCH_NETWORK"); });
        }
    }
    void fetch_started(const std::string&) override {}
    void fetched(const std::string& path, FetchWhy) override { answer(path, ""); }
    void fetch_failed(const std::string& path, FetchWhy, const std::string& message) override {
        answer(path, message.empty() ? "MSG_FETCH_NETWORK" : message);
    }
    void fetch_cancelled(const std::string& path) override { answer(path, "MSG_FETCH_NETWORK"); }
    void answer(const std::string& path, const std::string& failure) {
        const auto found = waiting.find(path);
        if (found == waiting.end()) return;
        std::vector<Done> dones = std::move(found->second);
        waiting.erase(found);
        for (Done& done : dones) done(failure);
    }
    Composition* parts;
    RootScope* scope;
    std::map<std::string, std::vector<Done>> waiting;
};

// What "Fetch missing" tells. It is a job (SPEC-140, SPEC-147): it takes `jobs_` while it runs, so that the
// job mark shows its progress, files done of files to fetch; its end is a notice. The Playlist screen draws
// again as each tune arrives. The job is ended however it ends, also for a root that is no longer the root;
// what a retired root's "Fetch missing" says is not shown.
struct Composition::ListsEvents : FetchMissingListener {
    ListsEvents(Composition* owner, RootScope* of) : parts(owner), scope(of) {}
    void fetch_missing_progress(const FetchMissingState& state) override {
        if (state.running && !job_taken && scope->live) job_taken = parts->jobs_.begin(JobKind::Fetch);
        if (job_taken) {
            parts->jobs_.progress(1, static_cast<std::uint64_t>(state.done), static_cast<std::uint64_t>(state.total));
        }
        ++parts->services().lists_changes;
        parts->app()->redraw();
    }
    void fetch_missing_ended(const ListsResult& notice) override {
        if (job_taken) parts->jobs_.end();
        job_taken = false;
        if (!scope->live) return;
        if (!notice.message.empty()) parts->app()->notice(notice.message, notice.about);
        ++parts->services().lists_changes;
        parts->app()->redraw();
    }
    // SPEC-147: "Stop", asked of the running job, ends "Fetch missing" after the file in flight.
    void watch_stop() {
        if (job_taken && parts->jobs_.stop_asked() && scope->fetch_missing) scope->fetch_missing->stop();
    }
    Composition* parts;
    RootScope* scope;
    bool job_taken = false;
};

void Composition::make_lists() {
    if (!part_built("playlists")) return;
    ui::Services& s = services();
    s.import_folder = folders_.imports;
    s.export_folder = folders_.exports;
    // SPEC-172: "Add" in the browser, Search and the now-playing "More" panel asks which playlist.
    App* a = app();
    s.add_to_playlist = [a](const Entry& entry) { ui::open_pick_playlist(a, entry); };
    a->every(kWatchEveryMs, [this] {
        if (scope_ && scope_->lists_events) scope_->lists_events->watch_stop();
    });
}

void Composition::make_lists_scope(RootScope* scope) {
    // The resolver finds a playlist's entries under the root (SPEC-173), and so the playlists, Favourites
    // and "Fetch missing" are the root's too. The screens get them once they are read (lists_at_start()).
    ui::Services& s = services();
    s.playlists = nullptr;
    s.favourites = nullptr;
    s.fetch_missing = nullptr;
    if (!part_built("playlists")) return;
    scope->resolver.reset(
        new EntryResolver(seams_.files, scope->root.path, scope->listing.get(), scope->own_tunes.get()));
    scope->playlists.reset(new Playlists(seams_.files, folders_.playlists, scope->resolver.get()));
    scope->favourites.reset(new Favourites(scope->playlists.get()));
    scope->lists_fetcher.reset(new ListsFetcher(this, scope));
    route_fetches_to_lists(scope, scope->lists_fetcher.get());
    scope->lists_events.reset(new ListsEvents(this, scope));
    scope->fetch_missing.reset(new FetchMissing(seams_.clock, seams_.files, scope->root.path, scope->resolver.get(),
                                                scope->lists_fetcher.get(), gate_.get(), scope->lists_events.get()));
}

void Composition::lists_at_start() {
    // The playlists are read on the quick worker, once the listing is ready. The screens get them only after
    // the player's start has read them on the same worker (SPEC-131: a queue made again from a playlist;
    // play_at_start() posts its work right after this call): until then nothing else touches them.
    RootScope* scope = scope_.get();
    if (!scope->playlists) return;
    Playlists* playlists = scope->playlists.get();
    app()->quick()->post([playlists] { playlists->load(); });
    on_ui([this, scope] {
        app()->quick()->post([this, scope] {
            on_ui([this, scope] {
                if (!scope->live) return;
                ui::Services& s = services();
                s.playlists = scope->playlists.get();
                s.favourites = scope->favourites.get();
                s.fetch_missing = scope->fetch_missing.get();
                app()->redraw();
            });
        });
    });
}

}  // namespace spz::app
