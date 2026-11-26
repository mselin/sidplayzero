// SPDX-License-Identifier: GPL-3.0-or-later
// The guard of the audio callback (SPEC-022). The audio callback must not lock, allocate or wait. In a
// test build (SIDPLAYZERO_TEST_HOOKS, without a sanitizer, which brings allocators of its own), memory
// allocation and pthread_mutex_lock are watched: when either happens on a thread while it is inside the
// audio callback, the process says so and aborts. In any other build the scope costs nothing and
// watches nothing.
#pragma once

namespace spz::platform {

// Marks the audio callback: from its construction to its destruction, the calling thread is inside it.
class AudioCallbackScope {
public:
    AudioCallbackScope();
    ~AudioCallbackScope();
    AudioCallbackScope(const AudioCallbackScope&) = delete;
    AudioCallbackScope& operator=(const AudioCallbackScope&) = delete;
};

// Whether this build has the guard.
bool audio_guard_built();

}  // namespace spz::platform
