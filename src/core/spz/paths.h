// SPDX-License-Identifier: GPL-3.0-or-later
// The two rules for the path of a tune inside the collection (SPEC-154).
#pragma once

#include <string>

namespace spz {

// The strict rule: for every path taken from the song length database, and every path a fetched file is
// written to. Starts with "/", no empty part, no "." or ".." part, ends in ".sid" in any case, at most
// 255 characters, and only A-Z a-z 0-9 _ - . and /.
bool is_hvsc_path(const std::string& path);

// The wider rule: for every other path of a tune inside the collection, which is only ever read. The
// same shape; at most 1,024 bytes; any valid UTF-8 but control characters and the backslash.
bool is_collection_path(const std::string& path);

}  // namespace spz
