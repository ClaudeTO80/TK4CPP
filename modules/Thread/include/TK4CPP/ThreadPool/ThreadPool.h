/**
 * @file ThreadPool.h
 * @brief Dynamic, prioritised thread pool with event notifications.
 */
#pragma once

#include "Events.h"
#include "Export.h"
#include "Task.h"
#include "TaskQueue.h"
#include "ThreadPoolConfig.h"
#include "Types.h"
#include "WorkerThread.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace TK4CPP::TP {

/** @brief Point-in-time counters of a pool (lock-free snapshot, values may be slightly inconsistent). */
struct PoolStats {
    std::size_t activeWorkers;   ///< Workers currently running a task.
    std::size_t idleWorkers;     ///< Workers waiting for a task.
    std::size_t queuedTasks;     ///< Tasks waiting in the queue.
    std::size_t totalCompleted;  ///< Tasks that finished without throwing (cumulative).
    std::size_t totalFailed;     ///< Tasks that threw (cumulative).
    std::size_t totalCancelled;  ///< Tasks cancelled before running (cumulative).
    std::size_t totalRejected;   ///< submit() calls refused because the queue was full (cumulative).
};

/** @brief Result of a submit operation. */
using SubmitResult = std::expected<void, SubmitError>;

/**
 * @brief Thread pool with priorities, dynamic scaling and an event bus.
 *
 * - Scale up: when a task is queued and no worker is idle, a worker is added (up to maxThreads).
 * - Scale down: a worker above minThreads retires after idleTimeout.
 * - Shutdown: graceful (drain the queue) or immediate (cancel queued tasks).
 *
 * All member functions are thread-safe unless stated otherwise.
 *
 * @code
 * ThreadPool pool(ThreadPoolConfig::cpuBound());
 * pool.start();
 * auto fut = pool.submitWithFuture([] { return 42; });
 * if (fut) std::cout << fut->get() << '\n';
 * pool.shutdown();
 * @endcode
 */
class TK4CPP_TP_API ThreadPool {
public:
    explicit ThreadPool(ThreadPoolConfig config = ThreadPoolConfig::defaults());

    /** @brief Performs a graceful shutdown if the pool is still running. */
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // ── Submission ─────────────────────────────────────────────────────────

    /**
     * @brief Queues an already built task.
     * @return Nothing on success, otherwise the reason of the refusal.
     */
    [[nodiscard]] SubmitResult submit(TaskPtr task);

    /** @brief Wraps @p fn in a FunctionTask and queues it. */
    [[nodiscard]] SubmitResult submit(FunctionTask::Func fn,
                                      std::string name = "task",
                                      Priority priority = Priority::Normal);

    /**
     * @brief Queues a callable and returns a future for its result.
     *
     * Exceptions thrown by @p fn are delivered through the future (the task
     * itself then counts as Completed). If the task is discarded by
     * shutdownNow() the future throws std::future_error(broken_promise).
     *
     * @return The future, or the reason why the task was refused.
     */
    template<class F>
    [[nodiscard]] auto submitWithFuture(F&& fn, std::string name = "task",
                                        Priority priority = Priority::Normal)
        -> std::expected<std::future<std::invoke_result_t<std::decay_t<F>&>>, SubmitError>
    {
        using R = std::invoke_result_t<std::decay_t<F>&>;
        std::packaged_task<R()> packaged(std::forward<F>(fn));
        std::future<R> future = packaged.get_future();
        auto r = submit(FunctionTask::Func([p = std::move(packaged)]() mutable { p(); }),
                        std::move(name), priority);
        if (!r) return std::unexpected(r.error());
        return future;
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────

    /** @brief Starts minThreads workers. No-op if already running. May be called again after shutdown(). */
    void start();

    /**
     * @brief Stops the pool and joins every worker.
     * @param waitForPending true: run all queued tasks first; false: cancel them.
     * @throws std::logic_error if called from one of the pool's own workers.
     */
    void shutdown(bool waitForPending = true);

    /** @brief shutdown(false): cancels queued tasks and signals running ones to stop. */
    void shutdownNow();

    /** @brief Blocks until every accepted task has finished (queue empty, no task running). */
    void waitForAll();

    /** @brief waitForAll() with a timeout. @return false if the timeout expired. */
    [[nodiscard]] bool waitForAll(std::chrono::milliseconds timeout);

    // ── Observation ────────────────────────────────────────────────────────

    /** @brief The event bus of this pool. */
    EventBus& events() noexcept { return _events; }

    [[nodiscard]] PoolStats   stats() const noexcept;
    [[nodiscard]] std::size_t workerCount() const noexcept { return _live.load(); }
    [[nodiscard]] bool        isRunning() const noexcept { return _state.load() == State::Running; }
    [[nodiscard]] const ThreadPoolConfig& config() const noexcept { return _config; }

    /** @return true if the calling thread is one of this pool's workers. */
    [[nodiscard]] bool onWorkerThread() const noexcept;

private:
    friend class WorkerThread;
    enum class State { Created, Running, Stopping, Stopped };

    bool trySpawnWorker();
    void ensureCapacity();
    void reapFinishedLocked(std::vector<std::unique_ptr<WorkerThread>>& graveyard);
    std::size_t discardPendingAndClose();
    void taskFinished() noexcept;


    ThreadPoolConfig _config;
    TaskQueue        _queue;
    EventBus         _events;

    std::mutex                                 _workersMutex;
    std::vector<std::unique_ptr<WorkerThread>> _workers;
    std::mutex                                 _lifecycleMutex;  // serialises start()/shutdown()

    std::atomic<State>       _state{ State::Created };
    std::atomic<std::size_t> _live{ 0 };       // workers alive (not yet retired/stopped)
    std::atomic<std::size_t> _idle{ 0 };       // workers waiting for a task
    std::atomic<std::size_t> _pending{ 0 };    // accepted tasks not yet finished (queued + running)
    std::atomic<std::size_t> _nextWorkerId{ 0 };
    std::atomic<std::size_t> _completed{ 0 };
    std::atomic<std::size_t> _failed{ 0 };
    std::atomic<std::size_t> _cancelled{ 0 };
    std::atomic<std::size_t> _rejected{ 0 };

    std::mutex              _doneMutex;
    std::condition_variable _doneCv;
};

} // namespace TK4CPP::TP
