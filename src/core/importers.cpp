// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/importers.h"

#include "spz/not_built.h"

namespace spz {

ImportRead read_m3u(const Bytes&) { SPZ_NOT_BUILT("SPEC-175"); }
ImportRead read_js2(const Bytes&) { SPZ_NOT_BUILT("SPEC-176"); }
ImportRead read_jsidplay2_json(const Bytes&) { SPZ_NOT_BUILT("SPEC-176"); }
ImportRead read_spl(const Bytes&) { SPZ_NOT_BUILT("SPEC-177"); }
ImportRead read_import(const std::string&, const Bytes&) { SPZ_NOT_BUILT("SPEC-175"); }

Importer::Importer(Files* files, const std::string& root, const std::string& import_folder, const Listing* listing,
                   Playlists* playlists)
    : files_(files), root_(root), import_folder_(import_folder), listing_(listing), playlists_(playlists) {}

std::vector<std::string> Importer::files() const { SPZ_NOT_BUILT("SPEC-175"); }
bool Importer::match(const std::string&, const std::string&, std::string*) const { SPZ_NOT_BUILT("SPEC-178"); }
ImportReport Importer::import_file(const std::string&) { SPZ_NOT_BUILT("SPEC-175"); }

}  // namespace spz
