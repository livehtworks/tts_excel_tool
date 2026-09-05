#include "adapters/audio/MiniaudioPlayer.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <vector>

#ifdef ADAYO_HAS_MINIAUDIO
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#endif

namespace adayo {

struct MiniaudioPlayer::Impl {
#ifdef ADAYO_HAS_MINIAUDIO
    ma_device device{};
    bool device_initialized{false};
    std::int32_t device_sample_rate{};
    std::int32_t device_channels{};
    std::mutex device_mutex;
#endif
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<float> samples;
    std::size_t cursor{0};
    std::int32_t channels{1};
    bool playing{false};
    bool paused{false};
    bool stop_requested{false};
};

#ifdef ADAYO_HAS_MINIAUDIO
namespace {
void DataCallback(ma_device* device, void* output, const void*, ma_uint32 frame_count) {
    auto* impl = static_cast<MiniaudioPlayer::Impl*>(device->pUserData);
    auto* out = static_cast<float*>(output);
    const auto channels = std::max<std::int32_t>(1, impl->channels);
    const auto requested = static_cast<std::size_t>(frame_count) * static_cast<std::size_t>(channels);

    std::unique_lock lock(impl->mutex);
    std::fill(out, out + requested, 0.0f);
    if (impl->paused || impl->stop_requested || !impl->playing) {
        return;
    }

    const auto remaining = impl->samples.size() - impl->cursor;
    const auto to_copy = (std::min)(requested, remaining);
    std::copy_n(impl->samples.data() + impl->cursor, to_copy, out);
    impl->cursor += to_copy;
    if (impl->cursor >= impl->samples.size()) {
        impl->playing = false;
        lock.unlock();
        impl->cv.notify_all();
    }
}

void UninitDevice(MiniaudioPlayer::Impl& impl) {
    if (!impl.device_initialized) return;
    ma_device_uninit(&impl.device);
    impl.device_initialized = false;
    impl.device_sample_rate = 0;
    impl.device_channels = 0;
}

void EnsureDevice(MiniaudioPlayer::Impl& impl, std::int32_t sample_rate, std::int32_t channels) {
    if (impl.device_initialized &&
        impl.device_sample_rate == sample_rate &&
        impl.device_channels == channels) {
        return;
    }
    UninitDevice(impl);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = static_cast<ma_uint32>(channels);
    config.sampleRate = static_cast<ma_uint32>(sample_rate);
    config.dataCallback = DataCallback;
    config.pUserData = &impl;

    const auto init_result = ma_device_init(nullptr, &config, &impl.device);
    if (init_result != MA_SUCCESS) {
        throw std::runtime_error("miniaudio device 初始化失败");
    }
    impl.device_initialized = true;
    impl.device_sample_rate = sample_rate;
    impl.device_channels = channels;
}
} // namespace
#endif

MiniaudioPlayer::MiniaudioPlayer() : impl_(std::make_unique<Impl>()) {}
MiniaudioPlayer::~MiniaudioPlayer() {
    Stop();
#ifdef ADAYO_HAS_MINIAUDIO
    std::lock_guard device_lock(impl_->device_mutex);
    if (impl_->device_initialized) {
        UninitDevice(*impl_);
    }
#endif
}

void MiniaudioPlayer::Play(const AudioBuffer& audio) {
#ifdef ADAYO_HAS_MINIAUDIO
    if (audio.samples.empty()) {
        throw std::invalid_argument("AudioBuffer samples 不能为空");
    }
    if (audio.sample_rate <= 0 || audio.channels <= 0) {
        throw std::invalid_argument("AudioBuffer sample_rate/channels 非法");
    }

    Stop();
    {
        std::lock_guard device_lock(impl_->device_mutex);
        EnsureDevice(*impl_, audio.sample_rate, audio.channels);
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->samples = audio.samples;
        impl_->cursor = 0;
        impl_->channels = audio.channels;
        impl_->playing = true;
        impl_->paused = false;
        impl_->stop_requested = false;
    }

    {
        std::lock_guard device_lock(impl_->device_mutex);
        const auto start_result = ma_device_start(&impl_->device);
        if (start_result != MA_SUCCESS) {
            std::lock_guard lock(impl_->mutex);
            impl_->playing = false;
            impl_->stop_requested = true;
            throw std::runtime_error("miniaudio device 启动失败");
        }
    }

    std::unique_lock lock(impl_->mutex);
    impl_->cv.wait(lock, [&] { return !impl_->playing || impl_->stop_requested; });
    lock.unlock();
    {
        std::lock_guard device_lock(impl_->device_mutex);
        if (impl_->device_initialized) {
            ma_device_stop(&impl_->device);
        }
    }
#else
    (void)audio;
    throw std::runtime_error("当前构建未启用 miniaudio");
#endif
}

void MiniaudioPlayer::Pause() {
    std::lock_guard lock(impl_->mutex);
    if (impl_->playing) impl_->paused = true;
}

void MiniaudioPlayer::Resume() {
    std::lock_guard lock(impl_->mutex);
    impl_->paused = false;
}

void MiniaudioPlayer::Stop() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop_requested = true;
        impl_->playing = false;
        impl_->paused = false;
    }
    impl_->cv.notify_all();
#ifdef ADAYO_HAS_MINIAUDIO
    std::lock_guard device_lock(impl_->device_mutex);
    if (impl_->device_initialized) {
        ma_device_stop(&impl_->device);
    }
#endif
}
} // namespace adayo
