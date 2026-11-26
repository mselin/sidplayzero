// SPDX-License-Identifier: GPL-3.0-or-later
// Paths as text: their parts, taken apart and put together, with no file looked at. Two kinds of path,
// each with its own idea of a parent:
//   a path of the file system ("/home/u/.config/sidplayzero/state.ini", "state.ini"), whose parent is
//   "." when it has no slash;
//   a path inside the collection ("/MUSICIANS/H/Hubbard_Rob", SPEC-154), always under the root "/", whose
//   parent is "/" for a part of the root.
#pragma once

#include <string>

namespace spz::path_text {

// What a path names in its folder: what follows its last slash, the whole path if it has none.
// "/a/b.sid" gives "b.sid", "b.sid" gives "b.sid", "/a/" gives "".
std::string last_part(const std::string& path);

// The folder a file-system path is in: "/a/b" for "/a/b/c", "/" for "/c", "." for "c".
std::string folder_of(const std::string& path);

// `name` in a file-system folder: "a" and "b" give "a/b", "a/" and "b" give "a/b", "" and "b" give "b".
std::string join(const std::string& folder, const std::string& name);

// The folder a collection path is in: "/A" for "/A/B", "/" for "/A" (and for a path with no slash).
std::string collection_parent(const std::string& path);

// `name` in a collection folder: "/" and "A" give "/A"; "/A" and "B" give "/A/B".
std::string collection_child(const std::string& folder, const std::string& name);

}  // namespace spz::path_text
