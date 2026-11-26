// SPDX-License-Identifier: GPL-3.0-or-later
// Releases (SPEC-148), part `releases`: "Check for a newer release", "Update to #m", and what an update
// does with the tunes that are on the device. The reader of hv_sids.txt itself is read_release(), in
// spz/hvsc_setup.h, because setting HVSC up needs it too.
#pragma once

#include <memory>
#include <string>

#include "spz/executor.h"
#include "spz/fetcher.h"
#include "spz/files.h"
#include "spz/http.h"
#include "spz/hvsc_setup.h"
#include "spz/job.h"
#include "spz/notice_gate.h"
#include "spz/settings_file.h"

namespace spz {

struct ReleasesParts {
    Files* files = nullptr;
    HttpClient* http = nullptr;
    Executor* long_worker = nullptr;     // the check and the update run here
    Executor* ui = nullptr;              // notices and the listener are called from here
    NoticeGate* gate = nullptr;
    Jobs* jobs = nullptr;
    NoticeSink* notices = nullptr;
    SettingsSource settings;             // `url_files`, a copy read when the job starts (A2)
    std::string root;                    // the collection root; HVSC is set up there
    std::string cache;                   // listing.bin is here, and what an update remembers
    HvscListener* listener = nullptr;    // may be null; told as after a set-up
};

class Releases : public ChangedTunes {
public:
    explicit Releases(const ReleasesParts& parts);
    ~Releases() override;
    Releases(const Releases&) = delete;
    Releases& operator=(const Releases&) = delete;

    // "Check for a newer release". Behind the notice gate, requests DOCUMENTS/hv_sids.txt from `url_files`
    // on the long worker, into the cache, and compares its release with the root's. Higher: newer()
    // gives it, and the notice MSG_HVSC_NEWER with "m". Otherwise MSG_HVSC_NEWEST with "n", the root's.
    // If the file cannot be fetched or names no release: MSG_CHECK_FAILED. Nothing in the root changes.
    void check();
    // The release a check found that is higher than the root's: the row "Update to #m". 0 if none.
    int newer() const;

    // "Update to #m": SPEC-140's steps 2 to 6 as a job of kind Update, with the same notices and the
    // same listener calls as a set-up. Before the new listing replaces the old one, for each tune that
    // is on the device:
    //   - its path is not in the new listing, and its MD5 is, at another path: the file is moved there;
    //   - neither its path nor its MD5 is in the new listing: it stays, a tune of the user's own;
    //   - its path is in the new listing with another MD5, and its file has the MD5 the old listing gave:
    //     it is remembered, in the cache, as one to fetch again when next played (fetch_again()).
    // Returns false, and does nothing, if a job is running already.
    bool update();

    // ChangedTunes, for the fetcher.
    bool fetch_again(const std::string& path) override;
    void fetched_again(const std::string& path) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spz
