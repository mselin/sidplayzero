// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, search (SPEC-160, SPEC-161): the index, kept in the cache folder, checked and brought up
// to date by the long worker at start, rebuilt when the user asks (the HVSC screen's "Rebuild the index",
// through ui::Services::rebuild_index) and when the collection root changes, and told of tunes as they
// arrive (compose_hvsc.cpp, Arrivals).
#include <atomic>

#include "app/composition.h"

namespace spz::app {

namespace {

// How many of the index's records a search for an MD5 takes at a time: each batch is a copy, made under the
// index's lock.
const int kIndexBatch = 1000;

}  // namespace

// The user's own tunes, which HVSC does not list, found by their MD5 (SPEC-173): the index has them.
struct Composition::OwnTunesInIndex : OwnTunes {
    explicit OwnTunesInIndex(const Index* of) : index(of) {}
    bool own_tune_by_md5(const std::string& md5, std::string* path) const override {
        const Index* in = index;
        const int count = in->count();
        for (int first = 0; first < count; first += kIndexBatch) {
            for (const IndexRecord& record : in->records(first, kIndexBatch)) {
                if (!record.md5.empty() && record.md5 == md5) {
                    *path = record.path;
                    return true;
                }
            }
        }
        return false;
    }
    const Index* index;                  // the index of its scope's root
};

void Composition::make_search() {
    // SPEC-161: "Rebuild the index" (SPEC-102): everything thrown away and built again on the long worker;
    // MSG_INDEX_READY when it is done. The index itself depends on the root: make_search_scope().
    services().rebuild_index = [this] { build_index(IndexReason::User); };
}

void Composition::make_search_scope(RootScope* scope) {
    scope->index.reset(
        new Index(seams_.files, seams_.clock, scope->listing.get(), scope->root.path, folders_.cache + "/index.bin"));
    scope->own_tunes.reset(new OwnTunesInIndex(scope->index.get()));
    scope->index_live = std::make_shared<std::atomic<bool>>(true);
    services().index = scope->index.get();
}

void Composition::build_index(IndexReason reason, std::function<void(const IndexRun&)> done) {
    // The build stops at its next batch when the app leaves, or when its scope is retired (a new root);
    // what it last saved is kept. A build of the index that is shown is followed by a redraw, so that
    // "Indexing n %" goes (SPEC-100).
    Index* index = scope_->index.get();
    if (!index) return;
    Files* files = seams_.files;
    const std::string cache = folders_.cache;
    std::shared_ptr<std::atomic<bool>> live = scope_->index_live;
    app()->long_worker()->post([this, index, files, cache, reason, live, done] {
        files->make_folders(cache);
        const IndexRun run = index->build(reason, [this, live] { return running() && live->load(); });
        if (done) {
            done(run);
            return;
        }
        if (!live->load()) return;
        on_ui([this, run, live] {
            if (!live->load()) return;
            if (!run.notice.empty()) app()->notice(run.notice);
            app()->redraw();
        });
    });
}

void Composition::search_at_start() {
    // SPEC-200, step 5: the index is checked, and brought up to date, on the long worker.
    build_index(IndexReason::Start);
}

}  // namespace spz::app
