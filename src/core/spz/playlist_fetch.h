// SPDX-License-Identifier: GPL-3.0-or-later
// "Fetch missing" (SPEC-174, part `playlists`): the job that fetches a playlist's absent entries, one
// after another. It lives on the UI thread and owns no thread: each file is asked of the fetcher, which
// runs it on the fetch worker (SPEC-022), and the pause between two files is a timer of the clock.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "spz/clock.h"
#include "spz/files.h"
#include "spz/playlist_resolve.h"
#include "spz/playlists.h"
#include "spz/types.h"

namespace spz {

// What "Fetch missing" needs of the fetcher of SPEC-141, which belongs to another part of `core`: one
// tune fetched, and how it went.
class TuneFetcher {
public:
    // Called on the UI thread when the fetch is over: "" if the tune is present now, else the failure's
    // message identifier by SPEC-069's table, "MSG_FETCH_NETWORK" for lack of network.
    using Done = std::function<void(const std::string& failure)>;

    virtual ~TuneFetcher() = default;
    // Fetches the tune at an HVSC path as SPEC-141 does (requested from `url_files`, checked against the
    // listing, renamed into place, added to the index), as a file of "Fetch missing": on the fetch
    // worker, after whatever is in flight there, and after any tune that is to be played.
    virtual void fetch_for_playlist(const std::string& hvsc_path, Done done) = 0;
};

// The gate of SPEC-152, which belongs to another part of the app: nothing uses the network until the
// user has accepted HVSC's notice.
class NoticeGate {
public:
    virtual ~NoticeGate() = default;
    // Calls `go` at once if the notice has been accepted. If not, opens the notice panel: Enter accepts
    // and calls `go`; ESC calls `cancelled`, and what was asked for does not happen.
    virtual void pass(std::function<void()> go, std::function<void()> cancelled) = 0;
};

struct FetchMissingState {
    bool running = false;
    int done = 0;      // "n of m": files dealt with so far, and files this go will fetch
    int total = 0;
};

class FetchMissingListener {
public:
    virtual ~FetchMissingListener() = default;
    virtual void fetch_missing_progress(const FetchMissingState& state) = 0;
    // The job is over. When it ran to its end, `notice` is MSG_FETCHED_SOME with n and m.
    virtual void fetch_missing_ended(const ListsResult& notice) = 0;
};

class FetchMissing {
public:
    static constexpr int kMostInOneGo = 200;                       // SPEC-174 (a proposed budget)
    static constexpr Millis kPauseMs = 250;                        // between two files (a proposed budget)
    static constexpr std::uint64_t kRoomNeeded = 8 * 1024 * 1024;    // SPEC-141: free bytes before a request

    FetchMissing(Clock* clock, Files* files, const std::string& root, const EntryResolver* resolver,
                 TuneFetcher* fetcher, NoticeGate* gate, FetchMissingListener* listener);

    // "Fetch missing" on a playlist's entries. Passes the notice gate first; then fetches the entries
    // that are absent, in their order, at most kMostInOneGo of them, each asked for only when the one
    // before is over and kPauseMs have passed, and only while the card has kRoomNeeded free. A failure
    // for lack of network ends the job; any other failure leaves that tune absent and goes on.
    void start(const std::vector<Entry>& entries);
    // "Stop": the job ends after the file in flight.
    void stop();
    FetchMissingState state() const;

private:
    Clock* clock_;
    Files* files_;
    std::string root_;
    const EntryResolver* resolver_;
    TuneFetcher* fetcher_;
    NoticeGate* gate_;
    FetchMissingListener* listener_;
};

}  // namespace spz
