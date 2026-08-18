#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>

namespace adayo {

class WorkerQueue {
public:
    using Task = std::function<void()>;

    WorkerQueue();
    ~WorkerQueue();
    WorkerQueue(const WorkerQueue&) = delete;
    WorkerQueue& operator=(const WorkerQueue&) = delete;

    void Submit(Task task);
    void Stop();

private:
    void Run(std::stop_token stop_token);

    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::queue<Task> tasks_;
    std::jthread worker_;
    bool accepting_{true};
};

} // namespace adayo
