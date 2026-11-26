// SPDX-License-Identifier: GPL-3.0-or-later
// Fetching a tune when it is played (SPEC-141), with SPEC-143's 60 s, the limit on unattended requests,
// the gate of SPEC-152, the room on the card, and SPEC-154's rule for where a tune may be written.
//
// The state is shared between the UI thread (request(), the cancel calls, the timers, what is posted to
// the UI executor) and the fetch worker, under one mutex. The worker runs one "pump" job at a time, which
// takes the next fetch from the state until there is none: the tune to be played first, then the
// fetch-aheads, then the files of "Fetch missing". Nothing is posted to the fetch worker while a pump
// runs, so the UI thread and the worker never post to the same executor at once.
#include "spz/fetcher.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include "hvsc_files.h"
#include "spz/md5.h"
#include "spz/paths.h"
#include "spz/tune.h"

namespace spz {

using namespace hvsc_detail;

namespace {

// SPEC-069's failures that SPEC-143 rests after, when an unattended fetch or a fetch-ahead fails so.
bool rests_after(const std::string& message) {
    return message == "MSG_FETCH_NETWORK" || message == "MSG_FETCH_TOO_LARGE" || message == "MSG_CARD_FULL" ||
           message == "MSG_CANNOT_WRITE";
}

std::string message_of(HttpFailure failure) {
    switch (failure) {
        case HttpFailure::None: return "";
        case HttpFailure::NotFound: return "MSG_FETCH_NOT_FOUND";
        case HttpFailure::Network:
        case HttpFailure::Tls: return "MSG_FETCH_NETWORK";
        case HttpFailure::TooLarge: return "MSG_FETCH_TOO_LARGE";
        case HttpFailure::CannotWrite: return "MSG_CANNOT_WRITE";
        case HttpFailure::CardFull: return "MSG_CARD_FULL";
        case HttpFailure::Cancelled: break;
    }
    return "";
}

const std::int64_t kNoStamp = -1;

}  // namespace

bool fetch_ahead_due(Millis length_ms, Millis elapsed_ms) {
    return length_ms > 0 && length_ms - elapsed_ms <= kFetchAheadMs;
}

struct FetchState;

struct Fetcher::Impl {
    std::shared_ptr<FetchState> state;
};

// What a fetch came to.
struct Outcome {
    bool cancelled = false;
    bool requested = false;   // a request was made of the server
    bool arrived = false;     // a new file is at the tune's path
    std::string message;      // "" when the file can be played
};

struct FetchState : std::enable_shared_from_this<FetchState> {
    FetcherParts p;
    FetchListener* listener = nullptr;
    mutable std::mutex mutex;

    // The tune to be played: there is one at a time.
    enum class Play { None, Timer, Asking, Queued, Flying };
    Play play = Play::None;
    std::string play_path;
    FetchWhy play_why = FetchWhy::Chosen;
    std::int64_t play_stamp = kNoStamp;   // its place in the limit of unattended requests, if it has one
    Clock::TimerId timer = 0;
    std::uint64_t generation = 0;         // changes whenever the tune to be played is forgotten

    struct Queued {
        std::string path;
        std::int64_t stamp = kNoStamp;
    };
    std::deque<Queued> aheads, playlist;
    bool pump_posted = false;

    // The fetch in flight.
    bool flying = false;
    std::string fly_path;
    FetchWhy fly_why = FetchWhy::Chosen;
    std::int64_t fly_stamp = kNoStamp;
    std::atomic<bool> fly_cancel{false};

    std::set<std::string> present;                      // fetched, or found on the device, in this run
    std::map<std::string, std::string> ahead_failed;    // a fetch-ahead's failure, for the entry's turn
    std::deque<std::int64_t> unattended;                // when each unattended request of the last minute was asked for
    std::int64_t rest_until = -1;                       // SPEC-143

    // --- the limit of six unattended requests in any minute ---

    void forget_old(std::int64_t now) {
        while (!unattended.empty() && unattended.front() <= now - 60 * 1000) unattended.pop_front();
    }
    bool limit_allows(std::int64_t now) {
        forget_old(now);
        return unattended.size() < static_cast<std::size_t>(kFetchUnattendedInAMinute);
    }
    std::int64_t count_unattended(std::int64_t now) {
        unattended.push_back(now);
        return now;
    }
    // A counted request that was never made (the tune was on the device, or it was given up first) does
    // not count.
    void uncount(std::int64_t stamp) {
        if (stamp == kNoStamp) return;
        const auto found = std::find(unattended.begin(), unattended.end(), stamp);
        if (found != unattended.end()) unattended.erase(found);
    }

