#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>

namespace adayo {

enum class StopMode {
    Drain,
    DiscardPending,
};

class WorkerQueue {
public:
    using Task = std::function<void(std::stop_token)>;
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    explicit WorkerQueue(ErrorHandler on_unhandled_task_error = {});
    ~WorkerQueue();
    WorkerQueue(const WorkerQueue&) = delete;
    WorkerQueue& operator=(const WorkerQueue&) = delete;

    void Submit(Task task);
    void RequestStop(StopMode mode = StopMode::Drain);
    void Stop(StopMode mode = StopMode::Drain);

private:
    void Run(std::stop_token stop_token);

    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::queue<Task> tasks_;
    std::jthread worker_;
    std::stop_source stop_source_;
    std::mutex join_mutex_;
    ErrorHandler on_unhandled_task_error_;
    bool accepting_{true};
    bool stopped_{false};
};

} // namespace adayo
