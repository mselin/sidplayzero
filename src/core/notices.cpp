// SPDX-License-Identifier: GPL-3.0-or-later
// The queue of notices (SPEC-113).
#include "spz/notices.h"

#include <utility>

namespace spz {

NoticeQueue::NoticeQueue(Clock* clock) : clock_(clock) {}

NoticeQueue::~NoticeQueue() {
    if (timer_ != 0) clock_->cancel(timer_);
}

void NoticeQueue::changed() {
    if (on_change_) on_change_();
}

void NoticeQueue::start_timer() {
    if (timer_ != 0) clock_->cancel(timer_);
    const Millis ms = showing_.id == "MSG_TOO_HEAVY" ? kNoticeWithKeyMs : kNoticeMs;
    timer_ = clock_->call_after(ms, [this] {
        timer_ = 0;
        show_next();
    });
}

void NoticeQueue::show_next() {
    if (timer_ != 0) clock_->cancel(timer_);
    timer_ = 0;
    if (waiting_.empty()) {
        has_showing_ = false;
        showing_ = Notice();
    } else {
        has_showing_ = true;
        showing_answers_key_ = false;
        showing_ = waiting_.front();
        waiting_.erase(waiting_.begin());
        start_timer();
    }
    changed();
}

void NoticeQueue::raise(const std::string& id, const std::string& text) {
    if (!has_showing_ || showing_.id == id) {
        has_showing_ = true;
        showing_answers_key_ = false;
        showing_ = Notice{id, text};
        start_timer();
        changed();
        return;
    }
    for (Notice& notice : waiting_) {
        if (notice.id == id) {
            notice.text = text;
            return;
        }
    }
    waiting_.push_back(Notice{id, text});
    if (waiting_.size() > kNoticesWaiting) waiting_.erase(waiting_.begin());
}

void NoticeQueue::show_now(const std::string& id, const std::string& text) {
    if (has_showing_ && showing_.id != id) {
        for (auto it = waiting_.begin(); it != waiting_.end(); ++it) {
            if (it->id == showing_.id) {
                waiting_.erase(it);
                break;
            }
        }
        waiting_.insert(waiting_.begin(), showing_);
        if (waiting_.size() > kNoticesWaiting) waiting_.pop_back();
    }
    for (auto it = waiting_.begin(); it != waiting_.end(); ++it) {
        if (it->id == id) {
            waiting_.erase(it);
            break;
        }
    }
    has_showing_ = true;
    showing_answers_key_ = false;
    showing_ = Notice{id, text};
    start_timer();
    changed();
}

void NoticeQueue::answer(const std::string& id, const std::string& text) {
    if (has_showing_ && showing_answers_key_ && showing_.id != id) {
        has_showing_ = false;        // the answer to an earlier key is done with; show_now() puts nothing back
    }
    show_now(id, text);
    showing_answers_key_ = true;
}

void NoticeQueue::remove(const std::string& id) {
    if (has_showing_ && showing_.id == id) {
        show_next();
        return;
    }
    for (auto it = waiting_.begin(); it != waiting_.end(); ++it) {
        if (it->id == id) {
            waiting_.erase(it);
            return;
        }
    }
}

const Notice* NoticeQueue::showing() const { return has_showing_ ? &showing_ : nullptr; }

std::vector<Notice> NoticeQueue::waiting() const { return waiting_; }

void NoticeQueue::on_change(std::function<void()> fn) { on_change_ = std::move(fn); }

}  // namespace spz
