// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-132: writing a file so that it is never left half-written; SPEC-202: clearing away what a killed
// run left behind.
#include "spz/safe_file.h"

#include <cstring>
#include <vector>

namespace spz {

namespace {

bool ends_with(const std::string& text, const std::string& ending) {
    return text.size() >= ending.size() && text.compare(text.size() - ending.size(), ending.size(), ending) == 0;
}

// The folder a path is in: "/a/b" for "/a/b/c", "/" for "/c", "." for "c".
std::string folder_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos) return ".";
    return slash == 0 ? "/" : path.substr(0, slash);
}

// What a path names in its folder.
std::string name_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

std::string temp_path_for(const std::string& target) {
    const std::size_t slash = target.rfind('/');
    const std::string folder = slash == std::string::npos ? "" : target.substr(0, slash + 1);
    return folder + "." + name_of(target) + kTempEnding;
}

bool is_temp_name(const std::string& name) {
    // A dot, at least one byte of the target's name, and the ending.
    return name.size() > 1 + std::strlen(kTempEnding) && name[0] == '.' && ends_with(name, kTempEnding);
}

FileStatus safe_commit(Files* files, const std::string& target) {
    const std::string temp = temp_path_for(target);
    FileStatus status = files->sync(temp);
    if (status == FileStatus::Ok) status = files->rename(temp, target);
    if (status != FileStatus::Ok) {
        // The target is as it was. What is left at the temporary name goes now, or at the next start.
        files->remove(temp);
        return status;
    }
    // The target holds the new bytes from here on. A failure to flush the folder is still reported, since
    // the rename may not have reached the card.
    return files->sync(folder_of(target));
}

FileStatus safe_write(Files* files, const std::string& target, const Bytes& bytes) {
    const FileStatus made = files->make_folders(folder_of(target));
    if (made != FileStatus::Ok) return made;
    const std::string temp = temp_path_for(target);
    const FileStatus written = files->write(temp, bytes);
    if (written != FileStatus::Ok) {
        files->remove(temp);
        return written;
    }
    return safe_commit(files, target);
}

int remove_temp_files(Files* files, const std::string& folder) {
    std::vector<FileInfo> entries;
    if (files->list(folder, &entries) != FileStatus::Ok) return 0;
    int removed = 0;
    for (const FileInfo& entry : entries) {
        if (entry.is_folder || !is_temp_name(entry.name)) continue;
        if (files->remove(folder + "/" + entry.name) == FileStatus::Ok) ++removed;
    }
    return removed;
}

int remove_old_partials(Files* files, const Clock* clock, const std::string& cache_folder) {
    std::vector<FileInfo> entries;
    if (files->list(cache_folder, &entries) != FileStatus::Ok) return 0;
    const std::int64_t now = clock->wall_time_s();
    int removed = 0;
    for (const FileInfo& entry : entries) {
        if (entry.is_folder || entry.name.size() <= std::strlen(kPartEnding) || !ends_with(entry.name, kPartEnding)) continue;
        if (now - entry.modified_s < kPartMaxAgeS) continue;
        const std::string part = cache_folder + "/" + entry.name;
        if (files->remove(part) != FileStatus::Ok) continue;
        files->remove(part + ".meta");
        ++removed;
    }
    return removed;
}

}  // namespace spz
