// SPDX-License-Identifier: GPL-3.0-or-later
// The queue by itself (SPEC-062), and the random order of SPEC-065: the generator, the loop and the
// orders the specification gives for seed 1.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"
#include "play_support.h"
#include "spz/queue.h"
#include "spz/shuffle.h"
#include "spz/types.h"

namespace {

// Entries numbered from 1, as SPEC-065's oracle numbers them: entry n is "/Q/n.sid".
spz::Entry numbered(int n) {
    spz::Entry e;
    e.path = "/Q/" + std::to_string(n) + ".sid";
    return e;
}

std::vector<spz::Entry> one_to(int count) {
    std::vector<spz::Entry> entries;
    for (int n = 1; n <= count; ++n) entries.push_back(numbered(n));
    return entries;
}

// The numbers of a list of such entries.
std::vector<int> numbers(const std::vector<spz::Entry>& entries) {
    std::vector<int> found;
    for (const spz::Entry& e : entries) found.push_back(std::stoi(e.path.substr(3)));
    return found;
}

int number(const spz::Entry& e) { return std::stoi(e.path.substr(3)); }

// Plays a queue through from where it stands: the current entry, then each one next() moves to, until
// next() says there is no more or `most` have been played.
std::vector<int> played(spz::Queue& queue, bool repeat_all, std::size_t most) {
    std::vector<int> order = {number(queue.current())};
    while (order.size() < most && queue.next(repeat_all)) order.push_back(number(queue.current()));
    return order;
}

}  // namespace

TEST_CASE("SPEC-062 the queue is a copy of the entries it was set from, with the place at the chosen one") {
    std::vector<spz::Entry> source = one_to(3);
    source[1].subtune = 2;
    spz::Queue queue;
    queue.set(source, 1, false);
    source.clear();                                         // whatever happens to the source afterwards
    source.push_back(numbered(9));
    CHECK_FALSE(queue.empty());
    CHECK(queue.size() == 3);
    CHECK(numbers(queue.order()) == std::vector<int>{1, 2, 3});
    CHECK(queue.order()[1].subtune == 2);                   // entries keep their subtune numbers
    CHECK(queue.place() == 1);
    CHECK(number(queue.current()) == 2);
    CHECK(numbers(queue.original()) == std::vector<int>{1, 2, 3});
    CHECK_FALSE(queue.shuffled());
}

TEST_CASE("SPEC-062 next moves one place and stops at the last entry; previous moves one place back and stays at the first") {
    spz::Queue queue;
    queue.set(one_to(3), 1, false);
    CHECK(queue.next(false));
    CHECK(number(queue.current()) == 3);
    CHECK_FALSE(queue.next(false));                         // at the last entry: nowhere to go
    CHECK(number(queue.current()) == 3);
    CHECK(queue.place() == 2);
    queue.previous();
    CHECK(number(queue.current()) == 2);
    queue.previous();
    CHECK(number(queue.current()) == 1);
    queue.previous();                                       // at the first entry: it is played again
    CHECK(number(queue.current()) == 1);
    CHECK(queue.place() == 0);
}

TEST_CASE("SPEC-062 SPEC-065 with repeat-all, next from the last entry goes to the first") {
    spz::Queue queue;
    queue.set(one_to(3), 2, false);
    CHECK(queue.next(true));
    CHECK(number(queue.current()) == 1);
    CHECK(queue.place() == 0);
    CHECK(played(queue, true, 7) == std::vector<int>{1, 2, 3, 1, 2, 3, 1});
}

TEST_CASE("SPEC-062 the entry that next would move to can be asked for without moving") {
    spz::Queue queue;
    queue.set(one_to(3), 1, false);
    spz::Entry next;
    CHECK(queue.peek_next(false, &next));
    CHECK(number(next) == 3);
    CHECK(number(queue.current()) == 2);                    // it has not moved
    CHECK(queue.next(false));
    CHECK_FALSE(queue.peek_next(false, &next));             // at the last entry there is none
    CHECK(queue.peek_next(true, &next));                    // unless repeat-all is on
    CHECK(number(next) == 1);
    CHECK(number(queue.current()) == 3);
}

TEST_CASE("SPEC-062 an empty queue is empty, and a cleared one too") {
    spz::Queue queue;
    CHECK(queue.empty());
    CHECK(queue.size() == 0);
    queue.set(one_to(2), 0, false);
    CHECK_FALSE(queue.empty());
    queue.clear();
    CHECK(queue.empty());
}

#if SPZ_PART_SHUFFLE
TEST_CASE("SPEC-065 the generator is xorshift64*: with seed 1 its first number is 0x47E4CE4B896CDD1D [parts: shuffle]") {
    spz::ShuffleGenerator generator(1);
    CHECK(generator.next() == 0x47E4CE4B896CDD1DULL);
    // The numbers after it, worked out from the item's three shifts and its multiplier.
    CHECK(generator.next() == 0xABCFA6A8E079651DULL);
    CHECK(generator.next() == 0xB9D10D8FEB731F57ULL);
    CHECK(generator.next() == 0x4DB418A0BB1B019DULL);
}

TEST_CASE("SPEC-065 a seed of 0 starts the generator from 0x9E3779B97F4A7C15 [parts: shuffle]") {
    spz::ShuffleGenerator from_zero(0);
    spz::ShuffleGenerator from_constant(0x9E3779B97F4A7C15ULL);
    const std::uint64_t first = from_zero.next();
    CHECK(first == from_constant.next());
    CHECK(first == 0x0D83B3E29A21487AULL);                  // that state, shifted and multiplied as the item says
    CHECK(from_zero.next() == from_constant.next());
}

