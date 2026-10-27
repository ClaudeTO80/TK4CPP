/**
 * @file Executor.h
 * @brief Execution back-end abstraction and a minimal thread pool.
 */
#pragma once

#include <TK4CPP/Scheduler/Export.h>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace TK4CPP::Scheduler {

/**
 * @brief Runs job executions off the dispatcher thread.
 *
 * Implement this to run jobs on an existing thread pool (see ThreadPoolAdapter for TK4CPP::TP).
 * The scheduler calls @ref shutdown exactly once, from JobScheduler::shutdown(), and never
 * submits afterwards. @ref submit is always called from the dispatcher thread or from the thread
 * that called JobScheduler::triggerJobNow()/start(), never while the scheduler lock is held.
 */
class IExecutor {
public:
    virtual ~IExecutor() = default;

    /**
     * @brief Queues a task.
     *
     * Must not block for long: it runs on the dispatcher thread, so blocking delays every trigger.
     *
     * @param task  Work to run exactly once. It never throws.
     * @param label Human readable origin of the task (the job id), for logs and pool events.
     * @return true if the task was accepted and will run (or be reported as discarded by
     *         @ref shutdown); false if it was refused (stopped executor, full queue) and will
     *         never run. The scheduler reports a refusal to its listeners.
     */
    [[nodiscard]] virtual bool submit(std::function<void()> task, std::string_view label) = 0;

    /**
     * @brief Stops the executor and waits for tasks already running.
     * @param drainQueue If true, queued tasks are executed first; if false they are discarded.
     */
    virtual void shutdown(bool drainQueue) = 0;
};

/// @brief Fixed-size FIFO thread pool.
class TK4CPP_SCHEDULER_EXPORT ThreadPoolExecutor final : public IExecutor {
public:
    /// @param threads Number of workers (at least 1).
    explicit ThreadPoolExecutor(std::size_t threads);
    ~ThreadPoolExecutor() override;
    ThreadPoolExecutor(const ThreadPoolExecutor&) = delete;
    ThreadPoolExecutor& operator=(const ThreadPoolExecutor&) = delete;

    /// @copydoc IExecutor::submit (refuses tasks after shutdown; the queue is unbounded)
    [[nodiscard]] bool submit(std::function<void()> task, std::string_view label) override;
    void shutdown(bool drainQueue) override;

private:
    void workerLoop();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stop_{false};
};

} // namespace TK4CPP::Scheduler
