// SPDX-License-Identifier: GPL-3.0-or-later
// Notices (SPEC-113): lines of text shown for a few seconds, one at a time, without taking the keys. This
// is their queue: what is showing, what waits, and for how long. Drawing the line is the screen's work.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "spz/clock.h"
#include "spz/types.h"

namespace spz {

const Millis kNoticeMs = 3000;            // SPEC-113: how long a notice shows
const Millis kNoticeWithKeyMs = 10000;    // SPEC-113: how long MSG_TOO_HEAVY shows, which offers a key
const std::size_t kNoticesWaiting = 5;    // SPEC-113: at most this many wait; the oldest is dropped

struct Notice {
    std::string id;      // an identifier of SPEC-116, such as "MSG_VOLUME"
    std::string text;    // its words, with what it is about filled in: "Volume 45 %"
};

class NoticeQueue {
public:
    // The time a notice shows is counted on `clock`, by timers that fire on the UI thread.
    explicit NoticeQueue(Clock* clock);
    ~NoticeQueue();
    NoticeQueue(const NoticeQueue&) = delete;
    NoticeQueue& operator=(const NoticeQueue&) = delete;

    // Raises a notice. With none showing it shows at once, for kNoticeMs, or kNoticeWithKeyMs if it is
    // MSG_TOO_HEAVY. With the same identifier as the one showing, it takes that one's place at once and its
    // time starts again. With the same identifier as one that waits, it takes that one's place in the
    // line. Otherwise it waits its turn behind the others; when that makes more than kNoticesWaiting wait,
    // the one that has waited longest is dropped.
    void raise(const std::string& id, const std::string& text);

    // SPEC-106, FIND-021: MSG_HOLD_TO_LEAVE does not wait its turn. It shows at once, in place of the one
    // showing, which goes back to the head of the line and has its turn again, from the start, after it.
    void show_now(const std::string& id, const std::string& text);

    // Takes a notice away before its time is up, whether it shows or waits: MSG_HOLD_TO_LEAVE when ESC is
    // let go (SPEC-106). The next in line then has its turn. Does nothing if there is no such notice.
    void remove(const std::string& id);

    // The notice showing now, or nullptr. The pointer holds until the next call that is not const.
    const Notice* showing() const;
    // Those waiting their turn, the next one first.
    std::vector<Notice> waiting() const;

    // `fn` is called each time what showing() returns has changed.
    void on_change(std::function<void()> fn);

private:
    void show_next();
    void start_timer();
    void changed();

    Clock* clock_;
    bool has_showing_ = false;
    Notice showing_;
    std::vector<Notice> waiting_;
    Clock::TimerId timer_ = 0;
    std::function<void()> on_change_;
};

}  // namespace spz
