// SPDX-License-Identifier: GPL-3.0-or-later
// "Fetch missing" (SPEC-174).
#include "spz/playlist_fetch.h"

#include <algorithm>
#include <utility>

namespace spz {

FetchMissing::FetchMissing(Clock* clock, Files* files, const std::string& root, const EntryResolver* resolver,
                           PlaylistTuneFetcher* fetcher, NoticeGate* gate, FetchMissingListener* listener)
    : clock_(clock), files_(files), root_(root), resolver_(resolver), fetcher_(fetcher), gate_(gate),
      listener_(listener) {}

FetchMissing::~FetchMissing() {
    if (timer_set_) clock_->cancel(timer_);
}

void FetchMissing::start(const std::vector<Entry>& entries) {
    if (state_.running || waiting_for_gate_) return;
    const Lifetime::Token alive = lifetime_.token();
    auto go_on = [this, alive, entries] {
        if (!alive.alive()) return;
        waiting_for_gate_ = false;
        go(entries);
    };
    auto cancelled = [this, alive] {
        if (!alive.alive()) return;
        waiting_for_gate_ = false;   // ESC: nothing was asked for, and nothing is
    };
    if (gate_ == nullptr) {
        go(entries);
        return;
    }
    waiting_for_gate_ = true;
    gate_->pass(go_on, cancelled);
}

void FetchMissing::go(const std::vector<Entry>& entries) {
    todo_.clear();
    for (const Entry& entry : entries) {
        if (resolver_ == nullptr) break;
        const EntryFacts facts = resolver_->resolve(entry);
        if (facts.found != Found::Absent) continue;
        if (std::find(todo_.begin(), todo_.end(), facts.path) == todo_.end()) todo_.push_back(facts.path);
    }
    absent_ = static_cast<int>(todo_.size());
    if (todo_.size() > static_cast<std::size_t>(kMostInOneGo)) todo_.resize(kMostInOneGo);
    fetched_ = 0;
    stopping_ = false;
    in_flight_ = false;
    state_.running = true;
    state_.done = 0;
    state_.total = static_cast<int>(todo_.size());
    if (listener_ != nullptr) listener_->fetch_missing_progress(state_);
    ask_next();
}

void FetchMissing::ask_next() {
    timer_set_ = false;
    if (!state_.running) return;
    if (stopping_) {
        finish("");
        return;
    }
    if (state_.done >= state_.total) {
        finish("MSG_FETCHED_SOME");
        return;
    }
    // SPEC-141: no request without room for it on the card.
    std::uint64_t free = 0;
    if (files_->free_bytes(root_, &free) != FileStatus::Ok || free < kRoomNeeded) {
        finish("MSG_CARD_FULL");
        return;
    }
    in_flight_ = true;
    const Lifetime::Token alive = lifetime_.token();
    const std::string path = todo_[static_cast<std::size_t>(state_.done)];
    fetcher_->fetch_for_playlist(path, [this, alive](const std::string& failure) {
        if (!alive.alive()) return;
        file_done(failure);
    });
}

void FetchMissing::file_done(const std::string& failure) {
    if (!in_flight_) return;
    in_flight_ = false;
    ++state_.done;
    if (failure.empty()) ++fetched_;
    if (listener_ != nullptr) listener_->fetch_missing_progress(state_);
    if (failure == "MSG_FETCH_NETWORK") {
        finish(failure);   // no network: the next file would fail the same way
        return;
    }
    if (stopping_) {
        finish("");
        return;
    }
    if (state_.done >= state_.total) {
        finish("MSG_FETCHED_SOME");
        return;
    }
    const Lifetime::Token alive = lifetime_.token();
    timer_set_ = true;
    timer_ = clock_->call_after(kPauseMs, [this, alive] {
        if (!alive.alive()) return;
        ask_next();
    });
}

void FetchMissing::finish(const std::string& message) {
    if (timer_set_) {
        clock_->cancel(timer_);
        timer_set_ = false;
    }
    state_.running = false;
    in_flight_ = false;
    stopping_ = false;
    ListsResult notice;
    notice.ok = message == "MSG_FETCHED_SOME";
    notice.message = message;
    notice.about["n"] = std::to_string(fetched_);
    notice.about["m"] = std::to_string(absent_ - fetched_);
    if (listener_ != nullptr) listener_->fetch_missing_ended(notice);
}

void FetchMissing::stop() {
    if (!state_.running) return;
    if (in_flight_) {
        stopping_ = true;   // the file in flight is finished first
        return;
    }
    finish("");
}

FetchMissingState FetchMissing::state() const { return state_; }

}  // namespace spz
