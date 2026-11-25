// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/fetcher.h"

#include "spz/not_built.h"

namespace spz {

struct Fetcher::Impl {};

bool fetch_ahead_due(Millis, Millis) { SPZ_NOT_BUILT("SPEC-141"); }

Fetcher::Fetcher(const FetcherParts&) {}
Fetcher::~Fetcher() = default;
void Fetcher::set_listener(FetchListener*) { SPZ_NOT_BUILT("SPEC-141"); }
FetchAnswer Fetcher::request(const std::string&, FetchWhy) { SPZ_NOT_BUILT("SPEC-141"); }
void Fetcher::cancel_play() { SPZ_NOT_BUILT("SPEC-141"); }
void Fetcher::cancel_playlist() { SPZ_NOT_BUILT("SPEC-174"); }
bool Fetcher::fetching(const std::string&) const { SPZ_NOT_BUILT("SPEC-141"); }
std::string Fetcher::ahead_failure(const std::string&) const { SPZ_NOT_BUILT("SPEC-141"); }

}  // namespace spz
