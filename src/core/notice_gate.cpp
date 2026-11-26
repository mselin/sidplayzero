// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/notice_gate.h"

#include <utility>

#include "spz/not_built.h"

namespace spz {

struct NoticeGate::Impl {};

const std::string& hvsc_notice_text() { SPZ_NOT_BUILT("SPEC-152"); }

NoticeGate::NoticeGate() = default;
NoticeGate::NoticeGate(NoticeRecord*, NoticePanel*) {}
NoticeGate::~NoticeGate() = default;
void NoticeGate::pass(std::function<void()> go, std::function<void()> cancelled) { ask(std::move(go), std::move(cancelled)); }
bool NoticeGate::accepted() const { SPZ_NOT_BUILT("SPEC-152"); }
bool NoticeGate::ask(std::function<void()>, std::function<void()>) { SPZ_NOT_BUILT("SPEC-152"); }
bool NoticeGate::asking() const { SPZ_NOT_BUILT("SPEC-152"); }
void NoticeGate::accept() { SPZ_NOT_BUILT("SPEC-152"); }
void NoticeGate::cancel() { SPZ_NOT_BUILT("SPEC-152"); }

}  // namespace spz
