// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/software_sound.h"

#include <chrono>

#include "spz/software_backend.h"

namespace spz::platform {

struct SoftwareSound::Impl {
    SoftwareBackend backend;
    Impl(Engine* engine, AudioSink* sink, Clock* clock, Executor* ui) : backend(engine, sink, clock, ui) {}
};

SoftwareSound::SoftwareSound(Engine* engine, AudioSink* sink, Clock* clock, Executor* ui)
    : impl_(new Impl(engine, sink, clock, ui)) {}

SoftwareSound::~SoftwareSound() { stop_render_thread(); }

SoundBackend* SoftwareSound::backend() { return &impl_->backend; }

bool SoftwareSound::render_step() { return timed_step(); }

bool SoftwareSound::timed_step() {
    const auto began = std::chrono::steady_clock::now();
    const bool rendered = impl_->backend.render_step();
    if (rendered) {
        render_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began).count();
    }
    return rendered;
}

std::size_t SoftwareSound::buffered() const { return impl_->backend.buffered(); }

void SoftwareSound::start_render_thread() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] {
        while (running_.load()) {
            if (!timed_step()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

void SoftwareSound::stop_render_thread() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

}  // namespace spz::platform
