// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, playlists and favourites (SPEC-170 to SPEC-179), part `playlists`: the resolver of
// entries, the playlists and Favourites, and "Fetch missing" (SPEC-174) with its PlaylistTuneFetcher on the
// fetcher of SPEC-141. The screens and panels of the part come with its area.
#include <map>
#include <vector>

#include "app/composition.h"
#include "spz/keymap.h"

namespace spz::app {

// SPEC-174: "Fetch missing" asks the fetcher for one tune at a time, as a playlist's file.
struct Composition::ListsFetcher : PlaylistTuneFetcher, FetchListener {
    explicit ListsFetcher(Composition* owner) : parts(owner) {}
    void fetch_for_playlist(const std::string& hvsc_path, Done done) override {
        const FetchAnswer answer = parts->fetcher_->request(hvsc_path, FetchWhy::Playlist);
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
    std::map<std::string, std::vector<Done>> waiting;
};

// What "Fetch missing" tells: its progress is the job mark's, its end a notice.
struct Composition::ListsEvents : FetchMissingListener {
    explicit ListsEvents(Composition* owner) : parts(owner) {}
    void fetch_missing_progress(const FetchMissingState&) override { parts->app()->redraw(); }
    void fetch_missing_ended(const ListsResult& notice) override {
        if (!notice.message.empty()) parts->app()->notice(notice.message, notice.about);
        parts->app()->redraw();
    }
    Composition* parts;
};

void Composition::make_lists() {
    if (!part_built("playlists")) return;
    resolver_.reset(new EntryResolver(seams_.files, root_.path, listing_.get(), own_tunes_.get()));
    playlists_.reset(new Playlists(seams_.files, folders_.playlists, resolver_.get()));
    favourites_.reset(new Favourites(playlists_.get()));
    lists_fetcher_.reset(new ListsFetcher(this));
    route_fetches_to_lists(lists_fetcher_.get());
    lists_events_.reset(new ListsEvents(this));
    fetch_missing_.reset(new FetchMissing(seams_.clock, seams_.files, root_.path, resolver_.get(), lists_fetcher_.get(),
                                          gate_.get(), lists_events_.get()));
}

void Composition::lists_at_start() {
    // The playlists are read on the quick worker, once the listing is ready; the screens have them once
    // they are read.
    if (!playlists_) return;
    Playlists* playlists = playlists_.get();
    app()->quick()->post([this, playlists] {
        playlists->load();
        on_ui([this] {
            ui::Services& s = services();
            s.playlists = playlists_.get();
            s.favourites = favourites_.get();
            s.fetch_missing = fetch_missing_.get();
            app()->redraw();
        });
    });
}

}  // namespace spz::app
