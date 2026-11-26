// SPDX-License-Identifier: GPL-3.0-or-later
// What a part of `core` that is not written yet does when it is called (Phase 4): it throws NotBuilt.
// A test that reaches it fails with "not built yet: ...", which is the reason tests/waiting.tsv gives for
// it. Phase 5 replaces each of these with the real thing, and takes the test off the list.
#pragma once

#include <stdexcept>
#include <string>

namespace spz {

class NotBuilt : public std::logic_error {
public:
    explicit NotBuilt(const std::string& what) : std::logic_error("not built yet: " + what) {}
};

}  // namespace spz

// In a stub's body: SPZ_NOT_BUILT("SPEC-203"); names the function and the item it belongs to.
#define SPZ_NOT_BUILT(item) throw ::spz::NotBuilt(std::string(__func__) + " (" item ")")
