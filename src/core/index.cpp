// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/index.h"

#include "spz/not_built.h"

namespace spz {

std::string search_text(const std::string&, const std::string&, const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-160"); }
bool read_index(const Bytes&, IndexHeader*, std::vector<IndexRecord>*) { SPZ_NOT_BUILT("SPEC-160"); }

Index::Index(Files*, Clock*, const Listing*, std::string, std::string) {}
bool Index::load() { SPZ_NOT_BUILT("SPEC-160"); }
IndexRun Index::build(IndexReason, const std::function<bool()>&) { SPZ_NOT_BUILT("SPEC-161"); }
void Index::set_playing(bool) { SPZ_NOT_BUILT("SPEC-161"); }
int Index::percent() const { SPZ_NOT_BUILT("SPEC-161"); }
bool Index::complete() const { SPZ_NOT_BUILT("SPEC-161"); }
void Index::refresh_folder(const std::string&) { SPZ_NOT_BUILT("SPEC-161"); }
void Index::tune_arrived(const std::string&) { SPZ_NOT_BUILT("SPEC-161"); }
IndexHeader Index::header() const { SPZ_NOT_BUILT("SPEC-160"); }
int Index::count() const { SPZ_NOT_BUILT("SPEC-160"); }
std::vector<IndexRecord> Index::records(int, int) const { SPZ_NOT_BUILT("SPEC-160"); }
bool Index::find(const std::string&, IndexRecord*) const { SPZ_NOT_BUILT("SPEC-160"); }

}  // namespace spz
