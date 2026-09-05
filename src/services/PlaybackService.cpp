#include "services/PlaybackService.h"

#include <algorithm>
#include <utility>

namespace adayo {

PlaybackService::PlaybackService(TtsService& tts, IAudioPlayer& player, ErrorHandler on_error)
    : tts_(tts), player_(player), on_error_(std::move(on_error)) {}

PlaybackService::~PlaybackService() {
    Shutdown();
}

void PlaybackService::Shutdown() {
    Stop();
    worker_.Stop(StopMode::DiscardPending);
}

void PlaybackService::Play(PlaybackRequest request) {
    Stop();
    const auto generation = generation_.fetch_add(1) + 1;
    for (auto& item : request.items) {
        item.request.speed = request.speed;
    }
    SetState(PlaybackState::Generating);
    worker_.Submit([this, request = std::move(request), generation](std::stop_token) {
        RunSequence(std::move(request), generation);
    });
}

void PlaybackService::Pause() {
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Playing || state_ == PlaybackState::Generating) {
        pause_requested_ = true;
        paused_from_ = state_;
        state_ = PlaybackState::Paused;
        if (paused_from_ == PlaybackState::Playing) {
            player_.Pause();
        }
    }
    cv_.notify_all();
}

void PlaybackService::Resume() {
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Paused) {
        pause_requested_ = false;
        const auto resume_to = paused_from_ == PlaybackState::Generating ? PlaybackState::Generating : PlaybackState::Playing;
        state_ = resume_to;
        paused_from_ = PlaybackState::Idle;
        if (resume_to == PlaybackState::Playing) {
            player_.Resume();
        }
    }
    cv_.notify_all();
}

void PlaybackService::Stop() {
    generation_.fetch_add(1);
    {
        std::lock_guard lock(mutex_);
        pause_requested_ = false;
        paused_from_ = PlaybackState::Idle;
        if (state_ != PlaybackState::Idle) {
            state_ = PlaybackState::Stopping;
        }
    }
    player_.Stop();
    cv_.notify_all();
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
        if (state != PlaybackState::Paused) {
            pause_requested_ = false;
            paused_from_ = PlaybackState::Idle;
        }
        if (state != PlaybackState::Error) {
            last_error_.clear();
        }
    }
    cv_.notify_all();
}

void PlaybackService::SetError(std::string error) {
    const std::string copy = error;
    {
        std::lock_guard lock(mutex_);
        state_ = PlaybackState::Error;
        last_error_ = std::move(error);
    }
    if (on_error_) on_error_(copy);
    cv_.notify_all();
}

void PlaybackService::RunSequence(PlaybackRequest request, std::uint64_t generation) {
    try {
        if (IsCanceled(generation)) {
            FinishCanceledIfCurrentStop(generation);
            return;
        }
        tts_.EnsureModelLoaded(request.model_id, request.model_config);
        if (IsCanceled(generation)) {
            FinishCanceledIfCurrentStop(generation);
            return;
        }
        if (!WaitWhilePaused(generation)) return;
        for (std::size_t i = 0; i < request.items.size(); ++i) {
            const auto& item = request.items[i];
            {
                std::lock_guard lock(mutex_);
                current_row_ = item.row_index;
                current_column_ = item.column_index;
                state_ = PlaybackState::Generating;
            }
            cv_.notify_all();
            if (IsCanceled(generation)) {
                FinishCanceledIfCurrentStop(generation);
                return;
            }
            if (!WaitWhilePaused(generation)) return;
            if (item.request.text.empty()) {
                if (i + 1 < request.items.size() && !WaitInterval(request.interval, generation)) return;
                continue;
            }

            auto audio = tts_.Synthesize(item.request);
            if (IsCanceled(generation)) {
                FinishCanceledIfCurrentStop(generation);
                return;
            }
            if (!WaitWhilePaused(generation)) return;
            SetState(PlaybackState::Playing);
            player_.Play(audio);
            if (IsCanceled(generation)) {
                FinishCanceledIfCurrentStop(generation);
                return;
            }
            if (i + 1 < request.items.size() && request.interval.count() > 0) {
                if (!WaitInterval(request.interval, generation)) return;
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

bool PlaybackService::IsCanceled(std::uint64_t generation) const {
    return generation_.load() != generation;
}

bool PlaybackService::WaitWhilePaused(std::uint64_t generation) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] {
        return generation_.load() != generation || !pause_requested_;
    });
    if (generation_.load() != generation) {
        const bool should_finish = state_ == PlaybackState::Stopping && generation_.load() == generation + 1;
        if (should_finish) {
            state_ = PlaybackState::Idle;
        }
        lock.unlock();
        cv_.notify_all();
        return false;
    }
    return true;
}

void PlaybackService::FinishCanceledIfCurrentStop(std::uint64_t generation) {
    {
        std::lock_guard lock(mutex_);
        if (state_ == PlaybackState::Stopping && generation_.load() == generation + 1) {
            state_ = PlaybackState::Idle;
            pause_requested_ = false;
            paused_from_ = PlaybackState::Idle;
        }
    }
    cv_.notify_all();
}

bool PlaybackService::WaitInterval(std::chrono::milliseconds interval, std::uint64_t generation) {
    if (interval.count() <= 0) return !IsCanceled(generation);
    auto remaining = interval;
    while (remaining.count() > 0) {
        const auto start = std::chrono::steady_clock::now();
        std::unique_lock lock(mutex_);
        const auto woke = cv_.wait_for(lock, remaining, [&] {
            return generation_.load() != generation || pause_requested_;
        });
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        if (!woke) return true;
        if (generation_.load() != generation) {
            const bool should_finish = state_ == PlaybackState::Stopping && generation_.load() == generation + 1;
            if (should_finish) {
                state_ = PlaybackState::Idle;
            }
            lock.unlock();
            cv_.notify_all();
            return false;
        }
        lock.unlock();
        remaining -= (std::min)(remaining, elapsed);
        if (!WaitWhilePaused(generation)) return false;
    }
    return true;
}

} // namespace adayo
