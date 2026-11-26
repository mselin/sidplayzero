// SPDX-License-Identifier: GPL-3.0-or-later
// What the unit tests of the index and of searching share.
#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "browse_support.h"
#include "doctest.h"
#include "spz/index.h"
#include "spz/search.h"

namespace spz::test {

// The fixture collection of release 85, all of it on the device, with three more tunes that HVSC does
// not list: fourteen tunes for the tests of searching.
//
//   /MUSICIANS/H/Hubbard_Rob/Year_Test.sid   "Year Test", released "1985 Test Label"
//   /My Rips/Cool Söng.sid                   "The User's Own" by "Somebody"
//   /My Rips/No Year.sid                     "No Year", released "Unknown label": no year in it
struct SearchRoot : TestRoot {
    SearchRoot() : TestRoot("collections/85/C64Music") {
        put("tunes/browse_year_1985.sid", "/MUSICIANS/H/Hubbard_Rob/Year_Test.sid");
        put("tunes/own_tune.sid", "/My Rips/Cool S\xC3\xB6ng.sid");
        put("tunes/browse_no_year.sid", "/My Rips/No Year.sid");
    }
};

const char* const kRob = "/MUSICIANS/H/Hubbard_Rob/";
const char* const kTom = "/MUSICIANS/T/Tester_Tom/";
const char* const kHuelsbeck = "/MUSICIANS/H/Huelsbeck_Chris/Huelsbeck_Test.sid";
const char* const kOwnTune = "/My Rips/Cool S\xC3\xB6ng.sid";

// The paths of a search's hits, in the order given, as one line: "/a.sid | /b.sid".
inline std::string paths_of(const spz::SearchResults& results) {
    std::vector<std::string> paths;
    for (const spz::SearchHit& hit : results.hits) paths.push_back(hit.path);
    return joined(paths);
}

// The words of a search text: what is between its spaces.
inline std::vector<std::string> words_of(const std::string& text) {
    std::istringstream in(text);
    std::vector<std::string> words;
    std::string word;
    while (in >> word) words.push_back(word);
    return words;
}

// The words of a search text in alphabetical order, as one line: the order of the four parts of a search
// text is not what these tests are about.
inline std::string sorted_words(const std::string& text) {
    std::vector<std::string> words = words_of(text);
    std::sort(words.begin(), words.end());
    return joined(words);
}

inline bool has_word(const std::string& text, const std::string& word) {
    const std::vector<std::string> words = words_of(text);
    return std::find(words.begin(), words.end(), word) != words.end();
}

}  // namespace spz::test
