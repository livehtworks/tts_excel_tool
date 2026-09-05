#pragma once

#include "core/domain/Types.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <stop_token>

namespace adayo {
struct AudioPlaybackContext {
    std::uint64_t request_id{};
    std::mutex mutex;
    std::condition_variable cv;
    bool canceled{false};
    bool paused{false};
    std::stop_source cancellation;
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
