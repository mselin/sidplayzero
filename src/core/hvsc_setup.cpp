// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/hvsc_setup.h"

#include "spz/not_built.h"

namespace spz {

struct HvscSetup::Impl {};

int read_release(const Bytes&) { SPZ_NOT_BUILT("SPEC-148"); }
HvscStatus hvsc_status(Files*, const Listing&, const std::string&) { SPZ_NOT_BUILT("SPEC-140"); }
Room check_room(Files*, const std::string&, std::uint64_t) { SPZ_NOT_BUILT("SPEC-146"); }

HvscSetup::HvscSetup(const HvscSetupParts&) {}
HvscSetup::~HvscSetup() = default;
bool HvscSetup::start() { SPZ_NOT_BUILT("SPEC-140"); }

}  // namespace spz
