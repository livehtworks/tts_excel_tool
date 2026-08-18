#include "core/worker/WorkerQueue.h"

#include <stdexcept>

namespace adayo {

WorkerQueue::WorkerQueue() : worker_([this](std::stop_token token) { Run(token); }) {}
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

void WorkerQueue::Stop() {
    {
        std::lock_guard lock(mutex_);
        accepting_ = false;
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
        task();
    }
}

} // namespace adayo
