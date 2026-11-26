// SPDX-License-Identifier: GPL-3.0-or-later
// What the player asks of whatever fetches absent tunes (SPEC-141, SPEC-143). The player decides when a
// tune is wanted: at once for a tune chosen with Enter, after the 700 ms wait for one stepped to, and
// 10 s before a song's end for the one that follows it. How it is fetched belongs behind this interface:
// the request, the check against the listing, the file put in its place, the fetch worker, the limit of
// six unattended requests a minute, the 60 s after a failure, and HVSC's notice.
//
// Every function is called on the UI thread, and every `done` is called on the UI thread.
#pragma once

#include <functional>
#include <string>

#include "spz/types.h"

namespace spz {

// What became of a request.
struct FetchResult {
    bool fetched = false;    // the file is in its place in the collection, checked: the tune is present
    std::string message;     // if not fetched, why (SPEC-069): "MSG_FETCH_NOT_FOUND", "MSG_FETCH_MISMATCH",
                             // "MSG_FETCH_NETWORK", "MSG_FETCH_TOO_LARGE", "MSG_CARD_FULL" or "MSG_CANNOT_WRITE"
};

// Whether an absent tune may be requested when no key asked for it: an entry reached by a song ending,
// or the fetch-ahead.
enum class UnattendedFetch {
    Allowed,
    Later,          // six unattended requests were made in the last minute (SPEC-141): the player waits,
                    // in Waiting, for the time given, and asks again
    NotNow,         // within 60 s of an unattended fetch that failed (SPEC-143): the entry is passed over
    NoticeNeeded,   // HVSC's notice is not accepted (SPEC-152): the entry is passed over, and the player
                    // raises the notice MSG_NOTICE_NEEDED
};

class TuneFetcher {
public:
    using Done = std::function<void(const FetchResult&)>;

    virtual ~TuneFetcher() = default;

    // Asked before every unattended request. With Later, `wait_ms` is set to how long to wait.
    virtual UnattendedFetch unattended(Millis* wait_ms) = 0;

    // Requests the tune at an HVSC path, to be played as soon as it is there. `attended` is true when a
    // key asked for the tune (Enter, or a step and its wait), false when a song's end reached it.
    // It goes ahead of everything waiting, and cancels a fetch-ahead or an earlier request made here
    // that is still in flight, unless that is for this same path: then it takes that request over, with
    // no second request, and `done` replaces the one given before.
    // `done` is called once, when the fetch has succeeded or failed; never after cancel().
    virtual void fetch(const std::string& path, bool attended, Done done) = 0;

    // The fetch-ahead (SPEC-141): requests the tune that will be played next, while the present one
    // still plays. `done` is called once with the outcome, unless fetch() takes the request over or
    // cancel() comes first.
    virtual void fetch_ahead(const std::string& path, Done done) = 0;

    // Gives up whatever fetch() and fetch_ahead() have waiting or in flight. Nothing is called back.
    virtual void cancel() = 0;
};

}  // namespace spz
