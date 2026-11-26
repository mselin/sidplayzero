// SPDX-License-Identifier: GPL-3.0-or-later
// Fetching a tune when it is played (SPEC-141), with everything that decides whether a single tune is
// requested: SPEC-143's 60 s after a failure, the limit on unattended requests, the gate of SPEC-152,
// the room on the card, and the rule that a tune is only ever written at an HVSC path the listing names
// (SPEC-154).
//
// The player, and "Fetch missing" (SPEC-174), drive it through request() and the two cancel calls, and
// hear back through FetchListener. request() and the cancel calls are made on the UI thread and never
// touch a file or the network: they answer from what the fetcher remembers of this run. The work itself
// (is the file there already, is there room, the folder, the request, the checks, the rename) is one job
// at a time on the fetch worker (SPEC-022); what came of it is posted back to the UI executor.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "spz/clock.h"
#include "spz/executor.h"
#include "spz/files.h"
#include "spz/http.h"
#include "spz/listing.h"
#include "spz/notice_gate.h"
#include "spz/settings_file.h"
#include "spz/types.h"

namespace spz {

// SPEC-141's numbers.
const Millis kFetchStepWaitMs = 700;                          // a tune reached by stepping is requested after this
const Millis kFetchAheadMs = 10 * 1000;                       // the fetch-ahead is asked for with this much left
const int kFetchUnattendedInAMinute = 6;                      // unattended requests in any minute
const std::uint64_t kFetchRoomBytes = 8ull * 1024 * 1024;     // free space needed before a request
// SPEC-143: after an unattended fetch failed for lack of network or room, no unattended request for this long.
const Millis kFetchRestMs = 60 * 1000;

// SPEC-141: whether it is time to ask for the fetch-ahead of the next entry: the playing song has a
// length (not 0) and `kFetchAheadMs` or less of it is left.
bool fetch_ahead_due(Millis length_ms, Millis elapsed_ms);

// How a tune came to be wanted.
enum class FetchWhy {
    Chosen,       // Enter on the tune: requested at once
    Stepped,      // reached by a next or previous key: requested once it has been wanted for 700 ms
    Unattended,   // reached by a song ending (SPEC-069)
    Ahead,        // the fetch-ahead: the entry that unattended playback will reach next
    Playlist,     // a file of "Fetch missing" (SPEC-174)
};

// What request() says at once.
enum class FetchAnswer {
    Present,       // fetched earlier in this run (ahead, perhaps): nothing to fetch, the file can be loaded
    Fetching,      // its request is with the fetch worker, or one in flight for this tune was taken over
    Waiting,       // it will be requested later, and fetch_started() says when: after the 700 ms of a
                   // step, or as soon as the limit on unattended requests allows
    AskingNotice,  // the notice panel was opened (SPEC-152): fetch_started() follows Enter, fetch_cancelled() ESC
    Failed,        // its fetch-ahead failed: the entry fails with ahead_failure(), and no second request is made
    // Not requested, and not going to be unless it is asked for again:
    NoticeNeeded,  // unattended, and HVSC's notice is not accepted: the notice MSG_NOTICE_NEEDED (SPEC-069)
    Resting,       // unattended, within SPEC-143's 60 s: passed over at once
    OverLimit,     // a fetch-ahead that the limit on unattended requests does not allow now
};

// What the fetcher tells whoever asked. Always called on the UI thread, from the UI executor.
class FetchListener {
public:
    virtual ~FetchListener() = default;
    // The request for a tune that was Waiting or AskingNotice has now been handed to the fetch worker:
    // the player's "wait is over". Not called for a fetch-ahead or a playlist's file.
    virtual void fetch_started(const std::string& path) = 0;
    // The tune's file is at its path and can be loaded. `why` is how it was last asked for.
    virtual void fetched(const std::string& path, FetchWhy why) = 0;
    // The fetch failed, with a message identifier of SPEC-069's table. Nothing was left behind.
    virtual void fetch_failed(const std::string& path, FetchWhy why, const std::string& message) = 0;
    // ESC in the notice panel: what was asked for with Chosen does not happen.
    virtual void fetch_cancelled(const std::string& path) = 0;
};

// Told, on the UI thread, when a tune's file has arrived at its HVSC path: the index adds it at once
// (SPEC-161) and the browser lists its folder again when it is opened (SPEC-043). The index and the
// browser belong to other parts of `core`; this is all the fetcher needs of them.
class NewTuneListener {
public:
    virtual ~NewTuneListener() = default;
    virtual void tune_arrived(const std::string& hvsc_path) = 0;
};

// The tunes an update found changed in HVSC (SPEC-148). spz::Releases implements it; without the part
// `releases` there is none and no present tune is ever requested.
class ChangedTunes {
public:
    virtual ~ChangedTunes() = default;
    // Whether the present tune at `path` is to be fetched again when it is played: the listing has its
    // path with another MD5, and its file still has the MD5 the listing before the update gave for it.
    // Called on the fetch worker.
    virtual bool fetch_again(const std::string& path) = 0;
    // Its new file is in place: it is not asked for again. Called on the fetch worker.
    virtual void fetched_again(const std::string& path) = 0;
};

struct FetcherParts {
    Files* files = nullptr;
    HttpClient* http = nullptr;
    Clock* clock = nullptr;
    Executor* fetch_worker = nullptr;      // single tunes are downloaded here, one at a time (SPEC-022)
    Executor* ui = nullptr;                // what came of it is posted here
    NoticeGate* gate = nullptr;
    const Listing* listing = nullptr;      // gives a path's MD5; a path it does not name is never requested
    SettingsSource settings;               // `url_files`, a copy read when a request is made (A2)
    std::string root;                      // the collection root, with no slash at its end
    NewTuneListener* arrivals = nullptr;   // may be null
    ChangedTunes* changed = nullptr;       // may be null
};

class Fetcher {
public:
    explicit Fetcher(const FetcherParts& parts);
    ~Fetcher();
    Fetcher(const Fetcher&) = delete;
    Fetcher& operator=(const Fetcher&) = delete;

