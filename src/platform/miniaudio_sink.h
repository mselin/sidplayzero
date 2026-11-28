// SPDX-License-Identifier: GPL-3.0-or-later
// The real AudioSink (SPEC-005, SPEC-021): miniaudio 0.11.25, built into the program, trying PulseAudio
// first and ALSA second, whose libraries it loads when the program runs. The stream is 16-bit stereo at
// the rate asked for; if the system runs at another, the system converts (SPEC-073).
//
// The pull function is called on miniaudio's own thread, the audio thread of SPEC-022, inside the guard
// of platform/audio_guard.h. A device that stops by itself, or is rerouted, while the stream is open is
// reported to the function given to on_device_lost(), from that thread (SPEC-078).
#pragma once

#include <memory>
#include <string>

#include "spz/audio_sink.h"

namespace spz::platform {

class MiniaudioSink : public AudioSink {
public:
    enum class Output {
        System,   // PulseAudio, else ALSA (SPEC-005)
        Null,     // miniaudio's null device: pulled in real time, heard nowhere. For tests and the tool
    };

    explicit MiniaudioSink(Output output = Output::System);
    ~MiniaudioSink() override;
    MiniaudioSink(const MiniaudioSink&) = delete;
    MiniaudioSink& operator=(const MiniaudioSink&) = delete;

    bool open(unsigned sample_rate, Pull pull) override;
    void close() override;
    void on_device_lost(std::function<void()> fn) override;

    // The backend of the stream that is open, such as "PulseAudio" or "ALSA"; "" when none is (SPEC-241).
    std::string backend_name() const;
    // What the backend opened the stream with, as miniaudio reports it once the device is open (SPEC-241):
    // the rate in Hz, and the sample format and channels, such as "s16 stereo". 0 and "" when none is open.
    unsigned opened_rate() const;
    std::string opened_format() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spz::platform
