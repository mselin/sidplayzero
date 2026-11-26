// SPDX-License-Identifier: GPL-3.0-or-later
// The notice, and the gate (SPEC-152).
#include "spz/notice_gate.h"

#include <utility>

namespace spz {

struct NoticeGate::Impl {
    NoticeRecord* record = nullptr;
    NoticePanel* panel = nullptr;
    bool asking = false;
    std::function<void()> go_on;
    std::function<void()> cancelled;
};

const std::string& hvsc_notice_text() {
    static const std::string text =
        "SidPlayZero can fetch music from the High Voltage SID Collection (HVSC), a free collection kept by the "
        "HVSC crew at hvsc.c64.org. HVSC says of its tunes: \"All of the tunes available in the High Voltage SID "
        "Collection (HVSC) are copyrighted.\" and \"Use of the SIDs in this collection for any purpose other than "
        "private enjoyment is subject to relevant copyright law.\" SidPlayZero downloads files to this device only "
        "when you ask, from the addresses in Settings: HVSC's own site, and the mirror its site links to, unless "
        "you change them. It does not contain HVSC's files and does not pass them on.";
    return text;
}

NoticeGate::NoticeGate() : impl_(new Impl) {}

NoticeGate::NoticeGate(NoticeRecord* record, NoticePanel* panel) : impl_(new Impl) {
    impl_->record = record;
    impl_->panel = panel;
}

NoticeGate::~NoticeGate() = default;

bool NoticeGate::accepted() const { return impl_->record != nullptr && impl_->record->notice_accepted(); }

bool NoticeGate::ask(std::function<void()> go_on, std::function<void()> cancelled) {
    if (accepted()) {
        if (go_on) go_on();
        return true;
    }
    // A second question while one is open replaces it: the panel shows one question at a time, and the
    // answer belongs to the last key that asked.
    impl_->go_on = std::move(go_on);
    impl_->cancelled = std::move(cancelled);
    impl_->asking = true;
    if (impl_->panel != nullptr) impl_->panel->open_notice_panel(hvsc_notice_text());
    return false;
}

bool NoticeGate::asking() const { return impl_->asking; }

void NoticeGate::accept() {
    if (!impl_->asking) return;
    impl_->asking = false;
    if (impl_->record != nullptr) impl_->record->accept_notice();
    std::function<void()> go_on = std::move(impl_->go_on);
    impl_->go_on = nullptr;
    impl_->cancelled = nullptr;
    if (go_on) go_on();
}

void NoticeGate::cancel() {
    if (!impl_->asking) return;
    impl_->asking = false;
    std::function<void()> cancelled = std::move(impl_->cancelled);
    impl_->go_on = nullptr;
    impl_->cancelled = nullptr;
    if (cancelled) cancelled();
}

void NoticeGate::pass(std::function<void()> go, std::function<void()> cancelled) {
    ask(std::move(go), std::move(cancelled));
}

}  // namespace spz
