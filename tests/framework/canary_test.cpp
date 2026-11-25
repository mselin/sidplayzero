// SPDX-License-Identifier: GPL-3.0-or-later
// The unit level's canaries (TF-014, TF-013, TF-022): tests of the test framework, not of the app.
//
// A name ending in [canary-fails] is a test built to fail. A normal run leaves it out; `tools/test prove`
// puts it in and expects exactly that failure. [canary-fails-asan] is run only in the sanitizer build.
#include <cstddef>
#include <string>
#include <vector>

#include "doctest.h"
#include "spz/version.h"

TEST_CASE("TF-014 canary: the unit level runs a test, and it passes") {
    CHECK(std::string(spz::app_name()) == "SidPlayZero");
    CHECK(1 + 1 == 2);
}

TEST_CASE("TF-014 canary: a test built to fail [canary-fails]") {
    CHECK(1 + 1 == 3);
}

#if SPZ_PART_PLAYLISTS
TEST_CASE("TF-013 canary: a test that needs a part [parts: playlists]") {
    CHECK(SPZ_PART_PLAYLISTS == 1);
}
#endif

// Reads one element past the end of an array. Only AddressSanitizer notices.
TEST_CASE("TF-022 canary: a read past the end of an array [canary-fails-asan]") {
    std::vector<int> numbers(4, 7);
    volatile std::size_t index = numbers.size();
    volatile int value = numbers.data()[index];
    CHECK(value == value);
}
