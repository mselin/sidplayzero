// SPDX-License-Identifier: GPL-3.0-or-later
// Seam: the file system (SPEC-021). Every file `core` reads or writes goes through this.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
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
    std::int32_t modified_ns = 0; // and nanoseconds past that second, 0 to 999,999,999, as the file
                                  // system gives them (FIND-019): a change within the same second is seen
};

// Whether two FileInfo give the same modification time, to the nanosecond.
inline bool same_time(const FileInfo& a, const FileInfo& b) {
    return a.modified_s == b.modified_s && a.modified_ns == b.modified_ns;
}

// A file mapped read-only (FIND-019): its bytes, which stay valid and unchanged for as long as any copy of
// this object is kept, whatever happens to the file meanwhile (it is replaced by a rename, never written
// in place). An empty file maps to size() 0. Copies are cheap and share the mapping.
class MappedFile {
public:
    MappedFile() = default;
    // For implementations of Files: `owner` keeps the bytes alive (an mmap, or a buffer in memory) and
    // releases them when the last copy goes.
    MappedFile(const std::uint8_t* data, std::size_t size, std::shared_ptr<const void> owner)
        : data_(data), size_(size), owner_(std::move(owner)) {}

    const std::uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }
    // Whether this holds a mapping at all (an empty file's mapping is valid with size() 0).
    bool is_mapped() const { return owner_ != nullptr; }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::shared_ptr<const void> owner_;
};

class Files {
public:
    virtual ~Files() = default;

    // Reads a whole file. TooLarge, with nothing read, if it is longer than `limit` bytes.
    virtual FileStatus read(const std::string& path, std::size_t limit, std::vector<std::uint8_t>* out) = 0;
    // Creates or replaces a file. Not safe against a crash by itself: SPEC-132 builds on write, sync and rename.
    virtual FileStatus write(const std::string& path, const std::vector<std::uint8_t>& bytes) = 0;
    // Adds bytes at the end of a file, creating it if it is not there (FIND-019: selftest.results, SPEC-241).
    virtual FileStatus append(const std::string& path, const std::vector<std::uint8_t>& bytes) = 0;
    // Maps a whole file read-only (FIND-019: listing.bin, SPEC-142). TooLarge, with nothing mapped, if it is
    // longer than `limit` bytes. On anything but Ok, `out` is left empty.
    virtual FileStatus map(const std::string& path, std::size_t limit, MappedFile* out) = 0;
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
    // The block size of the file system that holds `path`, or its nearest existing parent: what a file's
    // size is rounded up to on the card (FIND-019, SPEC-146).
    virtual FileStatus block_size(const std::string& path, std::uint64_t* out) = 0;
    // Whether a file could be created in this folder now.
    virtual bool can_write(const std::string& folder) = 0;
};

}  // namespace spz
