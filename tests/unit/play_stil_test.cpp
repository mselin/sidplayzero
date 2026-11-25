// SPDX-License-Identifier: GPL-3.0-or-later
// Notes from STIL (SPEC-097): the entry that DOCUMENTS/STIL.txt has for a tune, the part for the file and
// then the part for the playing subtune, converted from Latin-1. Read through the fake Files from the
// fixture collections' STIL files.
#include <string>

#include "doctest.h"
#include "fakes.h"
#include "spz/stil.h"
#include "support.h"

using spz::test::fixture_path;

#if SPZ_PART_NOTES
namespace {

// The STIL file of the fixture collection for release 85 (tools/make_fixtures.py), and that of the tests
// of playing (tools/fixture_makers/play.py).
const std::string kStil85 = "collections/85/C64Music/DOCUMENTS/STIL.txt";
const std::string kStilPlay = "play/C64Music/DOCUMENTS/STIL.txt";

spz::StilNotes notes(const std::string& stil, const std::string& path, int subtune) {
    spz::fake::Files files;
    const spz::StilNotes found = spz::stil_notes(&files, fixture_path(stil), path, subtune);
    CHECK(files.count("read") >= 1);                        // through the Files seam
    return found;
}

}  // namespace

TEST_CASE("SPEC-097 for a fixture tune with an entry in the fixture STIL file, the text equals the entry, and its é is that letter [parts: notes]") {
    const spz::StilNotes found = notes(kStil85, "/MUSICIANS/H/Hubbard_Rob/Commando.sid", 1);
    CHECK(found.found);
    CHECK(found.notice == "");
    CHECK(found.text ==
          "  TITLE: Commando Test\n"
          " ARTIST: Test Author\n"
          "COMMENT: A fixture's note, with an \xC3\xA9 in it to show that STIL is Latin-1.");
}

TEST_CASE("SPEC-097 the text is the part for the file, then the part for the playing subtune, and no other subtune's [parts: notes]") {
    const std::string whole_file = "COMMENT: A note for the whole file, with caf\xC3\xA9 in it.";
    CHECK(notes(kStilPlay, "/LENGTHS/Three_Lengths.sid", 1).text ==
          whole_file + "\n  TITLE: First Song\n ARTIST: Somebody");
    CHECK(notes(kStilPlay, "/LENGTHS/Three_Lengths.sid", 2).text ==
          whole_file + "\nCOMMENT: The second subtune's note runs over\n         two lines.");
    CHECK(notes(kStilPlay, "/LENGTHS/Three_Lengths.sid", 3).text == whole_file + "\n  TITLE: Third Song");
}

TEST_CASE("SPEC-097 an entry with only a part for the file gives that part, and one with only a part for the playing subtune gives that [parts: notes]") {
    const spz::StilNotes file_only = notes(kStilPlay, "/QUEUE/A_One.sid", 1);
    CHECK(file_only.found);
    CHECK(file_only.text == "  TITLE: Queue One\nCOMMENT: Only a part for the file.");

    const spz::StilNotes subtune_only = notes(kStilPlay, "/QUEUE/B_Three.sid", 2);
    CHECK(subtune_only.found);
    CHECK(subtune_only.text == "COMMENT: Only a part for subtune 2.");

    // The fixture of release 85 has such an entry too, for its three-subtune tune's default subtune.
    CHECK(notes(kStil85, "/MUSICIANS/T/Tester_Tom/Three_Songs.sid", 2).text ==
          "COMMENT: The second subtune is the one the file names as its default.");
}

TEST_CASE("SPEC-097 with no entry for the tune the panel says: No notes for this tune [parts: notes]") {
    CHECK(std::string(spz::kNoNotes) == "No notes for this tune");
    const spz::StilNotes none = notes(kStilPlay, "/QUEUE/C_Two_Chips.sid", 1);
    CHECK_FALSE(none.found);
    CHECK(none.text == "No notes for this tune");
    CHECK(none.notice == "");
    // A path that is the start of another entry's, or the end of it, is not that entry.
    CHECK_FALSE(notes(kStilPlay, "/QUEUE/A_One", 1).found);
    CHECK_FALSE(notes(kStilPlay, "/A_One.sid", 1).found);
}

TEST_CASE("SPEC-097 with no STIL file the panel says: No notes for this tune [parts: notes]") {
    spz::test::TempFolder folder;
    spz::fake::Files files;
    const spz::StilNotes none = spz::stil_notes(&files, folder.file("DOCUMENTS/STIL.txt"), "/QUEUE/A_One.sid", 1);
    CHECK_FALSE(none.found);
    CHECK(none.text == "No notes for this tune");
    CHECK(none.notice == "");
}

TEST_CASE("SPEC-097 STIL is Latin-1, with HVSC's CRLF line endings: its letters come out as UTF-8, and no carriage return with them [parts: notes]") {
    spz::test::TempFolder folder;
    spz::fake::Files files;
    const std::string ending = "\r\n";
    const std::string stil = "# a comment line" + ending + ending +
                             "/A/First.sid" + ending +
                             "COMMENT: na\xEFve fa\xE7" "ade, \xA9 1988 \xDCml\xE4ut" + ending + ending +
                             "/A/Second.sid" + ending +
                             "  TITLE: The entry after it" + ending;
    const std::string path = folder.write("STIL.txt", stil);
    const spz::StilNotes first = spz::stil_notes(&files, path, "/A/First.sid", 1);
    CHECK(first.found);
    CHECK(first.text == "COMMENT: na\xC3\xAFve fa\xC3\xA7" "ade, \xC2\xA9 1988 \xC3\x9Cml\xC3\xA4ut");
    const spz::StilNotes second = spz::stil_notes(&files, path, "/A/Second.sid", 1);
    CHECK(second.text == "  TITLE: The entry after it");       // the file's last entry, with no empty line after it
}

TEST_CASE("SPEC-097 SPEC-204 a STIL file longer than 16 MiB is not read: no notes, and the notice MSG_FILE_TOO_LARGE [parts: notes]") {
    CHECK(spz::kStilSizeLimit == 16u * 1024u * 1024u);
    spz::test::TempFolder folder;
    spz::fake::Files files;
    const std::string entry = "/A/First.sid\r\nCOMMENT: Here.\r\n\r\n";
    std::string exactly = entry;
    exactly.append(spz::kStilSizeLimit - entry.size(), '#');   // one long comment line fills the rest
    const std::string at_limit = folder.write("at_limit.txt", exactly);
    const std::string over_limit = folder.write("over_limit.txt", exactly + "#");

    const spz::StilNotes read = spz::stil_notes(&files, at_limit, "/A/First.sid", 1);
    CHECK(read.found);
    CHECK(read.text == "COMMENT: Here.");
    CHECK(read.notice == "");
    const spz::StilNotes refused = spz::stil_notes(&files, over_limit, "/A/First.sid", 1);
    CHECK_FALSE(refused.found);
    CHECK(refused.text == "No notes for this tune");
    CHECK(refused.notice == "MSG_FILE_TOO_LARGE");
}
#endif
