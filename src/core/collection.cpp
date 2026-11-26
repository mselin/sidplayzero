// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/collection.h"

#include "spz/not_built.h"

namespace spz {

CollectionRoot find_collection_root(const std::string&, const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-040"); }
bool name_comes_before(const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-042"); }
std::string shown_name(const std::string&) { SPZ_NOT_BUILT("SPEC-041"); }

Collection::Collection(Files*, std::string, const Listing*) {}
const std::string& Collection::root() const { SPZ_NOT_BUILT("SPEC-040"); }
bool Collection::has_listing() const { SPZ_NOT_BUILT("SPEC-047"); }
FolderListing Collection::list(const std::string&) const { SPZ_NOT_BUILT("SPEC-041"); }
bool Collection::has_entries() const { SPZ_NOT_BUILT("SPEC-048"); }
std::string Collection::file_path(const std::string&, const std::string&) const { SPZ_NOT_BUILT("SPEC-041"); }
EntryDetail Collection::detail(const std::string&, const FolderEntry&, Millis) const { SPZ_NOT_BUILT("SPEC-046"); }

}  // namespace spz
