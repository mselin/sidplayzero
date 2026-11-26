// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/playlist_fetch.h"

#include "spz/not_built.h"

namespace spz {

FetchMissing::FetchMissing(Clock* clock, Files* files, const std::string& root, const EntryResolver* resolver,
                           TuneFetcher* fetcher, NoticeGate* gate, FetchMissingListener* listener)
    : clock_(clock), files_(files), root_(root), resolver_(resolver), fetcher_(fetcher), gate_(gate),
      listener_(listener) {}

void FetchMissing::start(const std::vector<Entry>&) { SPZ_NOT_BUILT("SPEC-174"); }
void FetchMissing::stop() { SPZ_NOT_BUILT("SPEC-174"); }
FetchMissingState FetchMissing::state() const { SPZ_NOT_BUILT("SPEC-174"); }

}  // namespace spz
