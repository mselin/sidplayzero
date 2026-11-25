// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/safe_file.h"

#include "spz/not_built.h"

namespace spz {

std::string temp_path_for(const std::string&) { SPZ_NOT_BUILT("SPEC-132"); }
bool is_temp_name(const std::string&) { SPZ_NOT_BUILT("SPEC-132"); }
FileStatus safe_write(Files*, const std::string&, const Bytes&) { SPZ_NOT_BUILT("SPEC-132"); }
FileStatus safe_commit(Files*, const std::string&) { SPZ_NOT_BUILT("SPEC-132"); }
int remove_temp_files(Files*, const std::string&) { SPZ_NOT_BUILT("SPEC-132"); }
int remove_old_partials(Files*, const Clock*, const std::string&) { SPZ_NOT_BUILT("SPEC-132"); }

}  // namespace spz
