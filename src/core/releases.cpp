// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/releases.h"

#include "spz/not_built.h"

namespace spz {

struct Releases::Impl {};

Releases::Releases(const ReleasesParts&) {}
Releases::~Releases() = default;
void Releases::check() { SPZ_NOT_BUILT("SPEC-148"); }
int Releases::newer() const { SPZ_NOT_BUILT("SPEC-148"); }
bool Releases::update() { SPZ_NOT_BUILT("SPEC-148"); }
bool Releases::fetch_again(const std::string&) { SPZ_NOT_BUILT("SPEC-148"); }
void Releases::fetched_again(const std::string&) { SPZ_NOT_BUILT("SPEC-148"); }

}  // namespace spz