    // --- the fetch worker ---

    void ensure_pump() {
        if (pump_posted) return;
        pump_posted = true;
        std::shared_ptr<FetchState> self = shared_from_this();
        p.fetch_worker->post([self] { self->pump(); });
    }

    void pump() {
        // Whatever comes, the pump is never left marked as posted, nor a fetch as in flight: that would stop
        // every fetch after it (A8).
        struct Ended {
            explicit Ended(FetchState* of) : s(of) {}
            ~Ended() {
                if (clean) return;
                std::lock_guard<std::mutex> lock(s->mutex);
                s->pump_posted = false;
                s->flying = false;
                if (s->play == Play::Flying) s->play = Play::None;
            }
            FetchState* s;
            bool clean = false;
        } ended(this);
        for (;;) {
            std::string path;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (play == Play::Queued) {
                    path = play_path;
                    fly_why = play_why;
                    fly_stamp = play_stamp;
                    play_stamp = kNoStamp;
                    play = Play::Flying;
                } else if (!aheads.empty()) {
                    path = aheads.front().path;
                    fly_stamp = aheads.front().stamp;
                    fly_why = FetchWhy::Ahead;
                    aheads.pop_front();
                } else if (!playlist.empty()) {
                    path = playlist.front().path;
                    fly_stamp = kNoStamp;
                    fly_why = FetchWhy::Playlist;
                    playlist.pop_front();
                } else {
                    pump_posted = false;
                    ended.clean = true;
                    return;
                }
                flying = true;
                fly_path = path;
                fly_cancel = false;
            }
            // A fetch that throws (a seam that fails in a way it does not report) has failed, and is told so.
            Outcome outcome;
            try {
                outcome = fetch(path);
            } catch (...) {
                outcome = Outcome();
                outcome.message = "MSG_CANNOT_WRITE";
            }
            finished(path, outcome);
        }
    }

    // After a fetch: what is told, and to whom.
    void finished(const std::string& path, const Outcome& outcome) {
        std::lock_guard<std::mutex> lock(mutex);
        const FetchWhy why = fly_why;
        const bool was_play = play == Play::Flying && play_path == path;
        flying = false;
        if (!outcome.requested) uncount(fly_stamp);
        fly_stamp = kNoStamp;
        if (outcome.cancelled) {
            // Taken over after it had been cancelled: the tune to be played is asked for again.
            if (was_play && !fly_cancel_wanted_) {
                play = Play::Queued;
            } else if (was_play) {
                play = Play::None;
            }
            fly_cancel_wanted_ = false;
            return;
        }
        fly_cancel_wanted_ = false;
        if (was_play) play = Play::None;
        std::shared_ptr<FetchState> self = shared_from_this();
        if (outcome.message.empty()) {
            present.insert(path);
            ahead_failed.erase(path);
            const bool arrived = outcome.arrived;
            p.ui->post([self, path, why, arrived] {
                if (arrived && self->p.arrivals) self->p.arrivals->tune_arrived(path);
                if (self->listener) self->listener->fetched(path, why);
            });
            return;
        }
        if (why == FetchWhy::Ahead) ahead_failed[path] = outcome.message;
        if ((why == FetchWhy::Ahead || why == FetchWhy::Unattended) && rests_after(outcome.message)) {
            rest_until = p.clock->now_ms() + kFetchRestMs;
        }
        const std::string message = outcome.message;
        p.ui->post([self, path, why, message] {
            if (self->listener) self->listener->fetch_failed(path, why, message);
        });
    }

