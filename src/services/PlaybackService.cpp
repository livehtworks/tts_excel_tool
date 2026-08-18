#include "services/PlaybackService.h"

#include <thread>

namespace adayo {

PlaybackService::PlaybackService(TtsService& tts, IAudioPlayer& player)
    : tts_(tts), player_(player) {}

PlaybackService::~PlaybackService() {
    Stop();
}

void PlaybackService::PlayOne(TtsRequest request) {
    PlaySequence({PlaybackItem{std::move(request), 0, 0}}, std::chrono::milliseconds{0}, request.speed);
}

void PlaybackService::PlaySequence(std::vector<PlaybackItem> items, std::chrono::milliseconds interval, double speed) {
    Stop();
    const auto generation = generation_.fetch_add(1) + 1;
    for (auto& item : items) {
        item.request.speed = speed;
    }
    SetState(PlaybackState::Generating);
    worker_.Submit([this, items = std::move(items), interval, generation] {
        RunSequence(std::move(items), interval, generation);
    });
}

void PlaybackService::Pause() {
    player_.Pause();
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Playing || state_ == PlaybackState::Generating) {
        state_ = PlaybackState::Paused;
    }
    cv_.notify_all();
}

void PlaybackService::Resume() {
    player_.Resume();
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Paused) {
        state_ = PlaybackState::Playing;
    }
    cv_.notify_all();
}

void PlaybackService::Stop() {
    generation_.fetch_add(1);
    {
        std::lock_guard lock(mutex_);
        if (state_ != PlaybackState::Idle) {
            state_ = PlaybackState::Stopping;
        }
    }
    player_.Stop();
    SetState(PlaybackState::Idle);
}

PlaybackState PlaybackService::State() const {
    std::lock_guard lock(mutex_);
    return state_;
}

std::string PlaybackService::LastError() const {
    std::lock_guard lock(mutex_);
    return last_error_;
}

std::size_t PlaybackService::CurrentRow() const {
    std::lock_guard lock(mutex_);
    return current_row_;
}

std::size_t PlaybackService::CurrentColumn() const {
    std::lock_guard lock(mutex_);
    return current_column_;
}

bool PlaybackService::WaitUntilIdle(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] {
        return state_ == PlaybackState::Idle || state_ == PlaybackState::Error;
    });
}

void PlaybackService::SetState(PlaybackState state) {
    {
        std::lock_guard lock(mutex_);
        state_ = state;
        if (state != PlaybackState::Error) {
            last_error_.clear();
        }
    }
    cv_.notify_all();
}

void PlaybackService::SetError(std::string error) {
    {
        std::lock_guard lock(mutex_);
        state_ = PlaybackState::Error;
        last_error_ = std::move(error);
    }
    cv_.notify_all();
}

void PlaybackService::RunSequence(std::vector<PlaybackItem> items, std::chrono::milliseconds interval, std::uint64_t generation) {
    try {
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (generation_.load() != generation) return;
            const auto& item = items[i];
            if (item.request.text.empty()) {
                continue;
            }
            {
                std::lock_guard lock(mutex_);
                current_row_ = item.row_index;
                current_column_ = item.column_index;
                state_ = PlaybackState::Generating;
            }
            cv_.notify_all();

            auto audio = tts_.Synthesize(item.request);
            if (generation_.load() != generation) return;
            SetState(PlaybackState::Playing);
            player_.Play(audio);
            if (generation_.load() != generation) return;
            if (i + 1 < items.size() && interval.count() > 0) {
                std::this_thread::sleep_for(interval);
            }
        }
        if (generation_.load() == generation) {
            SetState(PlaybackState::Idle);
        }
    } catch (const std::exception& ex) {
        if (generation_.load() == generation) {
            SetError(ex.what());
        }
    } catch (...) {
        if (generation_.load() == generation) {
            SetError("未知播放错误");
        }
    }
}

} // namespace adayo
