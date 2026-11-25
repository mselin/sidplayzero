// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/download_resume.h"

#include "spz/not_built.h"

namespace spz {

HttpResult download_resumable(Files*, HttpClient*, const std::string&, const std::string&, const std::string&,
                              std::uint64_t, const HttpClient::Progress&) {
    SPZ_NOT_BUILT("SPEC-147");
}
void discard_partial(Files*, const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-147"); }

}  // namespace spz
