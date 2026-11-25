// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-204: files that come from outside are read with limits. The large files are made here, in a folder
// of the test's own, and not kept as fixtures. The two seconds are measured on this machine's own clock, as
// SPEC-240's table says of this budget.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "keep_support.h"
#include "spz/limits.h"
#include "spz/messages.h"
#include "support.h"

using spz::test::TempFolder;
using spz::test::bytes;

namespace {

const std::size_t kKiB = 1024;
const std::size_t kMiB = 1024 * 1024;
const std::int64_t kBudgetMs = 2000;      // SPEC-204 ‡

struct Row {
    spz::FileKind kind;
    const char* name;
    std::size_t limit;
};

// SPEC-204's table.
const std::vector<Row> kTable = {
    {spz::FileKind::Settings, "settings.ini", 64 * kKiB},
    {spz::FileKind::State, "state.ini", 64 * kKiB},
    {spz::FileKind::HvSids, "hv_sids.txt", 64 * kKiB},
    {spz::FileKind::Playlist, "a playlist.m3u8", 4 * kMiB},
    {spz::FileKind::Import, "imported.m3u", 4 * kMiB},
    {spz::FileKind::HvscText, "HVSC.txt", 1 * kMiB},
    {spz::FileKind::BugList, "BUGlist.txt", 1 * kMiB},
    {spz::FileKind::Stil, "STIL.txt", 16 * kMiB},
    {spz::FileKind::SongLengths, "Songlengths.md5", 32 * kMiB},
    {spz::FileKind::ArchiveListing, "listing of an archive.txt", 64 * kMiB},
};

const char* message_for(spz::FileKind kind) {
    return kind == spz::FileKind::SongLengths ? "MSG_LENGTHS_UNREADABLE" : "MSG_FILE_TOO_LARGE";
}

// A file of exactly `size` bytes, made without writing them: what is in it does not matter here.
std::string file_of_size(const TempFolder& folder, const std::string& name, std::size_t size) {
    const std::string path = folder.file(name);
    std::ofstream(path, std::ios::binary).close();
    std::filesystem::resize_file(path, size);
    return path;
}

class Stopwatch {
public:
    std::int64_t ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

}  // namespace

TEST_CASE("SPEC-204 the limits are those of the table, and a size is refused from one byte past its limit") {
    CHECK(spz::kLineLimit == 4096);
    CHECK(spz::kPlaylistEntryLimit == 20000);
    for (const Row& row : kTable) {
        CAPTURE(row.name);
        CHECK(spz::size_limit(row.kind) == row.limit);
        CHECK(spz::refuse_size(row.kind, 0) == "");
        CHECK(spz::refuse_size(row.kind, row.limit) == "");
        CHECK(spz::refuse_size(row.kind, row.limit + 1) == message_for(row.kind));
        CHECK(spz::refuse_size(row.kind, std::uint64_t{8} * 1024 * kMiB) == message_for(row.kind));
    }
    CHECK(spz::is_message("MSG_FILE_TOO_LARGE"));
    CHECK(spz::is_message("MSG_LENGTHS_UNREADABLE"));
}

TEST_CASE("SPEC-204 a file one byte past its limit is refused with its message, unread, within two seconds") {
    for (const Row& row : kTable) {
        CAPTURE(row.name);
        TempFolder folder;
        spz::fake::Files files;
        const std::string path = file_of_size(folder, row.name, row.limit + 1);
        spz::Bytes data = bytes("what was here before");
        std::string why = "not set";
        const Stopwatch watch;
        const spz::FileStatus status = spz::read_limited(&files, path, row.kind, &data, &why);
        CHECK(watch.ms() < kBudgetMs);
        CHECK(status == spz::FileStatus::TooLarge);
        CHECK(why == message_for(row.kind));
        CHECK(data.empty());
        CHECK(files.count("read") <= 1);      // it went through the seam, and did not try again
        CHECK(files.calls >= 1);
    }
}

TEST_CASE("SPEC-204 a file of exactly its limit is read whole") {
    // For the kinds of up to 4 MiB. (The larger ones are refused one byte later all the same: see above.)
    // Not timed: the files the two seconds are asked of are those of the other tests here.
    for (const Row& row : kTable) {
        if (row.limit > 4 * kMiB) continue;
        CAPTURE(row.name);
        TempFolder folder;
        spz::fake::Files files;
        const std::string line = "; a line like any other in a file of its kind\n";
        std::string text;
        text.reserve(row.limit);
        while (text.size() + line.size() <= row.limit) text += line;
        text.resize(row.limit, '.');
        REQUIRE(text.size() == row.limit);
        const std::string path = folder.write(row.name, text);

        spz::Bytes data;
        std::string why = "not set";
        const spz::FileStatus status = spz::read_limited(&files, path, row.kind, &data, &why);
        const std::vector<std::string> lines = spz::split_lines(data);
        CHECK(status == spz::FileStatus::Ok);
        CHECK(why == "");
        CHECK(data.size() == row.limit);
        REQUIRE(lines.size() >= row.limit / line.size());
        CHECK(lines[0] == "; a line like any other in a file of its kind");
        CHECK(lines[row.limit / line.size() - 1] == "; a line like any other in a file of its kind");
    }
}

TEST_CASE("SPEC-204 a file that is not there is not a file that is too large") {
    TempFolder folder;
    spz::fake::Files files;
    spz::Bytes data;
    std::string why = "not set";
    CHECK(spz::read_limited(&files, folder.file("none.txt"), spz::FileKind::Playlist, &data, &why) == spz::FileStatus::NotFound);
    CHECK(why == "");
    CHECK(data.empty());
}

TEST_CASE("SPEC-204 a line longer than 4,096 bytes is cut there: a file with a million-character line loads within two seconds") {
    TempFolder folder;
    spz::fake::Files files;
    const std::string text = "the line before\n" + std::string(1000000, 'x') + "\nthe line after\n";
    const std::string path = folder.write("imported.m3u", text);

    spz::Bytes data;
    std::string why;
    const Stopwatch watch;
    REQUIRE(spz::read_limited(&files, path, spz::FileKind::Import, &data, &why) == spz::FileStatus::Ok);
    const std::vector<std::string> lines = spz::split_lines(data);
    CHECK(watch.ms() < kBudgetMs);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "the line before");
    CHECK(lines[1] == std::string(4096, 'x'));
    CHECK(lines[2] == "the line after");