    void set_listener(FetchListener* listener);

    // Asks for the tune at an HVSC path.
    //
    // Chosen, Stepped and Unattended ask for "the tune to be played". There is one such tune at a time:
    // asking for another forgets the earlier one, ends its wait, and cancels its request if that is in
    // flight, or a fetch-ahead in flight, unless the request in flight is for the same tune, which is then
    // taken over. It goes ahead of everything waiting on the fetch worker; a playlist's file in flight is
    // left to finish first.
    //   Chosen      requested at once. If the notice is not accepted, the panel is opened first.
    //   Stepped     requested when it has been the tune to be played for 700 ms.
    //   Unattended  requested at once if the limit of six in a minute allows, else as soon as it does.
    //               Not requested within the 60 s of SPEC-143, or while the notice is not accepted. If
    //               its fetch-ahead failed, it fails with that failure.
    // Ahead asks for the entry after the playing one. It never touches the tune to be played, is not
    // started within the 60 s of SPEC-143, while the notice is not accepted, or beyond the limit, and
    // its outcome is remembered for the Unattended request that follows.
    // Playlist queues a file of "Fetch missing" behind those queued before it.
    //
    // The request itself is `url_files` followed by the path, with a limit of 1 MiB, saved to a temporary
    // file beside the tune's place (SPEC-132) after creating the folder; the card must have 8 MiB free.
    // The file must pass SPEC-203's check and have the MD5 the listing gives for the path; then it is
    // renamed to its name. Otherwise it is deleted and the fetch has failed. A path that is not an HVSC
    // path, or that the listing does not name, is never requested and nothing is written for it
    // (SPEC-154): fetch_failed() follows.
    //
    // A tune that is on the device already is not requested: fetched() follows at once. The one exception
    // is a tune that ChangedTunes::fetch_again() names (SPEC-148): it is requested, and replaces the file
    // if it arrives and passes; if that fails the file on the device stays and fetched() is called all the
    // same, so that it is played.
    FetchAnswer request(const std::string& path, FetchWhy why);

    // The player no longer wants the tune it asked for with Chosen, Stepped or Unattended (Stop, or a
    // step to a tune that is present): ends the wait and cancels the request if it is in flight.
    // Fetch-aheads and playlist files are not touched.
    void cancel_play();
    // "Stop" for "Fetch missing": forgets the playlist files not yet requested. The one in flight finishes.
    void cancel_playlist();

    // Whether a request for this path is in flight or about to be made: the browser's mark (SPEC-049).
    bool fetching(const std::string& path) const;
    // The message of the failure remembered for this path's fetch-ahead; "" if there is none.
    std::string ahead_failure(const std::string& path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spz
