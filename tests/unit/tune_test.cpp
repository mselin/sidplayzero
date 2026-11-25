// SPDX-License-Identifier: GPL-3.0-or-later
// The checks a file passes before anything trusts it: SPEC-203 (a tune's file), SPEC-154 (a tune's path),
// SPEC-008 (the MD5 that names a tune), SPEC-098 (the text in a tune's header).
#include <string>
#include <vector>

#include "doctest.h"
#include "spz/md5.h"
#include "spz/paths.h"
#include "spz/tune.h"
#include "support.h"

using spz::test::bytes;
using spz::test::fixture;

TEST_CASE("SPEC-203 every good fixture tune passes the check") {
    for (const char* name : {"commando_test", "three_subtunes", "two_sid", "three_sid", "model_8580", "clock_ntsc",
                             "needs_basic", "huelsbeck_test", "cp1252_1"}) {
        CAPTURE(name);
        CHECK(spz::check_tune(fixture(std::string("tunes/") + name + ".sid")) == "");
    }
}

TEST_CASE("SPEC-203 a tune that breaks any row of the table is refused with MSG_INVALID_SID") {
    for (const char* name : {"bad_too_large", "bad_only_header", "bad_cut_in_header", "bad_magic", "bad_version_0",
                             "bad_version_5", "bad_rsid_version_1", "bad_data_offset", "bad_no_songs",
                             "bad_too_many_songs", "bad_default_song", "bad_empty", "bad_noise"}) {
        CAPTURE(name);
        CHECK(spz::check_tune(fixture(std::string("tunes/") + name + ".sid")) == "MSG_INVALID_SID");
    }
}

TEST_CASE("SPEC-203 the limits of the table are exact: 256 songs pass, a default song of 0 is read as 1") {
    spz::Bytes tune = fixture("tunes/commando_test.sid");
    tune[0x0E] = 0x01; tune[0x0F] = 0x00;          // 256 songs
    CHECK(spz::check_tune(tune) == "");
    tune[0x0E] = 0x00; tune[0x0F] = 0x01;          // one song again
    tune[0x10] = 0x00; tune[0x11] = 0x00;          // default song 0
    REQUIRE(spz::check_tune(tune) == "");
    spz::TuneInfo info;
    REQUIRE(spz::read_tune_info(tune, &info));
    CHECK(info.default_song == 1);
}

TEST_CASE("SPEC-203 a good tune cut short at any length is refused or passes, and never reads past its end") {
    const spz::Bytes whole = fixture("tunes/three_sid.sid");
    for (std::size_t length = 0; length < whole.size(); ++length) {
        const spz::Bytes cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
        const std::string verdict = spz::check_tune(cut);
        CAPTURE(length);
        CHECK((verdict == "" || verdict == "MSG_INVALID_SID"));
        if (length <= 0x7C) CHECK(verdict == "MSG_INVALID_SID");
    }
}

TEST_CASE("SPEC-203 SPEC-096 the header's fields are read as the fixture's header gives them") {
    spz::TuneInfo info;
    REQUIRE(spz::read_tune_info(fixture("tunes/commando_test.sid"), &info));
    CHECK(info.title == "Commando Test");
    CHECK(info.author == "Test Author");
    CHECK(info.released == "2026 SidPlayZero");
    CHECK(info.year == 2026);
    CHECK(info.songs == 1);
    CHECK(info.default_song == 1);
    CHECK(info.chips == 1);
    CHECK(info.model == spz::SidModel::Mos6581);
    CHECK(info.clock == spz::C64Clock::Pal);
    CHECK_FALSE(info.needs_basic);

    REQUIRE(spz::read_tune_info(fixture("tunes/three_subtunes.sid"), &info));
    CHECK(info.songs == 3);
    CHECK(info.default_song == 2);
    REQUIRE(spz::read_tune_info(fixture("tunes/three_sid.sid"), &info));
    CHECK(info.chips == 3);
    REQUIRE(spz::read_tune_info(fixture("tunes/model_8580.sid"), &info));
    CHECK(info.model == spz::SidModel::Mos8580);
    REQUIRE(spz::read_tune_info(fixture("tunes/model_either.sid"), &info));
    CHECK(info.model == spz::SidModel::FromTune);
    REQUIRE(spz::read_tune_info(fixture("tunes/clock_ntsc.sid"), &info));
    CHECK(info.clock == spz::C64Clock::Ntsc);
    REQUIRE(spz::read_tune_info(fixture("tunes/needs_basic.sid"), &info));
    CHECK(info.needs_basic);
}

