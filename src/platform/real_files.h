// SPDX-License-Identifier: GPL-3.0-or-later
// The real file system (SPEC-021): Files over POSIX calls. Safe to use from several threads at once.
#pragma once

#include "spz/files.h"

namespace spz::platform {

class RealFiles : public Files {
public:
    FileStatus read(const std::string& path, std::size_t limit, std::vector<std::uint8_t>* out) override;
    FileStatus write(const std::string& path, const std::vector<std::uint8_t>& bytes) override;
    FileStatus append(const std::string& path, const std::vector<std::uint8_t>& bytes) override;
    // An mmap of the whole file, private and read-only; the mapping is undone when the last copy of the
    // MappedFile goes.
    FileStatus map(const std::string& path, std::size_t limit, MappedFile* out) override;
    FileStatus sync(const std::string& path) override;
    FileStatus rename(const std::string& from, const std::string& to) override;
    FileStatus remove(const std::string& path) override;
    FileStatus make_folders(const std::string& path) override;
    FileStatus list(const std::string& folder, std::vector<FileInfo>* out) override;
    FileStatus info(const std::string& path, FileInfo* out) override;
    FileStatus free_bytes(const std::string& path, std::uint64_t* out) override;
    FileStatus block_size(const std::string& path, std::uint64_t* out) override;
    bool can_write(const std::string& folder) override;
};

}  // namespace spz::platform