    // One fetch, on the fetch worker, with no lock held.
    Outcome fetch(const std::string& path) {
        Outcome outcome;
        ListedTune listed;
        // SPEC-154: only an HVSC path that the listing names is ever requested or written.
        if (!is_hvsc_path(path) || p.listing == nullptr || !p.listing->is_open() || !p.listing->by_path(path, &listed)) {
            outcome.message = "MSG_ENTRY_MISSING";
            return outcome;
        }
        const std::string target = p.root + path;
        FileInfo info;
        bool again = false;   // a present tune that an update found changed (SPEC-148)
        if (p.files->info(target, &info) == FileStatus::Ok) {
            if (p.changed == nullptr || !p.changed->fetch_again(path)) return outcome;
            again = true;
        }
        // From here on, a failure of a fetch again leaves the file on the device to be played.
        const auto failed = [&](const std::string& message) {
            outcome.message = again ? "" : message;
            return outcome;
        };
        std::uint64_t free_bytes = 0;
        if (p.files->free_bytes(target, &free_bytes) != FileStatus::Ok || free_bytes < kFetchRoomBytes) {
            return failed("MSG_CARD_FULL");
        }
        const std::string folder = parent_folder(target);
        if (p.files->make_folders(folder) != FileStatus::Ok) return failed("MSG_CANNOT_WRITE");
        if (fly_cancel) {
            outcome.cancelled = true;
            return outcome;
        }

        const std::string temporary = temp_beside(target);
        HttpRequest request;
        request.url = join_url(p.settings->url_files, path);
        request.to_path = temporary;
        request.limit_bytes = kTuneSizeLimit;
        outcome.requested = true;
        const HttpResult result =
            p.http->fetch(request, [this](std::uint64_t, std::uint64_t) { return !fly_cancel.load(); });
        const auto throw_away = [&] {
            FileInfo left;
            if (p.files->info(temporary, &left) == FileStatus::Ok) p.files->remove(temporary);
        };
        if (result.failure == HttpFailure::Cancelled || fly_cancel) {
            throw_away();
            outcome.cancelled = true;
            return outcome;
        }
        if (result.failure != HttpFailure::None) {
            throw_away();
            return failed(message_of(result.failure));
        }
        // The MD5 first, so that an answer that is no tune at all is a mismatch (FIND-021); then the
        // check of SPEC-203.
        Bytes bytes;
        const FileStatus read = p.files->read(temporary, kTuneSizeLimit, &bytes);
        std::string message;
        if (read == FileStatus::TooLarge) message = "MSG_FETCH_TOO_LARGE";
        else if (read != FileStatus::Ok) message = "MSG_CANNOT_WRITE";
        else if (md5_hex(bytes) != listed.md5) message = "MSG_FETCH_MISMATCH";
        else if (!check_tune(bytes).empty()) message = check_tune(bytes);
        if (!message.empty()) {
            throw_away();
            return failed(message);
        }
        if (commit_temporary(p.files, temporary, target) != FileStatus::Ok) return failed("MSG_CANNOT_WRITE");
        if (again) p.changed->fetched_again(path);
        outcome.arrived = true;
        return outcome;
    }

    // --- the tune to be played, under the lock ---

    // Forgets the tune to be played: its wait, its question, and its request if that is in flight. A
    // fetch-ahead in flight is cancelled too, unless `keep` is its path; a playlist's file never is.
    void forget_play(const std::string& keep) {
        if (timer != 0) p.clock->cancel(timer);
        timer = 0;
        ++generation;
        uncount(play_stamp);
        play_stamp = kNoStamp;
        if (flying && fly_why != FetchWhy::Playlist && fly_path != keep) {
            fly_cancel = true;
            fly_cancel_wanted_ = true;
        }
        play = Play::None;
        // Fetch-aheads not yet begun belong to the tune that was playing; they go too.
        for (const Queued& queued : aheads) {
            if (queued.path != keep) uncount(queued.stamp);
        }
        aheads.erase(std::remove_if(aheads.begin(), aheads.end(), [&](const Queued& q) { return q.path != keep; }),
                     aheads.end());
    }

    void queue_play(const std::string& path, FetchWhy why, std::int64_t stamp, bool tell_started) {
        play = Play::Queued;
        play_path = path;
        play_why = why;
        play_stamp = stamp;
        // A fetch-ahead of it that has not begun is this request now.
        for (const Queued& queued : aheads) {
            if (queued.path == path) uncount(queued.stamp);
        }
        aheads.erase(std::remove_if(aheads.begin(), aheads.end(), [&](const Queued& q) { return q.path == path; }),
                     aheads.end());
        if (tell_started) {
            std::shared_ptr<FetchState> self = shared_from_this();
            p.ui->post([self, path] {
                if (self->listener) self->listener->fetch_started(path);
            });
        }
        ensure_pump();
    }

