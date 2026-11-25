// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/listing.h"

#include "spz/not_built.h"

namespace spz {

ListingBuild Listing::build(Files*, const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-142"); }
bool Listing::open(Files*, const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-142"); }
bool Listing::is_open() const { SPZ_NOT_BUILT("SPEC-142"); }
int Listing::tune_count() const { SPZ_NOT_BUILT("SPEC-142"); }
int Listing::folder_count() const { SPZ_NOT_BUILT("SPEC-142"); }
bool Listing::by_path(const std::string&, ListedTune*) const { SPZ_NOT_BUILT("SPEC-142"); }
bool Listing::by_md5(const std::string&, ListedTune*) const { SPZ_NOT_BUILT("SPEC-142"); }
bool Listing::folder(const std::string&, std::vector<std::string>*, std::vector<std::string>*) const { SPZ_NOT_BUILT("SPEC-142"); }
std::vector<ListedTune> Listing::tunes(int, int) const { SPZ_NOT_BUILT("SPEC-142"); }

}  // namespace spz
