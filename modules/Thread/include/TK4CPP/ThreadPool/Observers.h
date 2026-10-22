/**
 * @file Observers.h
 * @brief Ready-made observers: MetricsCollector, RetryHandler, FileLogger.
 *
 * All observers subscribe to a pool's EventBus in their constructor and
 * unsubscribe in their destructor. They may be destroyed while the pool is
 * running; destruction waits for handlers in flight on other threads.
 */
#pragma once

#include "Export.h"
#include "PoolLogger.h"
#include "ThreadPool.h"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace TK4CPP::TP {

/** @brief Aggregates task statistics of one pool. */
class TK4CPP_TP_API MetricsCollector {
public:
    /** @brief Snapshot of the collected numbers. */
    struct Metrics {
        std::size_t totalSubmitted = 0;  ///< Tasks accepted into the queue.
        std::size_t totalCompleted = 0;
        std::size_t totalFailed    = 0;
        std::size_t totalCancelled = 0;
        std::size_t totalRejected  = 0;  ///< submit() refused (queue full or pool stopping).
        std::size_t peakWorkers    = 0;  ///< Highest number of simultaneously alive workers.
        std::chrono::microseconds totalExecutionTime{ 0 };  ///< Sum over completed tasks.
        std::chrono::microseconds avgExecutionTime{ 0 };    ///< Mean over completed tasks.
        std::chrono::microseconds maxExecutionTime{ 0 };    ///< Longest completed task.
    };

    explicit MetricsCollector(ThreadPool& pool);
    ~MetricsCollector() = default;

    MetricsCollector(const MetricsCollector&) = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;

    /** @brief Thread-safe copy of the current numbers. */
    [[nodiscard]] Metrics getMetrics() const;

    /** @brief Zeroes the counters (peakWorkers restarts from the current worker count). */
    void reset();

private:
    mutable std::mutex      _mutex;
    Metrics                 _m;
    std::size_t             _aliveWorkers = 0;
    std::vector<Connection> _connections;  // last: disconnected first
};

/**
 * @brief Resubmits failed tasks up to a fixed number of times.
 *
 * A task is identified by its address. Resubmission happens on the worker that
 * observed the failure and uses the pool's normal submit() path, so it is
 * subject to the queue limit; if the pool refuses, the task stays Failed.
 */
class TK4CPP_TP_API RetryHandler {
public:
    /**
     * @param pool Pool to watch and to resubmit into.
     * @param maxRetries Retries per task after the first failure (0 disables retrying).
     */
    explicit RetryHandler(ThreadPool& pool, std::size_t maxRetries = 3);
    ~RetryHandler() = default;

    RetryHandler(const RetryHandler&) = delete;
    RetryHandler& operator=(const RetryHandler&) = delete;

    /** @return Number of resubmissions performed so far. */
    [[nodiscard]] std::size_t retriesPerformed() const;
    /** @return Number of tasks that kept failing after maxRetries retries. */
    [[nodiscard]] std::size_t retriesExhausted() const;

private:
    void onFailed(const TaskFailedEvent& e);
    void onSettled(const TaskEvent& e);

    ThreadPool&                                  _pool;
    const std::size_t                            _maxRetries;
    mutable std::mutex                           _mutex;
    std::unordered_map<const ITask*, std::size_t> _attempts;
    std::size_t                                  _performed = 0;
    std::size_t                                  _exhausted = 0;
    std::vector<Connection>                      _connections;
};

/** @brief Appends timestamped pool events to a text file. */
class TK4CPP_TP_API FileLogger {
public:
    /**
     * @param pool Pool to watch.
     * @param filename File opened in append mode.
     * @param categories Event groups to record.
     * @throws std::runtime_error if the file cannot be opened.
     */
    FileLogger(ThreadPool& pool, const std::string& filename,
               LogCategory categories = LogCategory::Tasks | LogCategory::Pool);
    ~FileLogger();

    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

private:
    std::ofstream _file;    // must outlive _logger
    PoolLogger    _logger;
};

} // namespace TK4CPP::TP