    // The notice, for a request that a key asked for (SPEC-152). Called with the lock held; unlocks it.
    void ask_notice(std::unique_lock<std::mutex>* lock, const std::string& path, FetchWhy why) {
        play = Play::Asking;
        play_path = path;
        play_why = why;
        const std::uint64_t asked = generation;
        std::shared_ptr<FetchState> self = shared_from_this();
        lock->unlock();
        p.gate->ask(
            [self, path, why, asked] {
                std::lock_guard<std::mutex> again(self->mutex);
                if (self->generation != asked || self->play != Play::Asking) return;
                self->queue_play(path, why, kNoStamp, true);
            },
            [self, path, asked] {
                {
                    std::lock_guard<std::mutex> again(self->mutex);
                    if (self->generation != asked || self->play != Play::Asking) return;
                    self->play = Play::None;
                }
                self->p.ui->post([self, path] {
                    if (self->listener) self->listener->fetch_cancelled(path);
                });
            });
    }

    // A Stepped tune's 700 ms are over, or an Unattended one's turn in the limit has come.
    void wait_over(const std::string& path, FetchWhy why, std::uint64_t waited) {
        std::unique_lock<std::mutex> lock(mutex);
        if (generation != waited || play != Play::Timer || play_path != path) return;
        timer = 0;
        if (why == FetchWhy::Stepped && !p.gate->accepted()) {
            ask_notice(&lock, path, why);
            return;
        }
        std::int64_t stamp = kNoStamp;
        if (why == FetchWhy::Unattended) {
            const std::int64_t now = p.clock->now_ms();
            if (!limit_allows(now)) {   // not yet after all: wait on
                start_waiting(path, why, now);
                return;
            }
            stamp = count_unattended(now);
        }
        queue_play(path, why, stamp, true);
    }

    void start_waiting(const std::string& path, FetchWhy why, std::int64_t now) {
        play = Play::Timer;
        play_path = path;
        play_why = why;
        Millis wait = kFetchStepWaitMs;
        if (why == FetchWhy::Unattended) {
            forget_old(now);
            wait = unattended.empty() ? 0 : unattended.front() + 60 * 1000 - now;
            if (wait < 1) wait = 1;
        }
        const std::uint64_t waited = generation;
        std::weak_ptr<FetchState> weak = shared_from_this();
        timer = p.clock->call_after(wait, [weak, path, why, waited] {
            if (std::shared_ptr<FetchState> self = weak.lock()) self->wait_over(path, why, waited);
        });
    }

    bool fly_cancel_wanted_ = false;   // the fetch in flight was cancelled by the UI thread, not taken over
};

Fetcher::Fetcher(const FetcherParts& parts) : impl_(new Impl) {
    impl_->state = std::make_shared<FetchState>();
    impl_->state->p = parts;
}

Fetcher::~Fetcher() {
    FetchState& s = *impl_->state;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.timer != 0) s.p.clock->cancel(s.timer);
    s.timer = 0;
    s.listener = nullptr;
    s.fly_cancel = true;
    s.aheads.clear();
    s.playlist.clear();
    s.play = FetchState::Play::None;
}

void Fetcher::set_listener(FetchListener* listener) {
    std::lock_guard<std::mutex> lock(impl_->state->mutex);
    impl_->state->listener = listener;
}

