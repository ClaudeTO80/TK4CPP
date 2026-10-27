/**
 * @file ThreadPoolAdapter.h
 * @brief IExecutor implementation that runs scheduler jobs on a TK4CPP::TP::ThreadPool.
 *
 * The scheduler core only knows @ref TK4CPP::Scheduler::IExecutor; this adapter is what ties it to the
 * TK4CPP Thread module (target `TK4CPP::Thread`, which the Scheduler module depends on).
 */
#pragma once

#include <TK4CPP/Scheduler/Export.h>

#include <TK4CPP/Scheduler/Executor.h>
#include <TK4CPP/ThreadPool/ThreadPool.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace TK4CPP::Scheduler {

/**
 * @brief Runs job executions as tasks of a TK4CPP::TP::ThreadPool.
 *
 * Typical use: one application-wide pool shared with the rest of the program.
 *
 * @code
 * auto pool = std::make_shared<TK4CPP::TP::ThreadPool>(TK4CPP::TP::ThreadPoolConfig::ioBound());
 * pool->start();
 *
 * JobScheduler::Options options;
 * options.executor = std::make_shared<ThreadPoolAdapter>(pool);
 * JobScheduler scheduler(std::move(options));
 * @endcode
 *
 * ### Ownership
 * - **Borrowed pool** (default): the adapter never starts or stops the pool. On
 *   IExecutor::shutdown() it waits only for the tasks *it* submitted, so other users of the pool
 *   are unaffected and the pool stays usable afterwards.
 * - **Owned pool** (@ref createOwning, or Options::stopPoolOnShutdown): the pool is shut down too.
 *
 * ### Behaviour
 * - Each execution becomes one TP::FunctionTask named `labelPrefix + jobId` (visible in the pool's
 *   events and logs) with Options::priority.
 * - A refused submission (pool not running, bounded queue full) makes @ref submit return false;
 *   the scheduler reports it to its listeners. @ref rejected counts them.
 * - `shutdown(true)` waits for every task of this adapter, queued or running.
 *   `shutdown(false)` cancels the tasks that have not started and waits for the running ones;
 *   running tasks are never interrupted.
 *
 * @note Use a pool with `OverflowPolicy::Reject` (the default) or an unbounded queue. With
 *       `OverflowPolicy::Block` a full queue blocks the scheduler's dispatcher thread inside
 *       @ref submit and delays every trigger.
 * @note Do not call JobScheduler::shutdown() from a job running on the pool: it would wait for
 *       itself.
 */
class TK4CPP_SCHEDULER_EXPORT ThreadPoolAdapter final : public IExecutor {
public:
    /// @brief Tuning of the adapter.
    struct Options {
        TP::Priority priority{TP::Priority::Normal}; ///< Priority of every submitted task.
        std::string labelPrefix{"scheduler:"};       ///< Prefix of the task names.
        bool startPoolIfNeeded{false};               ///< Call ThreadPool::start() if it is not running.
        bool stopPoolOnShutdown{false};              ///< Also shut the pool down in shutdown().
    };

    /**
     * @brief Uses an existing pool without taking over its lifecycle.
     * @param pool Pool to run jobs on; must not be null and must be started before the first fire
     *             (unless Options::startPoolIfNeeded). Kept alive by the adapter.
     */
    explicit ThreadPoolAdapter(std::shared_ptr<TP::ThreadPool> pool);

    /// @overload with explicit @p options.
    ThreadPoolAdapter(std::shared_ptr<TP::ThreadPool> pool, Options options);

    /**
     * @brief Creates a private pool, starts it, and stops it when the scheduler shuts down.
     * @param config Pool configuration.
     */
    [[nodiscard]] static std::shared_ptr<ThreadPoolAdapter> createOwning(
        TP::ThreadPoolConfig config = TP::ThreadPoolConfig::defaults());

    ~ThreadPoolAdapter() override;
    ThreadPoolAdapter(const ThreadPoolAdapter&) = delete;
    ThreadPoolAdapter& operator=(const ThreadPoolAdapter&) = delete;

    /// @copydoc IExecutor::submit
    [[nodiscard]] bool submit(std::function<void()> task, std::string_view label) override;

    /// @copydoc IExecutor::shutdown
    void shutdown(bool drainQueue) override;

    /// @brief Tasks accepted by this adapter that have not finished yet (queued + running).
    [[nodiscard]] std::size_t outstanding() const;

    /// @brief Submissions refused so far (pool not running, queue full, adapter shut down).
    [[nodiscard]] std::uint64_t rejected() const noexcept { return rejected_.load(); }

    /// @brief The underlying pool.
    [[nodiscard]] const std::shared_ptr<TP::ThreadPool>& pool() const noexcept { return pool_; }

private:
    struct State;

    std::shared_ptr<TP::ThreadPool> pool_;
    Options options_;
    std::shared_ptr<State> state_;
    std::atomic<std::uint64_t> rejected_{0};
};

} // namespace TK4CPP::Scheduler
