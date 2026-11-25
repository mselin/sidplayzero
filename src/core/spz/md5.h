// SPDX-License-Identifier: GPL-3.0-or-later
// MD5, as HVSC's song length database identifies a tune's file (SPEC-008). The app's own, in `core`.
#pragma once

#include <string>

#include "spz/types.h"

namespace spz {

// The MD5 of some bytes, as 32 lower-case hexadecimal digits (RFC 1321).
std::string md5_hex(const Bytes& data);

}  // namespace spz
