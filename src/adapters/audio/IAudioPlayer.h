#pragma once

#include "core/domain/Types.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <stop_token>
#include <chrono>

namespace adayo {
struct AudioPlaybackContext {
    std::uint64_t request_id{};
    std::mutex mutex;
    std::condition_variable cv;
    bool canceled{false};
    bool paused{false};
    std::stop_source cancellation;
    std::chrono::steady_clock::time_point item_started{std::chrono::steady_clock::now()};
    double device_init_ms{};
    std::optional<double> first_nonzero_ms, playback_done_ms;
};

class IAudioPlayer {
public:
    virtual ~IAudioPlayer() = default;
    virtual void Play(const AudioBuffer& audio) = 0;
    virtual void Play(const AudioBuffer& audio, const std::shared_ptr<AudioPlaybackContext>& context) {
        std::unique_lock lock(context->mutex);
        context->cv.wait(lock, [&] { return context->canceled || !context->paused; });
        if (context->canceled) return;
        lock.unlock();
        Play(audio);
    }
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void Stop() = 0;
};
} // namespace adayo
