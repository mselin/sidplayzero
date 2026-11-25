// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/screen_stack.h"

#include "spz/not_built.h"

namespace spz {

ScreenStack::ScreenStack() {}

std::vector<std::string> ScreenStack::screens() const { SPZ_NOT_BUILT("SPEC-090"); }
std::string ScreenStack::top() const { SPZ_NOT_BUILT("SPEC-090"); }
void ScreenStack::open(const std::string&) { SPZ_NOT_BUILT("SPEC-090"); }
bool ScreenStack::close_top() { SPZ_NOT_BUILT("SPEC-090"); }
void ScreenStack::set_bottom(const std::string&) { SPZ_NOT_BUILT("SPEC-090"); }

}  // namespace spz
