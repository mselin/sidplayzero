// SPDX-License-Identifier: GPL-3.0-or-later
// The queue (SPEC-062): the ordered list of entries that "next" and "previous" move through. It is a
// copy, made at the moment a tune is played, and nothing done to its source afterwards changes it. With
// `shuffle` it is played in the random order of SPEC-065.
#pragma once

#include <cstddef>
#include <vector>

#include "spz/shuffle.h"
#include "spz/types.h"

namespace spz {

class Queue {
public:
    // `generator` makes the random orders; the player has one for the whole run, so that it carries on
    // from where it was (SPEC-065). It may be null if shuffle is never asked for.
    explicit Queue(ShuffleGenerator* generator = nullptr);

    // Sets the queue: a copy of `entries`, with the place at `chosen` (counted from 0). With `shuffle`,
    // the chosen entry is taken out, the others are permuted in their queue order, and the chosen entry
    // is put first.
    void set(const std::vector<Entry>& entries, std::size_t chosen, bool shuffle);
    // Empties the queue.
    void clear();

    bool empty() const;
    std::size_t size() const;
    // The entries in the order they are played, and the place of the current one in it, from 0.
    const std::vector<Entry>& order() const;
    std::size_t place() const;
    // The entries in the order they were given to set().
    const std::vector<Entry>& original() const;
    // The entry at the place. The queue must not be empty.
    const Entry& current() const;

    // "Next tune" (SPEC-062): moves one place on and returns true. At the last entry: without
    // `repeat_all` it stays where it is and returns false (the player then stops, Idle); with it, it goes
    // to the first entry and returns true, and a shuffled queue is first permuted anew, all its entries,
    // from their original order (SPEC-065).
    bool next(bool repeat_all);
    // "Previous tune": moves one place back. At the first entry it stays there, and the player plays that
    // entry again.
    void previous();
    // What next() would move to, without moving: for the fetch-ahead (SPEC-141). False where next()
    // would return false. At the last entry of a shuffled queue with `repeat_all`, the next round's
    // order is made now and kept, so that next() then gives the same entry.
    bool peek_next(bool repeat_all, Entry* out);

    // Switches shuffle on or off while the queue plays (SPEC-065). On: the entries after the current one
    // are permuted. Off: the queue goes back to its original order, with the place at the current entry.
    void set_shuffle(bool on);
    bool shuffled() const;

private:
    // Puts order_ in the order that places_ gives, as places in original_.
    void arrange();
    // A whole new round of a shuffled queue: every entry permuted from its original order.
    std::vector<std::size_t> new_round();

    ShuffleGenerator* generator_ = nullptr;
    std::vector<Entry> original_;
    std::vector<Entry> order_;
    std::vector<std::size_t> places_;       // for each entry of order_, its place in original_
    std::size_t place_ = 0;
    bool shuffled_ = false;
    bool round_made_ = false;               // peek_next() has made the next round already: it is next_round_
    std::vector<std::size_t> next_round_;
};

}  // namespace spz