TEST_CASE("SPEC-098 SPEC-010 text from a tune's header is converted from Windows-1252, every printable character of it") {
    spz::TuneInfo info;
    REQUIRE(spz::read_tune_info(fixture("tunes/huelsbeck_test.sid"), &info));
    CHECK(info.title == "H\xC3\xBClsbeck T\xC3\xABst");                 // "Hülsbeck Tëst"
    CHECK(info.author == "Thirty-two characters of author!");           // a field with no zero byte at its end
    CHECK(spz::cp1252_to_utf8("\x80") == "\xE2\x82\xAC");               // the euro sign
    CHECK(spz::cp1252_to_utf8("\x8A\x9A") == "\xC5\xA0\xC5\xA1");       // S and s with caron
    CHECK(spz::cp1252_to_utf8("\x85\x99") == "\xE2\x80\xA6\xE2\x84\xA2");  // ellipsis, trade mark
    CHECK(spz::cp1252_to_utf8("\x81") == "?");                          // undefined in Windows-1252
    CHECK(spz::cp1252_to_utf8(std::string("ab\0cd", 5)) == "ab");       // cut at the first zero byte
    CHECK(spz::cp1252_to_utf8("\xE9\xFF") == "\xC3\xA9\xC3\xBF");
}

TEST_CASE("SPEC-008 the MD5 function gives the test vectors of RFC 1321") {
    CHECK(spz::md5_hex(bytes("")) == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(spz::md5_hex(bytes("a")) == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(spz::md5_hex(bytes("abc")) == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(spz::md5_hex(bytes("message digest")) == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(spz::md5_hex(bytes("abcdefghijklmnopqrstuvwxyz")) == "c3fcd3d76192e4007dfb496cca67e13b");
    CHECK(spz::md5_hex(bytes("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")) ==
          "d174ab98d277d9f5a5611c2c9f419d9f");
    CHECK(spz::md5_hex(bytes("12345678901234567890123456789012345678901234567890123456789012345678901234567890")) ==
          "57edf4a22be3c955ac49da2e2107b67a");
}

TEST_CASE("SPEC-154 an HVSC path has HVSC's shape and alphabet and nothing else") {
    CHECK(spz::is_hvsc_path("/MUSICIANS/H/Hubbard_Rob/Commando.sid"));
    CHECK(spz::is_hvsc_path("/DEMOS/A-F/Devils_ReSIDence_3SID.SID"));
    CHECK(spz::is_hvsc_path("/a.sid"));
    for (const char* bad : {"MUSICIANS/a.sid", "/MUSICIANS/../a.sid", "/MUSICIANS/./a.sid", "/MUSICIANS//a.sid",
                            "/MUSICIANS\\a.sid", "/MUSICIANS/a\x01.sid", "/My Rips/a.sid", "/MUSICIANS/a.txt",
                            "/MUSICIANS/a.sid/", "/", "", "/MUSICIANS/S\xC3\xB6ng.sid"}) {
        CAPTURE(bad);
        CHECK_FALSE(spz::is_hvsc_path(bad));
    }
    const std::string longest = "/" + std::string(250, 'a') + ".sid";   // 255 characters
    CHECK(spz::is_hvsc_path(longest));
    CHECK_FALSE(spz::is_hvsc_path("/a" + longest.substr(1)));           // 256
}

TEST_CASE("SPEC-154 a collection path may have spaces and accents, and still no way out of the collection") {
    CHECK(spz::is_collection_path("/My Rips/Cool S\xC3\xB6ng.sid"));
    CHECK(spz::is_collection_path("/MUSICIANS/H/Hubbard_Rob/Commando.sid"));
    for (const char* bad : {"My Rips/a.sid", "/My Rips/../a.sid", "/My Rips/./a.sid", "/My Rips//a.sid",
                            "/My Rips\\a.sid", "/My Rips/a\x01.sid", "/My Rips/a.txt", "/My Rips/\xFF.sid", ""}) {
        CAPTURE(bad);
        CHECK_FALSE(spz::is_collection_path(bad));
    }
    CHECK(spz::is_collection_path("/" + std::string(1019, 'a') + ".sid"));      // 1,024 bytes
    CHECK_FALSE(spz::is_collection_path("/" + std::string(1020, 'a') + ".sid"));
}
