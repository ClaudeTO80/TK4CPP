#include <TK4CPP/Scheduler/Executor.h>

namespace TK4CPP::Scheduler {

ThreadPoolExecutor::ThreadPoolExecutor(std::size_t threads) {
    if (threads == 0) {
        threads = 1;
    }
    workers_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ThreadPoolExecutor::~ThreadPoolExecutor() {
    shutdown(true);
}

bool ThreadPoolExecutor::submit(std::function<void()> task, std::string_view label) {
    (void)label;
    {
        std::lock_guard lock(mutex_);
        if (stop_) {
            return false;
        }
        queue_.push_back(std::move(task));
    }
    cv_.notify_one();
    return true;
}

void ThreadPoolExecutor::shutdown(bool drainQueue) {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        if (!drainQueue) {
            queue_.clear();
        }
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPoolExecutor::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) {
                return; // stop_ is set and nothing is left to drain
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        try {
            task();
        } catch (...) {
            // Tasks are expected to handle their own errors; never kill the worker.
        }
    }
}

} // namespace TK4CPP::Scheduler
