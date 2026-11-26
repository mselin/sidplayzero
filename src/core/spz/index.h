// SPDX-License-Identifier: GPL-3.0-or-later
// The search index (SPEC-160, SPEC-161): one file, index.bin, in the cache folder, with a record for
// every tune HVSC lists and every tune on the device. The long worker builds it and keeps it true; a
// search (spz/search.h) is one pass over it on the quick worker, so an Index may be asked from one
// thread while another builds it. There is no database library behind it (SPEC-008).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "spz/clock.h"
#include "spz/engine.h"
#include "spz/files.h"
#include "spz/listing.h"
#include "spz/types.h"

namespace spz {

struct SearchFilter;
struct SearchResults;

// SPEC-160: what the file's header holds.
struct IndexHeader {
    int format = 0;                         // the format number; Index::kFormat for a file this build wrote
    std::string root;                       // the collection root it was built for
    std::uint64_t database_size = 0;        // the size and modification time of the song length database
    std::int64_t database_modified_s = 0;   // it was built from; both 0 when the root has none
};

// SPEC-160: one tune.
struct IndexRecord {
    std::string path;                       // an HVSC path, or a path under the root for a tune HVSC does not list
    bool present = false;                   // its file is on the device, and its header has been read
    std::uint64_t size = 0;                 // of the file, when present
    std::int64_t modified_s = 0;            // of the file, when present
    std::string title, author, released;    // from the file's header, as UTF-8, when present
    SidModel model = SidModel::FromTune;    // the header's chip model, when present
    C64Clock clock = C64Clock::FromTune;    // the header's clock, when present
    int year = 0;                           // the first four-digit number in the released line, else 0
    std::string md5;                        // of the file, for a tune HVSC does not list; else ""
    std::string search_text;                // search_text() of the path and, when present, the three lines
};

// SPEC-160: the search text of a tune: the path, title, author and released line, in lower case, with
// accented letters reduced to their base letters and every character that is not a letter or a digit
// turned into a space. The arguments are UTF-8.
std::string search_text(const std::string& path, const std::string& title, const std::string& author,
                        const std::string& released);

// SPEC-160, SPEC-202, SPEC-205: reads the bytes of an index file. False, with nothing kept, if the
// header does not fit this build's format or the file is shorter than its header says, or is damaged in
// any other way. This is the function the fuzz target of index.bin calls.
bool read_index(const Bytes& file, IndexHeader* header, std::vector<IndexRecord>* records);

// SPEC-161: why a build is run.
enum class IndexReason {
    Start,        // at every start: only step two, unless there is no index or its header does not fit
    AfterSetup,   // HVSC has just been set up or updated (SPEC-140, SPEC-148)
    User,         // "Rebuild the index": everything is thrown away and built again
};

// How a build ended.
struct IndexRun {
    bool complete = false;   // false: `keep_going` said stop, and the file holds what was last saved
    bool rebuilt = false;    // the index was built from nothing, not only brought up to date
    std::string notice;      // "MSG_INDEX_READY" when a build for User or AfterSetup is complete, else ""
    // For the measurements of SPEC-240 and SPEC-241, by the index's clock: how long each step took, and how
    // many tunes' files step two read.
    Millis step_one_ms = -1;   // -1: step one did not run
    Millis step_two_ms = 0;
    int tunes_read = 0;
};

class Index {
public:
    static constexpr int kFormat = 1;
    static constexpr int kBatchFiles = 200;      // SPEC-161: files handled at a time
    static constexpr Millis kPauseMs = 100;      // SPEC-161: the pause between batches while a tune plays
    static constexpr int kSaveEvery = 2000;      // SPEC-161: tunes read between two rewrites of the file

    // `root` is the collection root and `listing` its listing (nullptr, or not open: the root has none).
    // The song length database whose size and time the header records is DOCUMENTS/Songlengths.md5
    // under the root. `index_file` is index.bin in the cache folder; its folder exists. `clock` is for
    // the pauses of a build and the time limit of a search.
    Index(Files* files, Clock* clock, const Listing* listing, std::string root, std::string index_file);

