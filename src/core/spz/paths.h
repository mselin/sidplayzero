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

// The address of a file under a base address such as the setting `url_files` (SPEC-141): `base` with any
// slash at its end dropped, then `path`, which starts with "/". Nothing is percent-encoded, and nothing
// needs to be: every path joined is an HVSC path (is_hvsc_path: only A-Z a-z 0-9 _ - . and /) or one of
// HVSC's own document names ("/DOCUMENTS/Songlengths.md5"), and those characters stand for themselves in
// an address (RFC 3986's unreserved characters, and the slash).
std::string join_url(const std::string& base, const std::string& path);

}  // namespace spz
