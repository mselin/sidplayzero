// SPDX-License-Identifier: GPL-3.0-or-later
// Searching (SPEC-162, SPEC-164): one pass over the index.
#include "spz/search.h"

#include <algorithm>
#include <mutex>

namespace spz {

namespace {

// Whether `word` is the beginning of some word of `text`, whose words are separated by single spaces.
bool begins_a_word(const std::string& text, const std::string& word) {
    std::size_t at = text.find(word);
    while (at != std::string::npos) {
        if (at == 0 || text[at - 1] == ' ') return true;
        at = text.find(word, at + 1);
    }
    return false;
}

// SPEC-164: whether a record fits the filter. A tune whose header says "either" or "unknown" for its chip
// model or its clock fits any choice of that line.
bool fits(const IndexRecord& record, const SearchFilter& filter) {
    if (!filter.is_set()) return true;
    if (!record.present) return false;
    if (filter.model != SidModel::FromTune && record.model != SidModel::FromTune && record.model != filter.model) return false;
    if (filter.clock != C64Clock::FromTune && record.clock != C64Clock::FromTune && record.clock != filter.clock) return false;
    if (filter.first_year != 0 && (record.year == 0 || record.year < filter.first_year)) return false;
    if (filter.last_year != 0 && (record.year == 0 || record.year > filter.last_year)) return false;
    return true;
}

}  // namespace

bool SearchFilter::is_set() const {
    return model != SidModel::FromTune || clock != C64Clock::FromTune || first_year != 0 || last_year != 0;
}

std::vector<std::string> query_words(const std::string& query) {
    const std::string text = search_text(query, "", "", "");
    std::vector<std::string> words;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        if (end > start) words.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return words;
}

SearchResults search(const Index& index, const std::string& query, const SearchFilter& filter) {
    SearchResults results;
    std::vector<std::string> words = query_words(query);
    if (words.empty()) return results;
    results.searched = true;
    // The longest word first: it is the one least often found, so a record is passed over soonest.
    std::sort(words.begin(), words.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });

    std::lock_guard<std::mutex> lock(index.mutex_);
    const Millis start = index.clock_->now_ms();
    const std::size_t count = index.records_.size();
    for (std::size_t i = 0; i < count; ++i) {
        if ((i & 255u) == 0 && i > 0 && index.clock_->now_ms() - start > kSearchLimitMs) {
            results.cut_short = true;
            break;
        }
        const IndexRecord& record = index.records_[i];
        bool all = true;
        for (const std::string& word : words) {
            if (!begins_a_word(record.search_text, word)) {
                all = false;
                break;
            }
        }
        if (!all || !fits(record, filter)) continue;
        ++results.total;
        if (static_cast<int>(results.hits.size()) < kMaxSearchHits) results.hits.push_back(SearchHit{record.path, record.present});
    }
    return results;
}

}  // namespace spz
