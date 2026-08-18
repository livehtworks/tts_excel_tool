#pragma once

#include "adapters/audio/IAudioPlayer.h"
#include "core/worker/WorkerQueue.h"
#include "services/TtsService.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace adayo {

enum class PlaybackState {
    Idle,
    Generating,
    Playing,
    Paused,
    Stopping,
    Error,
};

struct PlaybackItem {
    TtsRequest request;
    std::size_t row_index{};
    std::size_t column_index{};
};

class PlaybackService {
public:
    PlaybackService(TtsService& tts, IAudioPlayer& player);
    ~PlaybackService();

    void PlayOne(TtsRequest request);
    void PlaySequence(std::vector<PlaybackItem> items, std::chrono::milliseconds interval, double speed);
    void Pause();
    void Resume();
    void Stop();

    PlaybackState State() const;
    std::string LastError() const;
    std::size_t CurrentRow() const;
    std::size_t CurrentColumn() const;
    bool WaitUntilIdle(std::chrono::milliseconds timeout) const;

private:
    void SetState(PlaybackState state);
    void SetError(std::string error);
    void RunSequence(std::vector<PlaybackItem> items, std::chrono::milliseconds interval, std::uint64_t generation);
    bool IsCanceled(std::uint64_t generation) const;
    bool WaitWhilePaused(std::uint64_t generation);
    void FinishCanceledIfCurrentStop(std::uint64_t generation);
    bool WaitInterval(std::chrono::milliseconds interval, std::uint64_t generation);

    TtsService& tts_;
    IAudioPlayer& player_;
    WorkerQueue worker_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    PlaybackState state_{PlaybackState::Idle};
    PlaybackState paused_from_{PlaybackState::Idle};
    std::string last_error_;
    std::size_t current_row_{};
    std::size_t current_column_{};
    std::atomic<std::uint64_t> generation_{0};
    bool pause_requested_{false};
};

} // namespace adayo
