// SPDX-License-Identifier: GPL-3.0-or-later
// test: TF-060 canary: a reader that reads past its input when the input starts with SPZ! [canary-fails]
//
// A fuzz target with a deliberate fault. `tools/test prove fuzz` builds it and expects the fuzz command to
// stop on it; nothing else builds it.
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size >= 4 && data[0] == 'S' && data[1] == 'P' && data[2] == 'Z' && data[3] == '!') {
        volatile std::size_t beyond = size;
        volatile std::uint8_t value = data[beyond];   // one byte past the end
        (void)value;
    }
    return 0;
}