    // SPEC-160: reads the index file. False, and the index is empty, if there is none or read_index()
    // refuses it. build() does this itself when it has not been done.
    bool load();

    // SPEC-161: builds the index, or brings it up to date. Called on the long worker.
    //  - With no index, or one whose format number, root or database size and time differ from the
    //    present ones, or for IndexReason::User: step one writes a record for every tune of the listing,
    //    without opening any tune; then step two.
    //  - Step two walks the folders of the root that are on the device. A present tune whose record is
    //    missing, or whose size or modification time differs, has its header read (and its whole file's
    //    MD5 taken, if HVSC does not list it); the record of a tune that is no longer there is removed
    //    (a tune HVSC lists then has step one's record again: its path, absent).
    //  - It handles 200 files at a time, sleeping 100 ms on the clock between two batches while
    //    set_playing(true), and rewrites the file through SPEC-132 after every 2,000 tunes read.
    //  - `keep_going` is asked after each batch, once that batch is handled and, when due, saved. When
    //    it says false the build stops there; the next build carries on from what the file holds.
    IndexRun build(IndexReason reason, const std::function<bool()>& keep_going = std::function<bool()>());

    // Whether a tune is playing now (SPEC-161's pauses). May be called from another thread.
    void set_playing(bool playing);
    // Progress of the build, 0 to 100, for "Indexing n %" (SPEC-100, SPEC-102). 100 when complete.
    int percent() const;
    // Whether the index is complete: the last build ran to its end.
    bool complete() const;

    // SPEC-161: step two for one folder ("/" is the root), when the browser has listed it and found a
    // difference.
    void refresh_folder(const std::string& folder);
    // SPEC-161, SPEC-141: a tune has been fetched: its record is made present, from its file, at once.
    void tune_arrived(const std::string& path);

    // What the index holds, in the order of the file, which is the order of path.
    IndexHeader header() const;
    int count() const;
    std::vector<IndexRecord> records(int first, int count) const;
    // The record of one tune. False if there is none.
    bool find(const std::string& path, IndexRecord* out) const;

private:
    friend SearchResults search(const Index& index, const std::string& query, const SearchFilter& filter);

    struct OnDevice {             // a tune found on the device by step two
        std::string path;
        FileInfo info;
    };
    IndexHeader present_header() const;
    std::string place(const std::string& path) const;
    bool listed(const std::string& path) const;
    bool walk(const std::string& folder, bool deep, std::vector<OnDevice>* out) const;   // false: `folder` could not be listed
    // Merges what is on the device with the records `in_scope` says are of the part looked at, and
    // returns the tunes whose files are to be read, in order of path.
    std::vector<OnDevice> reconcile(const std::vector<OnDevice>& device,
                                    const std::function<bool(const std::string&)>& in_scope);
    IndexRecord read_record(const OnDevice& tune) const;
    void put(IndexRecord record, std::size_t* cursor = nullptr);   // cursor: where the last put went, to look there first
    std::size_t locate(const std::string& path) const;   // the first record not before `path`; lock held
    bool save();

    Files* files_;
    Clock* clock_;
    const Listing* listing_;
    std::string root_;
    std::string index_file_;

    mutable std::mutex mutex_;              // guards what follows: a search may run while a build does
    IndexHeader header_;
    std::vector<IndexRecord> records_;      // in order of path (path_comes_before)
    std::atomic<bool> complete_{false};      // written under mutex_, read without it: complete() never waits for a search (SPEC-162 holds mutex_ up to 2 s)
    bool loaded_ = false;                   // load() has been tried
    bool have_file_ = false;                // and found an index that fits this build's format

    std::atomic<bool> playing_{false};
    std::atomic<int> percent_{0};
};

}  // namespace spz
