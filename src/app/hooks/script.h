// SPDX-License-Identifier: GPL-3.0-or-later
// The hooks the simulator's tests drive the program by (SPEC-253). Built only with
// SIDPLAYZERO_TEST_HOOKS, and never into a package.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace spz::hooks {

struct Options {
    std::string script;                 // --script FILE
    std::string out;                    // --out DIR
    std::string fakes;                  // --fakes DIR
    bool headless = false;              // --headless
    std::string backend = "software";   // --backend software|fake
    std::string engine = "real";        // --engine real|fake
    std::string clock = "fake";         // --clock fake|real
    std::uint64_t seed = 0;             // --seed N
    bool has_seed = false;
    std::string metrics;                // --metrics FILE
};

// Runs a script and returns the program's exit status: 0 if every line did what it should, 1 if an
// `expect` did not hold, 2 if the script could not be read or run.
int run_script(const Options& options);

// Part of "settled" (SPEC-253): the script goes on only while every check registered here says true.
// A worker registers one that says whether it is idle or waiting on the fake clock.
void add_idle_check(std::function<bool()> idle);

}  // namespace spz::hooks
