// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/miniaudio_sink.h"

#include <atomic>
#include <cstring>
#include <mutex>

#include "miniaudio.h"
#include "platform/audio_guard.h"

namespace spz::platform {

struct MiniaudioSink::Impl {
    Output output;
    ma_context context;
    bool context_ready = false;
    ma_device device;
    bool device_open = false;
    Pull pull;
    std::atomic<bool> closing{false};
    std::mutex lost_mutex;              // guards `lost` against on_device_lost(); never taken by the callback
    std::function<void()> lost;
    std::atomic<bool> lost_reported{false};

    static void data(ma_device* device, void* out, const void*, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(device->pUserData);
        AudioCallbackScope inside;
        self->pull(static_cast<std::int16_t*>(out), frames);
    }

    static void notified(const ma_device_notification* notification) {
        auto* self = static_cast<Impl*>(notification->pDevice->pUserData);
        if (self == nullptr || self->closing.load()) return;
        const bool gone = notification->type == ma_device_notification_type_stopped ||
                          notification->type == ma_device_notification_type_rerouted;
        if (!gone || self->lost_reported.exchange(true)) return;
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lock(self->lost_mutex);
            fn = self->lost;
        }
        if (fn) fn();
    }

    bool start_context() {
        if (context_ready) return true;
        ma_context_config config = ma_context_config_init();
        config.pulse.pApplicationName = "SidPlayZero";
        ma_result result;
        if (output == Output::Null) {
            const ma_backend backends[] = {ma_backend_null};
            result = ma_context_init(backends, 1, &config, &context);
        } else {
            const ma_backend backends[] = {ma_backend_pulseaudio, ma_backend_alsa};
            result = ma_context_init(backends, 2, &config, &context);
        }
        context_ready = result == MA_SUCCESS;
        return context_ready;
    }
};

MiniaudioSink::MiniaudioSink(Output output) : impl_(new Impl) { impl_->output = output; }

MiniaudioSink::~MiniaudioSink() {
    close();
    if (impl_->context_ready) ma_context_uninit(&impl_->context);
}

bool MiniaudioSink::open(unsigned sample_rate, Pull pull) {
    close();
    Impl& d = *impl_;
    if (!d.start_context()) return false;
    d.pull = std::move(pull);
    d.closing.store(false);
    d.lost_reported.store(false);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = sample_rate;
    config.dataCallback = &Impl::data;
    config.notificationCallback = &Impl::notified;
    config.pUserData = &d;
    config.noPreSilencedOutputBuffer = MA_TRUE;   // the pull fills every frame it is asked for
    config.noClip = MA_TRUE;
    if (ma_device_init(&d.context, &config, &d.device) != MA_SUCCESS) return false;
    d.device_open = true;
    if (ma_device_start(&d.device) != MA_SUCCESS) {
        close();
        return false;
    }
    return true;
}

void MiniaudioSink::close() {
    Impl& d = *impl_;
    if (!d.device_open) return;
    d.closing.store(true);
    ma_device_uninit(&d.device);
    d.device_open = false;
}

void MiniaudioSink::on_device_lost(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(impl_->lost_mutex);
    impl_->lost = std::move(fn);
}

std::string MiniaudioSink::backend_name() const {
    if (!impl_->device_open) return "";
    switch (impl_->context.backend) {
        case ma_backend_pulseaudio: return "PulseAudio";
        case ma_backend_alsa: return "ALSA";
        case ma_backend_null: return "null";
        default: return "other";
    }
}

}  // namespace spz::platform
