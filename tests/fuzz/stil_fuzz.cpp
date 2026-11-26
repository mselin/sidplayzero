// SPDX-License-Identifier: GPL-3.0-or-later
// test: SPEC-205 fuzz: the STIL reader (SPEC-097) on any bytes as DOCUMENTS/STIL.txt
//
// The input is the whole STIL file. The first byte, if any, picks a subtune; the notes are asked for a
// path the starting files have an entry for, and for one taken from the input itself, so that the reader
// also meets a path that does match whatever it finds. Whatever the bytes, the reader must end, and must
// not read outside them.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "spz/files.h"
#include "spz/stil.h"
#include "spz/types.h"

namespace {

// A file system of one file: the input, at any path.
class OneFile : public spz::Files {
public:
    explicit OneFile(spz::Bytes bytes) : bytes_(std::move(bytes)) {}

    spz::FileStatus read(const std::string&, std::size_t limit, std::vector<std::uint8_t>* out) override {
        if (bytes_.size() > limit) return spz::FileStatus::TooLarge;
        *out = bytes_;
        return spz::FileStatus::Ok;
    }
    spz::FileStatus write(const std::string&, const std::vector<std::uint8_t>&) override { return spz::FileStatus::Failed; }
    spz::FileStatus append(const std::string&, const std::vector<std::uint8_t>&) override { return spz::FileStatus::Failed; }
    spz::FileStatus map(const std::string&, std::size_t, spz::MappedFile*) override { return spz::FileStatus::Failed; }
    spz::FileStatus sync(const std::string&) override { return spz::FileStatus::Ok; }
    spz::FileStatus rename(const std::string&, const std::string&) override { return spz::FileStatus::Failed; }
    spz::FileStatus remove(const std::string&) override { return spz::FileStatus::Ok; }
    spz::FileStatus make_folders(const std::string&) override { return spz::FileStatus::Failed; }
    spz::FileStatus list(const std::string&, std::vector<spz::FileInfo>*) override { return spz::FileStatus::NotFound; }
    spz::FileStatus info(const std::string&, spz::FileInfo* out) override {
        out->size = bytes_.size();
        return spz::FileStatus::Ok;
    }
    spz::FileStatus free_bytes(const std::string&, std::uint64_t* out) override {
        *out = 0;
        return spz::FileStatus::Ok;
    }
    spz::FileStatus block_size(const std::string&, std::uint64_t* out) override {
        *out = 4096;
        return spz::FileStatus::Ok;
    }
    bool can_write(const std::string&) override { return false; }

private:
    spz::Bytes bytes_;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    OneFile files(spz::Bytes(data, data + size));
    const int subtune = size > 0 ? data[0] % 8 : 1;
    (void)spz::stil_notes(&files, "/C64Music/DOCUMENTS/STIL.txt", "/A/First.sid", subtune);

    // A path from the input: the bytes after the first '/', up to the next line feed.
    std::string path;
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] != '/') continue;
        for (std::size_t j = i; j < size && data[j] != '\n' && j - i < 256; ++j) path += static_cast<char>(data[j]);
        break;
    }
    if (!path.empty()) {
        const spz::StilNotes notes = spz::stil_notes(&files, "/C64Music/DOCUMENTS/STIL.txt", path, subtune);
        (void)notes.text.size();
    }
    return 0;
}
