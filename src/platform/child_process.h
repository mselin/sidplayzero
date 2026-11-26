// SPDX-License-Identifier: GPL-3.0-or-later
// Running another program (SPEC-007, SPEC-076): always from an argument list, never through a shell, so
// that no character of an argument means anything but itself.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace spz::platform {

// The full path of a program found on PATH, or "" if there is none of that name.
std::string find_program(const std::string& name);

struct ChildOptions {
    // Called with each piece of the program's standard output as it arrives. Return false to stop the
    // program (its whole process group is killed).
    std::function<bool(const std::string& piece)> output;
    // Kill the program, and say so, if it has not ended within this many milliseconds. 0: no limit.
    long timeout_ms = 0;
    // Run it in a process group of its own, so that stopping it stops whatever it started. Whatever is
    // still running in the group when the program ends is killed too, before the program is reaped.
    bool own_group = false;
    // Without an output function: what is kept of its output at most. A program that says more is stopped.
    std::size_t output_limit = 1024 * 1024;
    // Variables to set for it, as NAME=VALUE, beside the environment of this process.
    std::vector<std::string> environment;
};

struct ChildResult {
    bool started = false;     // false: the program could not be run at all
    bool exited = false;      // it ended by itself, with `status`
    int status = -1;
    bool stopped = false;     // the output function asked to stop it, or the time ran out
    std::string output;       // everything it wrote to its standard output, when no output function is given
};

// Runs `program` (a full path) with `arguments` (the first is its name, as argv[0]); standard input is
// empty and standard error is thrown away. Waits for it to end.
ChildResult run_child(const std::string& program, const std::vector<std::string>& arguments,
                      const ChildOptions& options = ChildOptions());

// Kills every process group started with `own_group` that is still running: for the app's exit (SPEC-201).
void stop_all_children();

}  // namespace spz::platform
