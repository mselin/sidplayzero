// SPDX-License-Identifier: GPL-3.0-or-later
// What the tests of playlists, favourites and importing share: a collection of the test's own, copied
// from the fixture collection, with its listing, the fake files, and the playlists on top.
#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "spz/listing.h"
#include "spz/playlist_resolve.h"
#include "spz/playlists.h"
#include "support.h"

namespace spz::test {

// The fixture collection's tunes, and the MD5 of each one's file (tools/make_fixtures.py makes the same
// bytes on every run).
const char* const kCommando = "/MUSICIANS/H/Hubbard_Rob/Commando.sid";            // one subtune, 0:03.500
const char* const kThreeSongs = "/MUSICIANS/T/Tester_Tom/Three_Songs.sid";        // 0:02 0:02.500 0:03, default 2
const char* const kClockDemo = "/DEMOS/A-F/Clock_Demo.sid";                       // 0:02
const char* const kEitherChip = "/GAMES/A-F/Either_Chip.sid";                     // 0:02
const char* const kHuelsbeck = "/MUSICIANS/H/Huelsbeck_Chris/Huelsbeck_Test.sid"; // 0:02
const char* const kNewChip = "/MUSICIANS/T/Tester_Tom/New_Chip.sid";
const char* const kTwoChips = "/MUSICIANS/T/Tester_Tom/Two_Chips_2SID.sid";
const char* const kThreeChips = "/MUSICIANS/T/Tester_Tom/Three_Chips_3SID.sid";
const char* const kRenamed = "/MUSICIANS/T/Tester_Tom/Renamed.sid";
const char* const kOwnTune = "/My Rips/Cool S\xC3\xB6ng.sid";                     // not HVSC's: "Cool Söng"
const char* const kNothing = "/MUSICIANS/N/Nobody/Nothing.sid";                   // nowhere

const char* const kMd5Commando = "c87c5ccd81d598c95b2e250c1f33b74d";
const char* const kMd5ThreeSongs = "d06b7955032f0cc8cd8cb358935291f6";
const char* const kMd5ClockDemo = "8e9071bbe73f26eeaac2f231f6521852";
const char* const kMd5NewChip = "58e55b6aaf8784f171d31016f54ab95f";
const char* const kMd5TwoChips = "f5125d35cf1f5fe2179b79dcaaac34cc";
const char* const kMd5Renamed = "54651eb33a8b1510ea676d648fa7f86a";
const char* const kMd5OwnTune = "a8777ab98bcd6c2c2070ea8af7eb8c03";

inline std::string text(const Bytes& data) { return std::string(data.begin(), data.end()); }

// A real file's contents, "" if it is not there.
inline std::string file_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline bool exists(const std::string& path) { return std::filesystem::exists(path); }

// Strings as one text, a line each: for comparisons that print what they found.
inline std::string join(const std::vector<std::string>& lines) {
    std::string all;
    for (const std::string& line : lines) all += line + "\n";
    return all;
}

// The lines of a text, without their line feeds.
inline std::vector<std::string> lines_of(const std::string& all) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < all.size()) {
        const std::size_t end = all.find('\n', start);
        lines.push_back(all.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

// Whether a text is valid UTF-8 with no control character but the line feed (SPEC-009).
inline bool is_utf8_text(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t more = 0;
        char32_t point = c;
        if (c < 0x80) {
            if (c < 0x20 && c != '\n') return false;
        } else if (c >= 0xC2 && c <= 0xDF) {
            more = 1; point = c & 0x1F;
        } else if (c >= 0xE0 && c <= 0xEF) {
            more = 2; point = c & 0x0F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            more = 3; point = c & 0x07;
        } else {
            return false;
        }
        if (more > 0 && i + more >= s.size()) return false;
        for (std::size_t k = 1; k <= more; ++k) {
            const unsigned char next = static_cast<unsigned char>(s[i + k]);
            if ((next & 0xC0) != 0x80) return false;
            point = (point << 6) | (next & 0x3F);
        }
        if ((more == 2 && point < 0x800) || (more == 3 && (point < 0x10000 || point > 0x10FFFF))) return false;
        if (point >= 0xD800 && point <= 0xDFFF) return false;
        i += more + 1;
    }
    return true;
}

// The search index's part in SPEC-173, faked: the user's own tunes by MD5. It counts its calls.
class FakeOwnTunes : public OwnTunes {
public:
    std::map<std::string, std::string> by_md5;
    mutable int calls = 0;
    bool own_tune_by_md5(const std::string& md5, std::string* path) const override {
        ++calls;
        const auto found = by_md5.find(md5);
        if (found == by_md5.end()) return false;
        *path = found->second;
        return true;
    }
};

// A collection of the test's own with everything the lists need around it.
struct ListsWorld {
    TempFolder temp;
    fake::Files files;
    std::string root;               // the collection root: a copy of the fixture collection, release 85
    std::string playlists_folder;
    std::string import_folder;
    std::string export_folder;
    Listing listing;
    FakeOwnTunes own;
    std::unique_ptr<EntryResolver> resolver;
    std::unique_ptr<Playlists> playlists;

    // `root_name` is the root folder's name. With `with_listing` false the collection is not HVSC's: its
    // DOCUMENTS folder is removed and there is no listing.
    explicit ListsWorld(const std::string& root_name = "C64Music", bool with_listing = true) {
        root = temp.file(root_name);
        copy_fixture_tree("collections/85/C64Music", root);
        playlists_folder = temp.file("share/sidplayzero/playlists");
        import_folder = temp.file("share/sidplayzero/import");
        export_folder = temp.file("share/sidplayzero/export");
        std::filesystem::create_directories(import_folder);
        if (with_listing) build_listing();
        else std::filesystem::remove_all(root + "/DOCUMENTS");
        resolver.reset(new EntryResolver(&files, root, &listing, &own));
        playlists.reset(new Playlists(&files, playlists_folder, resolver.get()));
    }

    // Builds the listing from the root's song length database, as it is now, and opens it.
    void build_listing() {
        const std::string database = root + "/DOCUMENTS/Songlengths.md5";
        const std::string built = temp.file("cache/listing.bin");
        std::filesystem::create_directories(temp.path / "cache");
        REQUIRE(Listing::build(&files, database, built).ok);
        REQUIRE(listing.open(&files, built, database));
    }

    // A second start of the app: the playlists read from the folder again.
    void restart() {
        playlists.reset(new Playlists(&files, playlists_folder, resolver.get()));
        playlists->load();
    }

    // Puts a file, by hand, where the app keeps its playlists.
    std::string put_playlist(const std::string& file_name, const std::string& contents) {
        return temp.write("share/sidplayzero/playlists/" + file_name, contents);
    }
    // The user's own tune of the fixtures, at its path under the root.
    void put_own_tune() { temp.write(std::filesystem::path(root).filename().string() + kOwnTune, fixture("own/My Rips/Cool S\xC3\xB6ng.sid")); }
    // Takes a tune's file away: if HVSC lists it, it is absent from then on.
    void take_away(const std::string& path) { std::filesystem::remove(root + path); }

    // The paths and the subtune numbers of a playlist, as "path" or "path#N".
    std::vector<std::string> entries_of(const std::string& name) const {
        std::vector<std::string> out;
        const Playlist* playlist = playlists->find(name);
        if (playlist == nullptr) return out;
        for (const PlaylistEntry& entry : playlist->entries) {
            out.push_back(entry.entry.subtune ? entry.entry.path + "#" + std::to_string(entry.entry.subtune) : entry.entry.path);
        }
        return out;
    }

    // Every write, rename, remove and make_folders the code under test asked of the fake files that did
    // not go into `folder`: there must be none (SPEC-154). The listing, which this world builds for
    // itself in its cache folder, is not counted.
    std::vector<std::string> changes_outside(const std::string& folder) const {
        const std::string cache = temp.file("cache");
        std::vector<std::string> out;
        for (const fake::Files::Operation& op : files.log) {
            const bool changes = op.what == "write" || op.what == "rename" || op.what == "remove" || op.what == "make_folders";
            if (!changes) continue;
            for (const std::string& path : {op.path, op.to}) {
                if (path.empty() || path.compare(0, cache.size(), cache) == 0) continue;
                if (path.compare(0, folder.size(), folder) != 0) out.push_back(op.what + " " + path);
            }
        }
        return out;
    }
};

}  // namespace spz::test
