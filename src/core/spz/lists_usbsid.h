// SPDX-License-Identifier: GPL-3.0-or-later
// What `core` knows of a real SID chip on a USBSID-Pico (SPEC-191, part `usbsid`, which is not built
// unless asked for). The backend that drives the device is in `platform`, because it is made of the
// engine library; the player meets it only as a SoundBackend (SPEC-025). Here is the little that both
// must agree on.
#pragma once

#include <string>

#include "spz/sound_backend.h"

namespace spz {

// The setting `output` (SPEC-191): where the sound is made. Software is the default.
enum class Output { Software, Usbsid };

// "software" and "usbsid", as settings.ini has them. False for any other word.
bool output_from_name(const std::string& name, Output* out);
const char* output_name(Output output);

// What the hardware backend's can() reports (SPEC-191): no skip and no registers.
BackendAbilities usbsid_abilities();

// The message when the backend reports `device_gone`, on which the player pauses (SPEC-060): the notice
// MSG_NO_SOUND with the software backend (SPEC-078), and MSG_USBSID_MISSING with the USBSID-Pico. It is
// also what a load fails with when the device is not there.
const char* device_gone_message(Output output);

}  // namespace spz
