// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/playlist_resolve.h"

#include "spz/not_built.h"

namespace spz {

EntryResolver::EntryResolver(Files* files, const std::string& root, const Listing* listing, const OwnTunes* own)
    : files_(files), root_(root), listing_(listing), own_(own) {}

EntryFacts EntryResolver::resolve(const Entry&) const { SPZ_NOT_BUILT("SPEC-173"); }
EntryFacts EntryResolver::facts(const Entry&) const { SPZ_NOT_BUILT("SPEC-173"); }

}  // namespace spz
