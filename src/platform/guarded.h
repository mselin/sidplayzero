// SPDX-License-Identifier: GPL-3.0-or-later
// The barrier at every edge where an exception must stop: the top of each of the app's threads (the UI
// queue, the workers, the render loop, the clock's timers) and every callback that C code calls (libcurl's).
// What `fn` throws is caught, whatever it is, and logged; it never goes on into a thread's start or a C
// frame, where it would end the program (SPEC-022: a job that fails does not take its thread with it).
#pragma once

#include <cstdio>
#include <exception>
#include <utility>

namespace spz::platform {

// Runs `fn`. True if it returned, false if it threw.
template <typename Fn>
bool run_guarded(const char* where, Fn&& fn) noexcept {
    try {
        std::forward<Fn>(fn)();
        return true;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sidplayzero: %s failed: %s\n", where, error.what());
    } catch (...) {
        std::fprintf(stderr, "sidplayzero: %s failed\n", where);
    }
    return false;
}

// For a callback that answers: what `fn` returns, or `if_thrown` if it threw.
template <typename T, typename Fn>
T run_guarded_or(const char* where, T if_thrown, Fn&& fn) noexcept {
    try {
        return std::forward<Fn>(fn)();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sidplayzero: %s failed: %s\n", where, error.what());
    } catch (...) {
        std::fprintf(stderr, "sidplayzero: %s failed\n", where);
    }
    return if_thrown;
}

}  // namespace spz::platform
