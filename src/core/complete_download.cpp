// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/complete_download.h"

#include "spz/not_built.h"

namespace spz {

struct CompleteDownload::Impl {};

DownloadOffer download_offer(const HvscStatus&, Files*, Unpacker*, Memory*, const std::string&) { SPZ_NOT_BUILT("SPEC-145"); }
bool read_7z_listing(const std::string&, std::vector<ArchiveEntry>*) { SPZ_NOT_BUILT("SPEC-149"); }
ArchiveFault check_archive(const std::vector<ArchiveEntry>&) { SPZ_NOT_BUILT("SPEC-149"); }
std::uint64_t room_for_unpacking(std::uint64_t, const std::vector<ArchiveEntry>&, std::uint64_t) { SPZ_NOT_BUILT("SPEC-146"); }
int read_install_marker(Files*, const std::string&) { SPZ_NOT_BUILT("SPEC-144"); }
bool write_install_marker(Files*, const std::string&, int) { SPZ_NOT_BUILT("SPEC-144"); }

CompleteDownload::CompleteDownload(const CompleteDownloadParts&) {}
CompleteDownload::~CompleteDownload() = default;
bool CompleteDownload::start() { SPZ_NOT_BUILT("SPEC-144"); }
void CompleteDownload::recover() { SPZ_NOT_BUILT("SPEC-144"); }
bool CompleteDownload::unfinished() const { SPZ_NOT_BUILT("SPEC-144"); }
bool CompleteDownload::finish() { SPZ_NOT_BUILT("SPEC-144"); }

}  // namespace spz
