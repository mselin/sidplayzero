// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, search (SPEC-160, SPEC-161): the index, kept in the cache folder, checked and brought up
// to date by the long worker at start, rebuilt when the user asks (the HVSC screen's "Rebuild the index",
// through ui::Services::rebuild_index) and when the collection root changes, and told of tunes as they
// arrive (compose_hvsc.cpp, Arrivals).
#include <atomic>

#include "app/composition.h"

namespace spz::app {

// The user's own tunes, which HVSC does not list, found by their MD5 (SPEC-173): the index has them.
struct Composition::OwnTunesInIndex : OwnTunes {
    explicit OwnTunesInIndex(const Index* of) : index(of) {}
    bool own_tune_by_md5(const std::string& md5, std::string* path) const override {
        const Index* in = index.load();
        const int count = in->count();
        const int batch = 1000;
        for (int first = 0; first < count; first += batch) {
            for (const IndexRecord& record : in->records(first, batch)) {
                if (!record.md5.empty() && record.md5 == md5) {
                    *path = record.path;
                    return true;
                }
            }
        }
        return false;
    }
    std::atomic<const Index*> index;     // the index of the root of the moment (search_root_changed())
};

void Composition::make_search() {
    index_.reset(new Index(seams_.files, seams_.clock, listing_.get(), root_.path, folders_.cache + "/index.bin"));
    own_tunes_.reset(new OwnTunesInIndex(index_.get()));
    index_live_ = std::make_shared<std::atomic<bool>>(true);
    services().index = index_.get();
    // SPEC-161: "Rebuild the index" (SPEC-102): everything thrown away and built again on the long worker;
    // MSG_INDEX_READY when it is done.
    services().rebuild_index = [this] { build_index(IndexReason::User); };
}

void Composition::build_index(IndexReason reason) {
    // The build stops at its next batch when the app leaves, or when this index is replaced by another
    // (a new root); what it last saved is kept. A build of the index that is shown is followed by a redraw,
    // so that "Indexing n %" goes (SPEC-100).
    Index* index = index_.get();
    if (!index) return;
    Files* files = seams_.files;
    const std::string cache = folders_.cache;
    std::shared_ptr<std::atomic<bool>> live = index_live_;
    app()->long_worker()->post([this, index, files, cache, reason, live] {
        files->make_folders(cache);
        const IndexRun run = index->build(reason, [this, live] { return running() && live->load(); });
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

void Composition::search_root_changed() {
    // SPEC-161: an index of another root is built again. The old index stays until the app ends (a search
    // on the quick worker or the build on the long worker may still hold it); its build stops at its next
    // batch, and the new one, behind it on the long worker, finds the file's root different and rebuilds.
    if (index_live_) index_live_->store(false);
    if (index_) retired_indexes_.push_back(std::move(index_));
    index_.reset(new Index(seams_.files, seams_.clock, listing_.get(), root_.path, folders_.cache + "/index.bin"));
    index_live_ = std::make_shared<std::atomic<bool>>(true);
    static_cast<OwnTunesInIndex*>(own_tunes_.get())->index.store(index_.get());
    services().index = index_.get();
    build_index(IndexReason::Start);
}

}  // namespace spz::app