TEST_CASE("SPEC-065 a list is permuted from its last place down, each time swapping with the place the next number picks [parts: shuffle]") {
    spz::ShuffleGenerator generator(1);
    std::vector<std::size_t> places = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    spz::shuffle_places(&places, &generator);
    // With seed 1: i = 9 takes 0x47E4CE4B896CDD1D modulo 10, and so on down to i = 1.
    CHECK(places == std::vector<std::size_t>{10, 1, 7, 5, 2, 3, 4, 8, 9, 6});

    std::vector<std::size_t> one = {7};
    spz::ShuffleGenerator untouched(1);
    spz::shuffle_places(&one, &untouched);                  // nothing to swap: no number is taken
    CHECK(one == std::vector<std::size_t>{7});
    CHECK(untouched.next() == 0x47E4CE4B896CDD1DULL);
}

TEST_CASE("SPEC-065 with seed 1, ten entries started at entry 3 are played in the order 3, 4, 8, 1, 10, 9, 5, 6, 7, 2 [parts: shuffle]") {
    spz::ShuffleGenerator generator(1);
    spz::Queue queue(&generator);
    queue.set(one_to(10), 2, true);
    CHECK(queue.shuffled());
    CHECK(queue.place() == 0);                              // the chosen entry is put first
    CHECK(numbers(queue.order()) == std::vector<int>{3, 4, 8, 1, 10, 9, 5, 6, 7, 2});
    CHECK(numbers(queue.original()) == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    CHECK(played(queue, false, 99) == std::vector<int>{3, 4, 8, 1, 10, 9, 5, 6, 7, 2});
    CHECK_FALSE(queue.next(false));                         // and there it ends
}

TEST_CASE("SPEC-065 with repeat-all the second round is 9, 8, 1, 2, 4, 10, 3, 7, 6, 5: all the entries permuted from their original order, the generator carrying on [parts: shuffle]") {
    spz::ShuffleGenerator generator(1);
    spz::Queue queue(&generator);
    queue.set(one_to(10), 2, true);
    CHECK(played(queue, true, 20) == std::vector<int>{3, 4, 8, 1, 10, 9, 5, 6, 7, 2, 9, 8, 1, 2, 4, 10, 3, 7, 6, 5});
    CHECK(numbers(queue.order()) == std::vector<int>{9, 8, 1, 2, 4, 10, 3, 7, 6, 5});
    CHECK(queue.place() == 9);
}

TEST_CASE("SPEC-065 the next round of a shuffled queue is the same whether or not it was asked about first [parts: shuffle]") {
    spz::ShuffleGenerator generator(1);
    spz::Queue queue(&generator);
    queue.set(one_to(10), 2, true);
    for (int i = 0; i < 9; ++i) REQUIRE(queue.next(true));
    REQUIRE(number(queue.current()) == 2);                  // the last of the first round
    spz::Entry next;
    CHECK(queue.peek_next(true, &next));
    CHECK(number(next) == 9);
    CHECK(queue.peek_next(true, &next));                    // asking twice makes one round, not two
    CHECK(number(next) == 9);
    CHECK(played(queue, true, 11) == std::vector<int>{2, 9, 8, 1, 2, 4, 10, 3, 7, 6, 5});
}

TEST_CASE("SPEC-065 switching shuffle on while a queue plays permutes the entries after the current one [parts: shuffle]") {
    spz::ShuffleGenerator generator(1);
    spz::Queue queue(&generator);
    queue.set(one_to(10), 2, false);
    queue.set_shuffle(true);
    CHECK(queue.shuffled());
    CHECK(queue.place() == 2);
    CHECK(number(queue.current()) == 3);
    const std::vector<int> order = numbers(queue.order());
    CHECK(std::vector<int>(order.begin(), order.begin() + 3) == std::vector<int>{1, 2, 3});   // up to the current one: as they were
    // The seven after it, permuted by the item's loop with the first six numbers of seed 1.
    CHECK(std::vector<int>(order.begin() + 3, order.end()) == std::vector<int>{4, 8, 6, 5, 7, 10, 9});
}

TEST_CASE("SPEC-065 switching shuffle off puts the queue back in its original order, with the place at the current entry [parts: shuffle]") {
    spz::ShuffleGenerator generator(1);
    spz::Queue queue(&generator);
    queue.set(one_to(10), 2, true);                         // 3, 4, 8, 1, 10, 9, 5, 6, 7, 2
    REQUIRE(queue.next(false));
    REQUIRE(queue.next(false));
    REQUIRE(number(queue.current()) == 8);
    queue.set_shuffle(false);
    CHECK_FALSE(queue.shuffled());
    CHECK(numbers(queue.order()) == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    CHECK(number(queue.current()) == 8);
    CHECK(queue.place() == 7);
    CHECK(queue.next(false));
    CHECK(number(queue.current()) == 9);
}

TEST_CASE("SPEC-065 a shuffled queue of one entry plays that entry, and of two the chosen one and then the other [parts: shuffle]") {
    spz::ShuffleGenerator generator(1);
    spz::Queue queue(&generator);
    queue.set(one_to(1), 0, true);
    CHECK(played(queue, false, 9) == std::vector<int>{1});
    queue.set(one_to(2), 1, true);
    CHECK(played(queue, false, 9) == std::vector<int>{2, 1});
}
#endif
