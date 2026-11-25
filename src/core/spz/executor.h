// SPDX-License-Identifier: GPL-3.0-or-later
// How `core` hands work to a thread (SPEC-022). The app has one executor for the UI thread and one for
// each worker: quick, fetch and long. In the unit tests an executor runs its jobs when the test says.
#pragma once

#include <functional>

namespace spz {

class Executor {
public:
    virtual ~Executor() = default;
    // Runs `job` later, on this executor's thread, after the jobs posted before it.
    virtual void post(std::function<void()> job) = 0;
};

}  // namespace spz
