// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, search (SPEC-160, SPEC-161): the index, kept in the cache folder, checked and brought up
// to date by the long worker at start, and told of tunes as they arrive (compose_hvsc.cpp, Arrivals).
#include "app/composition.h"

namespace spz::app {

// The user's own tunes, which HVSC does not list, found by their MD5 (SPEC-173): the index has them.
struct Composition::OwnTunesInIndex : OwnTunes {
    explicit OwnTunesInIndex(const Index* of) : index(of) {}
    bool own_tune_by_md5(const std::string& md5, std::string* path) const override {
        const int count = index->count();
        const int batch = 1000;
        for (int first = 0; first < count; first += batch) {
            for (const IndexRecord& record : index->records(first, batch)) {
                if (!record.md5.empty() && record.md5 == md5) {
                    *path = record.path;
                    return true;
                }
            }
        }
        return false;
    }
    const Index* index;
};

void Composition::make_search() {
    index_.reset(new Index(seams_.files, seams_.clock, listing_.get(), root_.path, folders_.cache + "/index.bin"));
    own_tunes_.reset(new OwnTunesInIndex(index_.get()));
    services().index = index_.get();
}

void Composition::search_at_start() {
    // SPEC-200, step 5: the index is checked, and brought up to date, on the long worker. It stops at the
    // next batch when the app leaves; what it last saved is kept.
    Index* index = index_.get();
    Files* files = seams_.files;
    const std::string cache = folders_.cache;
    app()->long_worker()->post([this, index, files, cache] {
        files->make_folders(cache);
        const IndexRun run = index->build(IndexReason::Start, [this] { return running(); });
        if (!run.notice.empty()) on_ui([this, run] { app()->notice(run.notice); });
        on_ui([this] { app()->redraw(); });
    });
}

}  // namespace spz::app
