// SPDX-License-Identifier: GPL-3.0-or-later
// See spz/path_text.h.
#include "spz/path_text.h"

namespace spz::path_text {

std::string last_part(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string folder_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos) return ".";
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string join(const std::string& folder, const std::string& name) {
    if (folder.empty()) return name;
    return folder.back() == '/' ? folder + name : folder + "/" + name;
}

std::string collection_parent(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return path.substr(0, slash);
}

std::string collection_child(const std::string& folder, const std::string& name) {
    return folder == "/" ? "/" + name : folder + "/" + name;
}

}  // namespace spz::path_text
