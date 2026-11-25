// SPDX-License-Identifier: GPL-3.0-or-later
// A real SID chip on a USBSID-Pico (SPEC-191), as far as `core` goes. The part `usbsid` is not built unless
// it is asked for, so these tests are compiled only then.
//
// The first clause of SPEC-191's oracle, that the player's own tests pass against a fake backend whose
// can() is the hardware's, is tests/unit/test_lists_usbsid.py: it runs this program's player tests again
// with the environment variable SPZ_FAKE_BACKEND_CAN set to "hardware", which the fake SoundBackend is to
// take its abilities from. The test at the end of this file is that run's proof that the fake did.
#include <cstdlib>
#include <string>

#include "doctest.h"
#include "fakes.h"
#include "spz/lists_usbsid.h"
#include "spz/messages.h"
#include "spz/sound_backend.h"

#if SPZ_PART_USBSID

TEST_CASE("SPEC-191 the hardware backend's can() reports no skip and no registers [parts: usbsid]") {
    const spz::BackendAbilities can = spz::usbsid_abilities();
    CHECK_FALSE(can.skip);
    CHECK_FALSE(can.registers);
}

TEST_CASE("SPEC-191 the setting `output` is software, the default, or usbsid [parts: usbsid]") {
    spz::Output output = spz::Output::Usbsid;
    CHECK(spz::output_from_name("software", &output));
    CHECK(output == spz::Output::Software);
    CHECK(spz::output_from_name("usbsid", &output));
    CHECK(output == spz::Output::Usbsid);
    for (const char* other : {"", "hardware", "USBSID", "exsid", "software "}) {
        CAPTURE(other);
        CHECK_FALSE(spz::output_from_name(other, &output));
    }
    CHECK(std::string(spz::output_name(spz::Output::Software)) == "software");
    CHECK(std::string(spz::output_name(spz::Output::Usbsid)) == "usbsid");
    CHECK(spz::Output{} == spz::Output::Software);             // the default
}

TEST_CASE("SPEC-191 SPEC-078 a backend that reports the device gone gives MSG_USBSID_MISSING with the USBSID-Pico, and MSG_NO_SOUND with the software backend [parts: usbsid]") {
    CHECK(std::string(spz::device_gone_message(spz::Output::Usbsid)) == "MSG_USBSID_MISSING");
    CHECK(std::string(spz::device_gone_message(spz::Output::Software)) == "MSG_NO_SOUND");
    CHECK(spz::is_message(spz::device_gone_message(spz::Output::Usbsid)));
    CHECK(spz::is_message(spz::device_gone_message(spz::Output::Software)));
}

TEST_CASE("SPEC-191 in a run that asks for the hardware's can(), the fake backend reports it [parts: usbsid]") {
    const char* asked = std::getenv("SPZ_FAKE_BACKEND_CAN");
    if (asked == nullptr || std::string(asked) != "hardware") return;   // an ordinary run: nothing to show
    spz::fake::SoundBackend backend;
    const spz::BackendAbilities hardware = spz::usbsid_abilities();
    CHECK(backend.can().skip == hardware.skip);
    CHECK(backend.can().registers == hardware.registers);
    CHECK(backend.can().mute == hardware.mute);
    CHECK(backend.can().filter == hardware.filter);
    CHECK_FALSE(backend.can().skip);
    CHECK_FALSE(backend.can().registers);
}

#endif  // SPZ_PART_USBSID