    // The limit is exact: 4,096 bytes are a whole line, 4,097 are cut.
    const std::vector<std::string> edges =
        spz::split_lines(bytes(std::string(4096, 'a') + "\n" + std::string(4097, 'b') + "\n" + std::string(4095, 'c') + "\n"));
    REQUIRE(edges.size() == 3);
    CHECK(edges[0] == std::string(4096, 'a'));
    CHECK(edges[1] == std::string(4096, 'b'));
    CHECK(edges[2] == std::string(4095, 'c'));

    // A file that is one long line with no end.
    const std::vector<std::string> endless = spz::split_lines(bytes(std::string(1000000, 'y')));
    REQUIRE(endless.size() == 1);
    CHECK(endless[0] == std::string(4096, 'y'));
}

TEST_CASE("SPEC-204 a zero byte ends its line, and a file with zero bytes inside loads within two seconds") {
    const std::string text = std::string("first\n") + std::string("sec\0ond", 7) + "\n" + std::string("\0gone", 5) + "\nlast\n";
    const std::vector<std::string> lines = spz::split_lines(bytes(text));
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "first");
    CHECK(lines[1] == "sec");
    CHECK(lines[2] == "");
    CHECK(lines[3] == "last");

    // A megabyte of zero bytes, and a megabyte of lines that each hold one.
    TempFolder folder;
    spz::fake::Files files;
    std::string many;
    while (many.size() < kMiB - 16) many += std::string("ab\0cd\n", 6);
    for (const std::string& contents : {std::string(kMiB, '\0'), many}) {
        const std::string path = folder.write("HVSC.txt", contents);
        spz::Bytes data;
        std::string why;
        const Stopwatch watch;
        REQUIRE(spz::read_limited(&files, path, spz::FileKind::HvscText, &data, &why) == spz::FileStatus::Ok);
        const std::vector<std::string> split = spz::split_lines(data);
        CHECK(watch.ms() < kBudgetMs);
        REQUIRE_FALSE(split.empty());
        for (const std::string& line : split) {
            if (line != "" && line != "ab") {
                FAIL_CHECK("a line holds what came after its zero byte");
                break;
            }
        }
    }
}

TEST_CASE("SPEC-204 lines end at LF or CRLF, and the last line need not end at all") {
    const std::vector<std::string> lines = spz::split_lines(bytes("[Database]\r\n; /MUSICIANS/a.sid\r\nlast"));
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "[Database]");
    CHECK(lines[1] == "; /MUSICIANS/a.sid");
    CHECK(lines[2] == "last");
    CHECK(spz::split_lines(spz::Bytes()).empty());
    const std::vector<std::string> blank = spz::split_lines(bytes("one\n\nthree\n"));
    REQUIRE(blank.size() == 3);
    CHECK(blank[1] == "");
}
