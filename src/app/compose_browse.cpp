// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, the collection and the browser (SPEC-040 to SPEC-049): the root, the collection, the
// browser's model kept while the app runs, where it opens at start (SPEC-131), and the empty screen's
// place at the bottom of the stack (SPEC-048).
#include <cstdlib>

#include "app/composition.h"
#include "spz/keep_start.h"

namespace spz::app {

namespace {

const Millis kRootWatchMs = 5000;   // SPEC-048: how often the empty screen looks at the root

}  // namespace

// The model's news: what the state file remembers (SPEC-131), the screen's drawing, and at start the
// empty screen in the browser's place (SPEC-048).
struct Composition::BrowserEvents : BrowserListener {
    BrowserEvents(Composition* owner, RootScope* of) : parts(owner), scope(of) {}
    void browser_changed() override {
        BrowserModel* model = scope->browser.get();
        if (!model || !scope->live) return;
        const int highlight = model->highlight();
        std::string entry;
        if (highlight >= 0) entry = model->row(highlight).name;
        if (model->note() != ListNote::Reading && (model->count() > 0 || model->note() == ListNote::Empty)) {
            parts->state_->set_browser(model->folder(), entry);
            if (!parts->first_listing_seen_) {
                parts->first_listing_seen_ = true;
                // SPEC-200: the first screenful comes first; then the playlists and the player's entry, on the
                // quick worker behind it (SPEC-131, SPEC-135).
                parts->lists_at_start();
                parts->play_at_start();
                if (model->folder() == "/" && model->count() == 0) {
                    // SPEC-048: the root does not exist, or holds no entry.
                    parts->app()->set_bottom("empty");
                    parts->watch_empty_root();
                }
            }
        }
        parts->app()->redraw();
    }
    void browser_notice(const std::string& id) override {
        // At start, a root that is not there is the empty screen's business, not a notice's.
        BrowserModel* model = scope->browser.get();
        if (!scope->live) return;
        if (!parts->first_listing_seen_ && model && model->folder() == "/") return;
        parts->app()->notice(id);
    }
    Composition* parts;
    RootScope* scope;
};

CollectionRoot Composition::find_root() const {
    const char* base = std::getenv("HVSC_BASE");
    CollectionRoot root = find_collection_root(base ? base : "", settings_->settings().collection, services().home);
    while (root.path.size() > 1 && root.path.back() == '/') root.path.pop_back();
    return root;
}

void Composition::make_browse() {
    // Everything of the browser depends on the root: make_browse_scope().
}

void Composition::make_browse_scope(RootScope* scope) {
    scope->collection.reset(new Collection(seams_.files, scope->root.path, scope->listing.get()));
    scope->browser_events.reset(new BrowserEvents(this, scope));
    scope->browser.reset(new BrowserModel(scope->collection.get(), app()->quick(), app()->ui(), seams_.clock));
    scope->browser->set_listener(scope->browser_events.get());
    // SPEC-091: six rows, or five and the detail line.
    scope->browser->set_rows_on_screen(part_built("detail") ? 5 : 6);
    services().collection = scope->root.path;
    services().collection_from_variable = scope->root.from_hvsc_base;
    services().tree = scope->collection.get();
    services().browser = scope->browser.get();
}

void Composition::browse_at_start() {
    // SPEC-131: the remembered folder, or its nearest parent that is there, found on the quick worker.
    const State state = state_->state();
    Files* files = seams_.files;
    const std::string root = scope_->root.path;
    const Listing* listing = scope_->listing.get();
    app()->quick()->post([this, state, files, root, listing] {
        const BrowserPlace place = browser_place_at_start(state, files, root, listing);
        on_ui([this, place] {
            if (scope_->browser) scope_->browser->show(place.folder, place.entry);
        });
    });
}

void Composition::watch_empty_root() {
    // SPEC-048: while the empty screen shows, the quick worker looks at the root every 5 s; the browser takes
    // its place as soon as the root has an entry and no panel or help is open.
    if (watching_root_) return;
    watching_root_ = true;
    after(kRootWatchMs, [this] {
        watching_root_ = false;
        if (!running() || app()->bottom() != "empty") return;
        Collection* collection = scope_->collection.get();
        app()->quick()->post([this, collection] {
            const bool entries = collection->has_entries();
            on_ui([this, entries] {
                if (app()->bottom() != "empty") return;
                if (!entries) {
                    watch_empty_root();
                    return;
                }
                app()->when_no_overlay([this] {
                    if (!running() || app()->bottom() != "empty") return;
                    if (scope_->browser) scope_->browser->show("/");
                    app()->set_bottom("browser");
                });
            });
        });
    });
}

void Composition::refresh_browser() {
    // A new model, which keeps no folder listed before, at the folder and entry the old one had.
    RootScope* scope = scope_.get();
    std::string folder = "/", entry;
    if (scope->browser) {
        folder = scope->browser->folder();
        if (scope->browser->highlight() >= 0) entry = scope->browser->row(scope->browser->highlight()).name;
    }
    scope->browser.reset(new BrowserModel(scope->collection.get(), app()->quick(), app()->ui(), seams_.clock));
    scope->browser->set_listener(scope->browser_events.get());
    scope->browser->set_rows_on_screen(part_built("detail") ? 5 : 6);
    services().browser = scope->browser.get();
    scope->browser->show(folder, entry);
    app()->redraw();
}

void Composition::collection_changed() {
    // SPEC-040: with HVSC_BASE unset, the setting names the root. Everything that depends on the root follows
    // at once: a new scope is made for the new root, and the old one is retired whole. The browser shows the
    // new root's top at once, and again when its listing is ready.
    const CollectionRoot root = find_root();
    if (root.path == scope_->root.path) return;
    retire(scope_.get());
    retired_scopes_.push_back(std::move(scope_));
    scope_ = make_scope(root);
    scope_->browser->show("/");
    app()->redraw();
    RootScope* scope = scope_.get();
    Files* files = seams_.files;
    Listing* listing = scope->listing.get();
    const std::string listing_path = listing_file();
    const std::string database = database_file(scope);
    const std::string cache = folders_.cache;
    // The new root's listing, opened or built on the long worker as at start (SPEC-142); then the browser,
    // the index and the playlists take it up.
    app()->long_worker()->post([this, scope, files, listing, listing_path, database, cache] {
        const bool unreadable = !open_listing(files, listing, listing_path, database, cache);
        on_ui([this, scope, unreadable] {
            if (!scope->live) return;
            if (unreadable) app()->notice("MSG_LENGTHS_UNREADABLE");
            refresh_browser();
            build_index(IndexReason::Start);
            lists_at_start();
        });
    });
}

}  // namespace spz::app
