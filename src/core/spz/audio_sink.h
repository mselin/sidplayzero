// SPDX-License-Identifier: GPL-3.0-or-later
// Seam: the sound device (SPEC-021). The real one is miniaudio, in `platform`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace spz {

class AudioSink {
public:
    // Fills `frames` stereo frames. Called on the audio thread: it must not lock, allocate or wait.
    using Pull = std::function<void(std::int16_t* out, std::size_t frames)>;

    virtual ~AudioSink() = default;

    // Opens a stream of 16-bit stereo at `sample_rate` and starts pulling. False if no device opens.
    virtual bool open(unsigned sample_rate, Pull pull) = 0;
    virtual void close() = 0;
    // `fn` is called, on any thread, when the device is lost or changed while the stream is open.
    virtual void on_device_lost(std::function<void()> fn) = 0;
};

}  // namespace spz
