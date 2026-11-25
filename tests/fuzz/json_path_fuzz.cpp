// SPDX-License-Identifier: GPL-3.0-or-later
// test: TF-060 SPEC-205 fuzz: looking a value up in a screen description by any path at all
//
// The only reader `core` has before Phase 5. The input is taken as a path, such as "rows[2].text", and
// looked up in a description; whatever the bytes, the lookup must end, and must not read outside it.
#include <cstddef>
#include <cstdint>
#include <string>

#include "spz/json.h"
#include "spz/keys.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static const spz::Json description = [] {
        spz::Json root = spz::Json::object();
        root.set("screen", "browser");
        spz::Json rows = spz::Json::array();
        for (int i = 0; i < 3; ++i) {
            spz::Json row = spz::Json::object();
            row.set("text", "row");
            row.set("marks", spz::Json::array());
            rows.push(row);
        }
        root.set("rows", rows);
        root.set("position", nullptr);
        return root;
    }();
    const std::string path(reinterpret_cast<const char*>(data), size);
    if (const spz::Json* found = description.find(path)) {
        (void)found->scalar_text();
    }
    (void)spz::key_from_name(path);
    return 0;
}
