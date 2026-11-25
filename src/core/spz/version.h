// SPDX-License-Identifier: GPL-3.0-or-later
// The app's name and version. The version comes from app-builder.json at build time (SPEC-023).
#pragma once

namespace spz {

// "SidPlayZero"
const char* app_name();
// The version in app-builder.json, such as "0.1.0".
const char* app_version();
// The first line `sidplayzero --version` prints: "SidPlayZero 0.1.0".
const char* version_line();

}  // namespace spz
