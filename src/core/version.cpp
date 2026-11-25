// SPDX-License-Identifier: GPL-3.0-or-later
#include "spz/version.h"

#ifndef SPZ_VERSION
#error "SPZ_VERSION must be defined by the build"
#endif

namespace spz {

const char* app_name() { return "SidPlayZero"; }
const char* app_version() { return SPZ_VERSION; }
const char* version_line() { return "SidPlayZero " SPZ_VERSION; }

}  // namespace spz
