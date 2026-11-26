// SPDX-License-Identifier: GPL-3.0-or-later
// SHA-256, as the self-test identifies the samples the engines render (SPEC-241: `render_hash_accurate`,
// `render_hash_light`). The app's own, in `core`, beside md5.h.
#pragma once

#include <string>

#include "spz/types.h"

namespace spz {

// The SHA-256 of some bytes, as 64 lower-case hexadecimal digits (FIPS 180-4).
std::string sha256_hex(const Bytes& data);

}  // namespace spz
