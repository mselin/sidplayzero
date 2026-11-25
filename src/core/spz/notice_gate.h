// SPDX-License-Identifier: GPL-3.0-or-later
// The notice, and the gate (SPEC-152). While `notice_accepted` is off the app makes no request of any
// kind. Whatever a key press asks for that would make the first one goes through ask(): the notice panel
// opens, Enter accepts and what was asked for goes on, ESC cancels it. Nothing that happens by itself
// opens the panel: unattended code looks at accepted() and does without.
//
// Everything here runs on the UI thread.
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace spz {

// Where `notice_accepted` is kept: state.ini (SPEC-131). The state file belongs to another part of
// `core`; this is all the gate needs of it.
class NoticeRecord {
public:
    virtual ~NoticeRecord() = default;
    virtual bool notice_accepted() const = 0;
    // Sets `notice_accepted` to on, to be saved with the rest of the state.
    virtual void accept_notice() = 0;
};

// What shows the `notice-panel` (SPEC-114). The screens implement it; its Enter and ESC come back as
// NoticeGate::accept() and NoticeGate::cancel().
class NoticePanel {
public:
    virtual ~NoticePanel() = default;
    virtual void open_notice_panel(const std::string& text) = 0;
};

// The words of SPEC-152's notice, as one paragraph of UTF-8 with single spaces. The About screen shows
// the same (SPEC-103).
const std::string& hvsc_notice_text();

class NoticeGate {
public:
    NoticeGate(NoticeRecord* record, NoticePanel* panel);
    ~NoticeGate();
    NoticeGate(const NoticeGate&) = delete;
    NoticeGate& operator=(const NoticeGate&) = delete;

    // Whether requests may be made: `notice_accepted` is on.
    bool accepted() const;

    // For something a key press asked for. If the notice is accepted, runs `go_on` now and returns true.
    // If not, opens the notice panel with hvsc_notice_text(), keeps `go_on` and `cancelled`, and returns
    // false: nothing of what was asked for has happened yet.
    bool ask(std::function<void()> go_on, std::function<void()> cancelled = nullptr);

    // Whether the panel is open and something waits for its answer.
    bool asking() const;
    // Enter in the panel: sets `notice_accepted`, then runs the `go_on` that was kept. Nothing if not asking.
    void accept();
    // ESC in the panel: leaves `notice_accepted` off and runs the `cancelled` that was kept, if there was
    // one. What was asked for does not happen. Nothing if not asking.
    void cancel();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spz
