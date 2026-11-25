// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/notices.h"

#include "spz/not_built.h"

namespace spz {

NoticeQueue::NoticeQueue(Clock* clock) : clock_(clock) {}
NoticeQueue::~NoticeQueue() {}

void NoticeQueue::raise(const std::string&, const std::string&) {
    (void)clock_;
    SPZ_NOT_BUILT("SPEC-113");
}
void NoticeQueue::remove(const std::string&) { SPZ_NOT_BUILT("SPEC-113"); }
const Notice* NoticeQueue::showing() const { SPZ_NOT_BUILT("SPEC-113"); }
std::vector<Notice> NoticeQueue::waiting() const { SPZ_NOT_BUILT("SPEC-113"); }
void NoticeQueue::on_change(std::function<void()>) { SPZ_NOT_BUILT("SPEC-113"); }

}  // namespace spz
