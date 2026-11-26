// SPDX-License-Identifier: GPL-3.0-or-later
// The queue (SPEC-062), and its random order (SPEC-065).
#include "spz/queue.h"

namespace spz {

Queue::Queue(ShuffleGenerator* generator) : generator_(generator) {}

void Queue::arrange() {
    order_.clear();
    order_.reserve(places_.size());
    for (const std::size_t at : places_) order_.push_back(original_[at]);
}

std::vector<std::size_t> Queue::new_round() {
    std::vector<std::size_t> places;
    for (std::size_t i = 0; i < original_.size(); ++i) places.push_back(i);
    shuffle_places(&places, generator_);
    return places;
}

void Queue::set(const std::vector<Entry>& entries, std::size_t chosen, bool shuffle) {
    original_ = entries;
    places_.clear();
    round_made_ = false;
    next_round_.clear();
    if (original_.empty()) {
        order_.clear();
        place_ = 0;
        shuffled_ = false;
        return;
    }
    if (chosen >= original_.size()) chosen = 0;
    shuffled_ = shuffle && generator_ != nullptr;
    if (shuffled_) {
        // The chosen entry is taken out, the others are permuted in their queue order, and the chosen
        // entry is put first.
        std::vector<std::size_t> others;
        for (std::size_t i = 0; i < original_.size(); ++i) {
            if (i != chosen) others.push_back(i);
        }
        shuffle_places(&others, generator_);
        places_.push_back(chosen);
        places_.insert(places_.end(), others.begin(), others.end());
        place_ = 0;
    } else {
        for (std::size_t i = 0; i < original_.size(); ++i) places_.push_back(i);
        place_ = chosen;
    }
    arrange();
}

void Queue::clear() {
    original_.clear();
    order_.clear();
    places_.clear();
    place_ = 0;
    shuffled_ = false;
    round_made_ = false;
    next_round_.clear();
}

bool Queue::empty() const { return order_.empty(); }
std::size_t Queue::size() const { return order_.size(); }
const std::vector<Entry>& Queue::order() const { return order_; }
std::size_t Queue::place() const { return place_; }
const std::vector<Entry>& Queue::original() const { return original_; }
const Entry& Queue::current() const { return order_[place_]; }

bool Queue::next(bool repeat_all) {
    if (order_.empty()) return false;
    if (place_ + 1 < order_.size()) {
        ++place_;
        return true;
    }
    if (!repeat_all) return false;
    if (shuffled_) {
        places_ = round_made_ ? next_round_ : new_round();
        round_made_ = false;
        next_round_.clear();
        arrange();
    }
    place_ = 0;
    return true;
}

void Queue::previous() {
    if (place_ > 0) --place_;
}

bool Queue::peek_next(bool repeat_all, Entry* out) {
    if (order_.empty()) return false;
    if (place_ + 1 < order_.size()) {
        if (out) *out = order_[place_ + 1];
        return true;
    }
    if (!repeat_all) return false;
    if (shuffled_) {
        if (!round_made_) {
            next_round_ = new_round();
            round_made_ = true;
        }
        if (out) *out = original_[next_round_.front()];
        return true;
    }
    if (out) *out = order_.front();
    return true;
}

void Queue::set_shuffle(bool on) {
    if (order_.empty()) {
        shuffled_ = on && generator_ != nullptr;
        return;
    }
    if (on == shuffled_ || (on && generator_ == nullptr)) return;
    round_made_ = false;
    next_round_.clear();
    const std::size_t current = places_[place_];
    if (on) {
        // The entries after the current one are permuted; those up to it stay where they are.
        std::vector<std::size_t> after(places_.begin() + static_cast<std::ptrdiff_t>(place_) + 1, places_.end());
        shuffle_places(&after, generator_);
        places_.resize(place_ + 1);
        places_.insert(places_.end(), after.begin(), after.end());
        shuffled_ = true;
    } else {
        places_.clear();
        for (std::size_t i = 0; i < original_.size(); ++i) places_.push_back(i);
        place_ = current;
        shuffled_ = false;
    }
    arrange();
}

bool Queue::shuffled() const { return shuffled_; }

}  // namespace spz
