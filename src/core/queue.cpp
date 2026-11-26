// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/queue.h"

#include "spz/not_built.h"

namespace spz {

Queue::Queue(ShuffleGenerator* generator) : generator_(generator) {}
void Queue::set(const std::vector<Entry>&, std::size_t, bool) { SPZ_NOT_BUILT("SPEC-062"); }
void Queue::clear() { SPZ_NOT_BUILT("SPEC-062"); }
bool Queue::empty() const { SPZ_NOT_BUILT("SPEC-062"); }
std::size_t Queue::size() const { SPZ_NOT_BUILT("SPEC-062"); }
const std::vector<Entry>& Queue::order() const { SPZ_NOT_BUILT("SPEC-062"); }
std::size_t Queue::place() const { SPZ_NOT_BUILT("SPEC-062"); }
const std::vector<Entry>& Queue::original() const { SPZ_NOT_BUILT("SPEC-062"); }
const Entry& Queue::current() const { SPZ_NOT_BUILT("SPEC-062"); }
bool Queue::next(bool) { SPZ_NOT_BUILT("SPEC-062"); }
void Queue::previous() { SPZ_NOT_BUILT("SPEC-062"); }
bool Queue::peek_next(bool, Entry*) { SPZ_NOT_BUILT("SPEC-062"); }
void Queue::set_shuffle(bool) { SPZ_NOT_BUILT("SPEC-065"); }
bool Queue::shuffled() const { SPZ_NOT_BUILT("SPEC-065"); }

}  // namespace spz
