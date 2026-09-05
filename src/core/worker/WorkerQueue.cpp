#include "core/worker/WorkerQueue.h"

#include <stdexcept>
#include <utility>

namespace adayo {

WorkerQueue::WorkerQueue(ErrorHandler on_unhandled_task_error)
    : on_unhandled_task_error_(std::move(on_unhandled_task_error)) {
    worker_ = std::jthread([this](std::stop_token token) { Run(token); });
}
WorkerQueue::~WorkerQueue() { Stop(); }

void WorkerQueue::Submit(Task task) {
    if (!task) return;
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) throw std::runtime_error("WorkerQueue 已停止");
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void WorkerQueue::Stop(StopMode mode) {
    {
        std::lock_guard lock(mutex_);
        if (stopped_) return;
        stopped_ = true;
        accepting_ = false;
        if (mode == StopMode::DiscardPending) {
            std::queue<Task> empty;
            tasks_.swap(empty);
        }
    }
    worker_.request_stop();
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void WorkerQueue::Run(std::stop_token stop_token) {
    while (true) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, stop_token, [this] { return !tasks_.empty() || !accepting_; });
            if ((stop_token.stop_requested() || !accepting_) && tasks_.empty()) return;
            if (tasks_.empty()) continue;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try {
            task(stop_token);
        } catch (...) {
            if (on_unhandled_task_error_) {
                on_unhandled_task_error_(std::current_exception());
            }
        }
    }
}

} // namespace adayo
