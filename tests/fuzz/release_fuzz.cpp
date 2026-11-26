// SPDX-License-Identifier: GPL-3.0-or-later
// test: SPEC-205 fuzz: the release reader of SPEC-148, read_release(), on any hv_sids.txt at all
//
// hv_sids.txt comes from the network (SPEC-140, SPEC-148). Whatever its bytes, reading it must end, must
// not read outside them, and must give 0 or a release number.
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "spz/hvsc_setup.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const spz::Bytes text(data, data + size);
    const int release = spz::read_release(text);
    if (release < 0) std::abort();
    return 0;
}
