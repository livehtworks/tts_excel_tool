#include "services/PlaybackService.h"
#include <algorithm>
#include <utility>

namespace adayo {
PlaybackService::PlaybackService(TtsService& tts, IAudioPlayer& player, ErrorHandler on_error, TimingHandler on_timing)
    : tts_(tts), player_(player), on_error_(std::move(on_error)), on_timing_(std::move(on_timing)) {}
PlaybackService::~PlaybackService() { Shutdown(); }

void PlaybackService::Shutdown() {
    { std::lock_guard lock(mutex_); shutdown_ = true; }
    Stop();
    worker_.Stop(StopMode::DiscardPending);
    { std::lock_guard lock(mutex_); active_request_.reset(); state_ = PlaybackState::Idle; }
    cv_.notify_all();
}

void PlaybackService::Play(PlaybackRequest request) {
    // Control lock precedes request lock. Neither spans synthesis or blocking Play.
    std::lock_guard lock(mutex_);
    if (shutdown_) return;
    if (active_request_) {
        std::lock_guard request_lock(active_request_->mutex);
        active_request_->canceled = true;
        active_request_->cancellation.request_stop();
        active_request_->cv.notify_all();
    }
    player_.Stop();
    auto context = std::make_shared<AudioPlaybackContext>();
    context->request_id = ++generation_;
    active_request_ = context;
    state_ = PlaybackState::Generating;
    last_error_.clear();
    for (auto& item : request.items) item.request.speed = request.speed;
    worker_.Submit([this, request = std::move(request), context](std::stop_token) {
        RunSequence(std::move(request), context);
    });
    cv_.notify_all();
}

void PlaybackService::Pause() {
    std::lock_guard lock(mutex_);
    if (!active_request_ || (state_ != PlaybackState::Playing && state_ != PlaybackState::Generating)) return;
    { std::lock_guard request_lock(active_request_->mutex); active_request_->paused = true; }
    paused_from_ = state_;
    state_ = PlaybackState::Paused;
    player_.Pause();
    active_request_->cv.notify_all();
    cv_.notify_all();
}
void PlaybackService::Resume() {
    std::lock_guard lock(mutex_);
    if (!active_request_ || state_ != PlaybackState::Paused) return;
    { std::lock_guard request_lock(active_request_->mutex); active_request_->paused = false; }
    state_ = paused_from_;
    player_.Resume();
    active_request_->cv.notify_all();
    cv_.notify_all();
}
void PlaybackService::Stop() {
    std::lock_guard lock(mutex_);
    if (active_request_) {
        { std::lock_guard request_lock(active_request_->mutex);
          active_request_->canceled = true;
          active_request_->paused = false;
          active_request_->cancellation.request_stop(); }
        state_ = PlaybackState::Stopping;
        active_request_->cv.notify_all();
    } else { state_ = PlaybackState::Idle; }
    player_.Stop();
    cv_.notify_all();
}
PlaybackState PlaybackService::State() const { std::lock_guard lock(mutex_); return state_; }
std::string PlaybackService::LastError() const { std::lock_guard lock(mutex_); return last_error_; }
std::size_t PlaybackService::CurrentRow() const { std::lock_guard lock(mutex_); return current_row_; }
std::size_t PlaybackService::CurrentColumn() const { std::lock_guard lock(mutex_); return current_column_; }
bool PlaybackService::WaitUntilIdle(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return state_ == PlaybackState::Idle || state_ == PlaybackState::Error; });
}
bool PlaybackService::WaitReady(const std::shared_ptr<AudioPlaybackContext>& context) {
    std::unique_lock lock(context->mutex);
    context->cv.wait(lock, [&] { return context->canceled || !context->paused; });
    return !context->canceled;
}
bool PlaybackService::SetRequestState(const std::shared_ptr<AudioPlaybackContext>& context, PlaybackState state) {
    std::lock_guard lock(mutex_);
    if (active_request_ != context) return false;
    std::lock_guard request_lock(context->mutex);
    if (context->canceled) return false;
    paused_from_ = state;
    state_ = context->paused ? PlaybackState::Paused : state;
    cv_.notify_all();
    return true;
}
void PlaybackService::Finish(const std::shared_ptr<AudioPlaybackContext>& context, std::string error) {
    bool report = false;
    {
        std::lock_guard lock(mutex_);
        if (active_request_ != context) return;
        std::lock_guard request_lock(context->mutex);
        report = !context->canceled && !error.empty();
        state_ = report ? PlaybackState::Error : PlaybackState::Idle;
        last_error_ = report ? error : std::string{};
        active_request_.reset();
    }
    cv_.notify_all();
    if (report && on_error_) on_error_(error);
}
bool PlaybackService::WaitInterval(std::chrono::milliseconds interval, const std::shared_ptr<AudioPlaybackContext>& context) {
    auto remaining = std::chrono::steady_clock::duration(interval);
    std::unique_lock lock(context->mutex);
    while (remaining > std::chrono::steady_clock::duration::zero()) {
        context->cv.wait(lock, [&] { return context->canceled || !context->paused; });
        if (context->canceled) return false;
        const auto start = std::chrono::steady_clock::now();
        context->cv.wait_for(lock, remaining, [&] { return context->canceled || context->paused; });
        remaining -= (std::min)(remaining, std::chrono::steady_clock::now() - start);
        if (context->canceled) return false;
    }
    return !context->canceled;
}
void PlaybackService::RunSequence(PlaybackRequest request, const std::shared_ptr<AudioPlaybackContext>& context) {
    try {
        if (!WaitReady(context)) { Finish(context); return; }
        for (std::size_t i = 0; i < request.items.size(); ++i) {
            if (!WaitReady(context) || !SetRequestState(context, PlaybackState::Generating)) break;
            const auto& item = request.items[i];
            {
                std::lock_guard lock(mutex_);
                if (active_request_ != context) break;
                current_row_ = item.row_index; current_column_ = item.column_index;
            }
            if (!item.request.text.empty()) {
                {
                    std::lock_guard request_lock(context->mutex);
                    context->item_started=std::chrono::steady_clock::now();
                    context->first_nonzero_ms.reset(); context->playback_done_ms.reset(); context->device_init_ms=0;
                }
                auto prepared = tts_.Prepare(request.model_id, request.model_config, item.request, context->cancellation.get_token());
                if (!WaitReady(context) || !SetRequestState(context, PlaybackState::Playing)) break;
                player_.Play(*prepared.audio, context);
                if(on_timing_) {
                    std::optional<double> first,done;
                    double device_ms; bool canceled;
                    {
                        std::lock_guard request_lock(context->mutex);
                        first=context->first_nonzero_ms; done=context->playback_done_ms;
                        device_ms=context->device_init_ms; canceled=context->canceled;
                    }
                    on_timing_(context->request_id,i,prepared.timings,device_ms,first,done,canceled);
                }
            }
            if (i + 1 < request.items.size() && !WaitInterval(request.interval, context)) break;
        }
        Finish(context);
    } catch (const std::exception& ex) { Finish(context, ex.what()); }
      catch (...) { Finish(context, "Unknown playback error"); }
}
} // namespace adayo
