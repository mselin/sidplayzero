// SPDX-License-Identifier: GPL-3.0-or-later
// Searching (SPEC-162, SPEC-164): one pass over the index (spz/index.h), on the quick worker. Nothing
// here uses the network: HVSC's site is not searched (SPEC-165).
#pragma once

#include <string>
#include <vector>

#include "spz/engine.h"
#include "spz/index.h"

namespace spz {

// SPEC-164 (part `filters`): what limits the results. As built, nothing is set.
struct SearchFilter {
    SidModel model = SidModel::FromTune;   // FromTune: any chip model
    C64Clock clock = C64Clock::FromTune;   // FromTune: any clock
    int first_year = 0;                    // 0: any; else a year from 1982 to the present one
    int last_year = 0;                     // 0: any

    // Whether any of the four is set. While one is, absent tunes are left out, and a tune with no year
    // fits a year line only when that line is "any".
    bool is_set() const;
};

struct SearchHit {
    std::string path;       // the tune's path, as the index has it
    bool present = false;
};

struct SearchResults {
    bool searched = false;         // false: the query had no word, and there was no search
    std::vector<SearchHit> hits;   // in order of path; at most kMaxSearchHits
    int total = 0;                 // the true number of matches, which is what the screen shows
    bool cut_short = false;        // the time ran out: these are the matches found until then
};

const int kMaxSearchHits = 500;            // SPEC-162
const Millis kSearchLimitMs = 2000;        // SPEC-162

// SPEC-162: the words of a query: the query treated like search text (SPEC-160) and split at spaces.
std::vector<std::string> query_words(const std::string& query);

// SPEC-162: a tune matches when every word of the query is the beginning of some word of its search
// text. One pass over the index, in its order, limited to 2 s on the index's clock, after which it
// returns what it has. A query with no word gives no search.
SearchResults search(const Index& index, const std::string& query, const SearchFilter& filter = SearchFilter());

}  // namespace spz
