#pragma once

#include "adapters/audio/IAudioPlayer.h"
#include "core/worker/WorkerQueue.h"
#include "services/TtsService.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
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

struct PlaybackRequest {
    std::string model_id;
    TtsModelConfig model_config;
    std::vector<PlaybackItem> items;
    std::chrono::milliseconds interval{};
    double speed{1.0};
};

class PlaybackService {
public:
    using ErrorHandler = std::function<void(const std::string&)>;

    PlaybackService(TtsService& tts, IAudioPlayer& player, ErrorHandler on_error = {});
    ~PlaybackService();

    void Play(PlaybackRequest request);
    void Pause();
    void Resume();
    void Stop();
    void Shutdown();

    PlaybackState State() const;
    std::string LastError() const;
    std::size_t CurrentRow() const;
    std::size_t CurrentColumn() const;
    bool WaitUntilIdle(std::chrono::milliseconds timeout) const;

private:
    void SetState(PlaybackState state);
    void SetError(std::string error);
    void RunSequence(PlaybackRequest request, std::uint64_t generation);
    bool IsCanceled(std::uint64_t generation) const;
    bool WaitWhilePaused(std::uint64_t generation);
    void FinishCanceledIfCurrentStop(std::uint64_t generation);
    bool WaitInterval(std::chrono::milliseconds interval, std::uint64_t generation);

    TtsService& tts_;
    IAudioPlayer& player_;
    ErrorHandler on_error_;
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
