// SPDX-License-Identifier: GPL-3.0-or-later
// Setting HVSC up (SPEC-140): "Get HVSC" downloads five documents, checks them, moves them into the
// collection root and builds the listing. With it: what is known of HVSC on the device, the reader of
// hv_sids.txt (SPEC-148), and the check for room on the card (SPEC-146).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "spz/executor.h"
#include "spz/files.h"
#include "spz/http.h"
#include "spz/job.h"
#include "spz/listing.h"
#include "spz/notice_gate.h"
#include "spz/settings.h"
#include "spz/types.h"

namespace spz {

// Where things are (SPEC-140, SPEC-142, SPEC-148): the five documents are DOCUMENTS/Songlengths.md5,
// DOCUMENTS/STIL.txt, DOCUMENTS/HVSC.txt, DOCUMENTS/BUGlist.txt and DOCUMENTS/hv_sids.txt, under the
// collection root and under `url_files` alike; the listing is listing.bin in the cache folder.

// SPEC-150's limit for a document, and SPEC-146's figure for setting HVSC up.
const std::uint64_t kDocumentLimitBytes = 32ull * 1024 * 1024;
const std::uint64_t kSetupRoomBytes = 16ull * 1024 * 1024;

// SPEC-148: the release that the text of hv_sids.txt names: the first number after the word "Release".
// 0 if it names none. This is the release reader of SPEC-205.
int read_release(const Bytes& hv_sids);

// What is known of HVSC on the device (SPEC-140).
struct HvscStatus {
    bool set_up = false;   // the root has a song length database that gave a listing
    int release = 0;       // what the root's hv_sids.txt names; 0: not known
};
// `listing` is the listing in use, open or not. Reads the root's hv_sids.txt (at most 64 KiB, SPEC-204),
// so it is called on a worker.
HvscStatus hvsc_status(Files* files, const Listing& listing, const std::string& root);

// SPEC-146: what is about to be written, against the free space of the file system holding `destination`.
struct Room {
    bool enough = false;
    std::uint64_t needed_mib = 0;   // rounded up to whole MiB
    std::uint64_t free_mib = 0;     // rounded down
};
Room check_room(Files* files, const std::string& destination, std::uint64_t needed_bytes);

// Told, on the UI thread, how a set-up or an update ended.
class HvscListener {
public:
    virtual ~HvscListener() = default;
    // Step 6 has ended: the listing, listing.bin in the cache, is new and is to be opened again, and the
    // index is to be started (SPEC-161). If the HVSC screen or the empty screen is showing, the browser
    // opens at the root. The notice MSG_HVSC_READY has been raised.
    virtual void hvsc_ready(int release) = 0;
    // A step failed or the job was stopped; what was there before is untouched. `step` is 0 for a stop.
    virtual void hvsc_failed(int step) = 0;
};

struct HvscSetupParts {
    Files* files = nullptr;
    HttpClient* http = nullptr;
    Executor* long_worker = nullptr;     // the job runs here (SPEC-022)
    Executor* ui = nullptr;              // notices and the listener are called from here
    NoticeGate* gate = nullptr;
    Jobs* jobs = nullptr;
    NoticeSink* notices = nullptr;
    const Settings* settings = nullptr;  // `url_files`
    std::string root;                    // the collection root, with no slash at its end; need not exist
    std::string cache;                   // the cache folder (SPEC-134)
    HvscListener* listener = nullptr;    // may be null
};

class HvscSetup {
public:
    explicit HvscSetup(const HvscSetupParts& parts);
    ~HvscSetup();
    HvscSetup(const HvscSetup&) = delete;
    HvscSetup& operator=(const HvscSetup&) = delete;

    // "Get HVSC". Step 1 is the notice gate: if the notice is not accepted the panel opens, and the rest
    // follows Enter. Then a job of kind Setup runs steps 2 to 6 on the long worker:
    //   2  room: 16 MiB free where the root is, else the notice MSG_NEED_ROOM with "n" (needed) and "m" (free)
    //   3  the five documents from `url_files`, each whole, into a new folder in the cache
    //   4  the song length file gives a listing with at least one tune; hv_sids.txt names a release
    //   5  each is moved into DOCUMENTS under the root through SPEC-132, the song length file last
    //   6  the listing is built as listing.bin in the cache; the notice MSG_HVSC_READY with "n", the release
    // If a step fails: the notice MSG_HVSC_FAILED with "step", and what was there before is untouched.
    // Returns false, and does nothing, if a job is running already.
    bool start();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spz
