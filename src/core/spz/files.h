// SPDX-License-Identifier: GPL-3.0-or-later
// Seam: the file system (SPEC-021). Every file `core` reads or writes goes through this.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spz {

enum class FileStatus {
    Ok,
    NotFound,
    NoAccess,   // may not be read, or may not be written
    NoSpace,    // the card is full
    TooLarge,   // larger than the limit the caller gave
    Failed,     // anything else
};

struct FileInfo {
    std::string name;            // the last part of the path, as bytes
    bool is_folder = false;
    bool is_link = false;        // a symbolic link, not followed
    bool is_special = false;     // a device, socket or pipe
    std::uint64_t size = 0;
    std::int64_t modified_s = 0; // seconds since 1970
};

class Files {
public:
    virtual ~Files() = default;

    // Reads a whole file. TooLarge, with nothing read, if it is longer than `limit` bytes.
    virtual FileStatus read(const std::string& path, std::size_t limit, std::vector<std::uint8_t>* out) = 0;
    // Creates or replaces a file. Not safe against a crash by itself: SPEC-132 builds on write, sync and rename.
    virtual FileStatus write(const std::string& path, const std::vector<std::uint8_t>& bytes) = 0;
    // Flushes a file, or a folder's entries, to the card.
    virtual FileStatus sync(const std::string& path) = 0;
    // Renames over whatever is at `to`.
    virtual FileStatus rename(const std::string& from, const std::string& to) = 0;
    // Deletes a file or an empty folder. Ok if it was not there.
    virtual FileStatus remove(const std::string& path) = 0;
    // Creates a folder and its parents, for the user only. Ok if it exists.
    virtual FileStatus make_folders(const std::string& path) = 0;
    // The entries of a folder, in no particular order, without "." and "..".
    virtual FileStatus list(const std::string& folder, std::vector<FileInfo>* out) = 0;
    // What is at a path. Links are not followed.
    virtual FileStatus info(const std::string& path, FileInfo* out) = 0;
    // Free space on the file system that holds `path`, or its nearest existing parent.
    virtual FileStatus free_bytes(const std::string& path, std::uint64_t* out) = 0;
    // Whether a file could be created in this folder now.
    virtual bool can_write(const std::string& folder) = 0;
};

}  // namespace spz