FetchAnswer Fetcher::request(const std::string& path, FetchWhy why) {
    FetchState& s = *impl_->state;
    std::unique_lock<std::mutex> lock(s.mutex);
    const std::int64_t now = s.p.clock->now_ms();

    if (why == FetchWhy::Playlist) {
        s.playlist.push_back({path, kNoStamp});
        s.ensure_pump();
        return FetchAnswer::Fetching;
    }

    if (why == FetchWhy::Ahead) {
        if (s.present.count(path) > 0) return FetchAnswer::Present;
        if (s.flying && s.fly_path == path) return FetchAnswer::Fetching;
        if ((s.play == FetchState::Play::Queued || s.play == FetchState::Play::Flying) && s.play_path == path) {
            return FetchAnswer::Fetching;
        }
        for (const FetchState::Queued& queued : s.aheads) {
            if (queued.path == path) return FetchAnswer::Fetching;
        }
        if (!s.p.gate->accepted()) return FetchAnswer::NoticeNeeded;
        if (now < s.rest_until) return FetchAnswer::Resting;
        if (!s.limit_allows(now)) return FetchAnswer::OverLimit;
        s.ahead_failed.erase(path);
        s.aheads.push_back({path, s.count_unattended(now)});
        s.ensure_pump();
        return FetchAnswer::Fetching;
    }

    // The tune to be played. The one before it is forgotten, unless it is this one.
    if (s.present.count(path) > 0) {
        s.forget_play("");
        return FetchAnswer::Present;
    }
    if (s.flying && s.fly_path == path && s.fly_why != FetchWhy::Playlist) {
        // Taken over: the request in flight is this tune's, and what it comes to is told as `why`.
        s.forget_play(path);
        s.fly_cancel = false;
        s.fly_cancel_wanted_ = false;
        s.fly_why = why;
        s.play = FetchState::Play::Flying;
        s.play_path = path;
        s.play_why = why;
        return FetchAnswer::Fetching;
    }
    if (why == FetchWhy::Unattended) {
        const auto failed = s.ahead_failed.find(path);
        if (failed != s.ahead_failed.end()) {
            s.forget_play("");
            return FetchAnswer::Failed;
        }
        if (!s.p.gate->accepted()) {
            s.forget_play("");
            return FetchAnswer::NoticeNeeded;
        }
        if (now < s.rest_until) {
            s.forget_play("");
            return FetchAnswer::Resting;
        }
    }
    s.forget_play(path);

    switch (why) {
        case FetchWhy::Unattended: {
            // A fetch-ahead of this tune that has not begun becomes its request, with its place in the limit.
            const auto ahead = std::find_if(s.aheads.begin(), s.aheads.end(),
                                            [&](const FetchState::Queued& q) { return q.path == path; });
            if (ahead != s.aheads.end()) {
                const std::int64_t stamp = ahead->stamp;
                s.aheads.erase(ahead);
                s.queue_play(path, why, stamp, false);
                return FetchAnswer::Fetching;
            }
            if (!s.limit_allows(now)) {
                s.start_waiting(path, why, now);
                return FetchAnswer::Waiting;
            }
            s.queue_play(path, why, s.count_unattended(now), false);
            return FetchAnswer::Fetching;
        }
        case FetchWhy::Stepped:
            s.start_waiting(path, why, now);
            return FetchAnswer::Waiting;
        default:   // Chosen
            if (!s.p.gate->accepted()) {
                s.ask_notice(&lock, path, why);
                return FetchAnswer::AskingNotice;
            }
            s.queue_play(path, why, kNoStamp, false);
            return FetchAnswer::Fetching;
    }
}

void Fetcher::cancel_play() {
    FetchState& s = *impl_->state;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.timer != 0) s.p.clock->cancel(s.timer);
    s.timer = 0;
    ++s.generation;
    s.uncount(s.play_stamp);
    s.play_stamp = kNoStamp;
    if (s.play == FetchState::Play::Flying && s.flying && s.fly_path == s.play_path) {
        s.fly_cancel = true;
        s.fly_cancel_wanted_ = true;
    }
    s.play = FetchState::Play::None;
}

void Fetcher::cancel_playlist() {
    std::lock_guard<std::mutex> lock(impl_->state->mutex);
    impl_->state->playlist.clear();
}

bool Fetcher::fetching(const std::string& path) const {
    const FetchState& s = *impl_->state;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.flying && s.fly_path == path && !s.fly_cancel) return true;
    if (s.play != FetchState::Play::None && s.play_path == path) return true;
    for (const FetchState::Queued& queued : s.aheads) {
        if (queued.path == path) return true;
    }
    for (const FetchState::Queued& queued : s.playlist) {
        if (queued.path == path) return true;
    }
    return false;
}

std::string Fetcher::ahead_failure(const std::string& path) const {
    const FetchState& s = *impl_->state;
    std::lock_guard<std::mutex> lock(s.mutex);
    const auto found = s.ahead_failed.find(path);
    return found == s.ahead_failed.end() ? "" : found->second;
}

}  // namespace spz
