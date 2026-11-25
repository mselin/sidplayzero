// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/limits.h"

#include "spz/not_built.h"

namespace spz {

std::size_t size_limit(FileKind) { SPZ_NOT_BUILT("SPEC-204"); }
std::string refuse_size(FileKind, std::uint64_t) { SPZ_NOT_BUILT("SPEC-204"); }
FileStatus read_limited(Files*, const std::string&, FileKind, Bytes*, std::string*) { SPZ_NOT_BUILT("SPEC-204"); }
std::vector<std::string> split_lines(const Bytes&) { SPZ_NOT_BUILT("SPEC-204"); }

}  // namespace spz
