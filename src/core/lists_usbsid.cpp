// SPDX-License-Identifier: GPL-3.0-or-later
// What `core` knows of a real SID chip on a USBSID-Pico (SPEC-191).
#include "spz/lists_usbsid.h"

namespace spz {

bool output_from_name(const std::string& name, Output* out) {
    if (name == "software") {
        *out = Output::Software;
        return true;
    }
    if (name == "usbsid") {
        *out = Output::Usbsid;
        return true;
    }
    return false;
}

const char* output_name(Output output) { return output == Output::Usbsid ? "usbsid" : "software"; }

BackendAbilities usbsid_abilities() {
    // The chip is driven in step with the real clock: it cannot run ahead, and nothing reads its
    // registers back. Muting voices and switching filters are done on the chip.
    BackendAbilities can;
    can.mute = true;
    can.filter = true;
    can.skip = false;
    can.registers = false;
    return can;
}

const char* device_gone_message(Output output) {
    return output == Output::Usbsid ? "MSG_USBSID_MISSING" : "MSG_NO_SOUND";
}

}  // namespace spz
