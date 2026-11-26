// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-204: files that come from outside are read with limits, and become lines in time in proportion to
// their bytes.
#include "spz/limits.h"

#include <cstring>

namespace spz {

std::size_t size_limit(FileKind kind) {
    const std::size_t KiB = 1024;
    const std::size_t MiB = 1024 * KiB;
    switch (kind) {
        case FileKind::Settings:
        case FileKind::State:
        case FileKind::HvSids: return 64 * KiB;
        case FileKind::Playlist:
        case FileKind::Import: return kPlaylistFileLimit;
        case FileKind::HvscText:
        case FileKind::BugList: return 1 * MiB;
        case FileKind::Stil: return 16 * MiB;
        case FileKind::SongLengths: return 32 * MiB;
        case FileKind::ArchiveListing: return 64 * MiB;
    }
    return 0;
}

std::string refuse_size(FileKind kind, std::uint64_t size) {
    if (size <= size_limit(kind)) return "";
    return kind == FileKind::SongLengths ? "MSG_LENGTHS_UNREADABLE" : "MSG_FILE_TOO_LARGE";
}

FileStatus read_limited(Files* files, const std::string& path, FileKind kind, Bytes* out, std::string* why) {
    out->clear();
    why->clear();
    const std::size_t limit = size_limit(kind);
    const FileStatus status = files->read(path, limit, out);
    if (status != FileStatus::Ok) {
        out->clear();
        if (status == FileStatus::TooLarge) *why = refuse_size(kind, std::uint64_t{limit} + 1);
        return status;
    }
    // A file that grew between the size check and the read is refused all the same.
    if (out->size() > limit) {
        out->clear();
        *why = refuse_size(kind, std::uint64_t{limit} + 1);
        return FileStatus::TooLarge;
    }
    return FileStatus::Ok;
}

std::vector<std::string> split_lines(const Bytes& data) {
    std::vector<std::string> lines;
    const char* const begin = reinterpret_cast<const char*>(data.data());
    const std::size_t size = data.size();
    std::size_t at = 0;
    while (at < size) {
        const void* lf = std::memchr(begin + at, '\n', size - at);
        const std::size_t end = lf == nullptr ? size : static_cast<std::size_t>(static_cast<const char*>(lf) - begin);
        // The line's bytes: up to its end, a zero byte, or the limit, whichever comes first.
        std::size_t length = end - at;
        if (length > kLineLimit) length = kLineLimit;
        const void* zero = std::memchr(begin + at, '\0', length);
        const bool whole = zero == nullptr && at + length == end;
        if (zero != nullptr) length = static_cast<std::size_t>(static_cast<const char*>(zero) - (begin + at));
        std::string line(begin + at, length);
        // A CR just before the LF (or the end of the file) is not part of the line.
        if (whole && !line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        at = end + 1;
    }
    return lines;
}

}  // namespace spz
