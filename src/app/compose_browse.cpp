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
    explicit BrowserEvents(Composition* owner) : parts(owner) {}
    void browser_changed() override {
        BrowserModel* model = parts->browser_.get();
        if (!model) return;
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
        BrowserModel* model = parts->browser_.get();
        if (!parts->first_listing_seen_ && model && model->folder() == "/") return;
        parts->app()->notice(id);
    }
    Composition* parts;
};

void Composition::find_root() {
    const char* base = std::getenv("HVSC_BASE");
    root_ = find_collection_root(base ? base : "", settings_->settings().collection, services().home);
    while (root_.path.size() > 1 && root_.path.back() == '/') root_.path.pop_back();
    services().collection = root_.path;
    services().collection_from_variable = root_.from_hvsc_base;
}

void Composition::make_browse() {
    collection_.reset(new Collection(seams_.files, root_.path, listing_.get()));
    browser_events_.reset(new BrowserEvents(this));
    browser_.reset(new BrowserModel(collection_.get(), app()->quick(), app()->ui(), seams_.clock));
    browser_->set_listener(browser_events_.get());
    // SPEC-091: six rows, or five and the detail line.
    browser_->set_rows_on_screen(part_built("detail") ? 5 : 6);
    services().tree = collection_.get();
    services().browser = browser_.get();
}

void Composition::browse_at_start() {
    // SPEC-131: the remembered folder, or its nearest parent that is there, found on the quick worker.
    const State state = state_->state();
    Files* files = seams_.files;
    const std::string root = root_.path;
    const Listing* listing = listing_.get();
    app()->quick()->post([this, state, files, root, listing] {
        const BrowserPlace place = browser_place_at_start(state, files, root, listing);
        on_ui([this, place] {
            if (browser_) browser_->show(place.folder, place.entry);
        });
    });
}

void Composition::watch_empty_root() {
    // SPEC-048: while the empty screen shows, the quick worker looks at the root every 5 s; the browser takes
    // its place as soon as the root has an entry and no panel or help is open.
    if (watching_root_) return;
    watching_root_ = true;
    seams_.clock->call_after(kRootWatchMs, [this] {
        watching_root_ = false;
        if (!running() || app()->bottom() != "empty") return;
        Collection* collection = collection_.get();
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
                    if (browser_) browser_->show("/");
                    app()->set_bottom("browser");
                });
            });
        });
    });
}

void Composition::refresh_browser() {
    // A new model, which keeps no folder listed before, at the folder and entry the old one had.
    std::string folder = "/", entry;
    if (browser_) {
        folder = browser_->folder();
        if (browser_->highlight() >= 0) entry = browser_->row(browser_->highlight()).name;
    }
    browser_.reset(new BrowserModel(collection_.get(), app()->quick(), app()->ui(), seams_.clock));
    browser_->set_listener(browser_events_.get());
    browser_->set_rows_on_screen(part_built("detail") ? 5 : 6);
    services().browser = browser_.get();
    browser_->show(folder, entry);
    app()->redraw();
}

void Composition::collection_changed() {
    // SPEC-040: with HVSC_BASE unset, the setting names the root. The collection and the browser follow at
    // once, at the new root's top; and so does the player, whose entries are paths under the root.
    const std::string before = root_.path;
    find_root();
    if (root_.path == before) return;
    retired_collections_.push_back(std::move(collection_));
    retired_listings_.push_back(std::move(listing_));
    // The new root's own listing, if it has one, is for HVSC to set up there; until then it has none.
    listing_.reset(new Listing());
    services().listing = listing_.get();
    collection_.reset(new Collection(seams_.files, root_.path, listing_.get()));
    services().tree = collection_.get();
    browser_.reset();
    refresh_browser();
    remake_player();
}

}  // namespace spz::app
