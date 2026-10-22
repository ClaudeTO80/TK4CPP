/**
 * @file Task.h
 * @brief Task abstraction executed by the ThreadPool.
 */
#pragma once

#include "Events.h"
#include "Export.h"
#include "Types.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace TK4CPP::TP {

/**
 * @brief Base class of every unit of work.
 *
 * Derive and implement execute(), or use FunctionTask for lambdas. Tasks must
 * be owned by a std::shared_ptr (they use shared_from_this()).
 *
 * Lifecycle: Pending -> Queued -> Running -> Completed | Failed, or Pending/Queued -> Cancelled.
 * A Failed task can be submitted again (see RetryHandler).
 */
class TK4CPP_TP_API ITask : public std::enable_shared_from_this<ITask> {
public:
    /**
     * @param name Label used in events and logs.
     * @param priority Scheduling priority.
     */
    explicit ITask(std::string name = "task", Priority priority = Priority::Normal);
    virtual ~ITask() = default;

    ITask(const ITask&) = delete;
    ITask& operator=(const ITask&) = delete;

    /**
     * @brief Runs the task on the calling thread. Called by the workers.
     *
     * Never throws: exceptions from execute() become a TaskFailedEvent.
     * Events are emitted as the very last action, so a handler may safely
     * resubmit the task.
     *
     * @param bus Event destination, may be null.
     * @param workerId Id of the calling worker (reported in events).
     * @return Completed, Failed or Cancelled. Any other value is the current status of a task
     *         that was not runnable (e.g. already running) and was left untouched.
     */
    TaskStatus run(const EventBus* bus = nullptr, std::size_t workerId = 0) noexcept;

    /**
     * @brief Cancels the task.
     *
     * A Pending or Queued task will never run (the queue slot is reclaimed lazily when a
     * worker pops it). A Running task only gets its stop token signalled: it
     * must poll stopRequested() to terminate early.
     *
     * @return true if the task was cancelled before starting.
     */
    bool cancel() noexcept;

    /** @brief Signals cooperative cancellation to a running execute(). */
    void requestStop() noexcept { _stop.request_stop(); }

    /** @return Token that becomes signalled by cancel()/requestStop(); poll it inside execute(). */
    [[nodiscard]] std::stop_token stopToken() const noexcept { return _stop.get_token(); }
    [[nodiscard]] bool stopRequested() const noexcept { return _stop.stop_requested(); }

    [[nodiscard]] bool        isCancelled() const noexcept { return status() == TaskStatus::Cancelled; }
    [[nodiscard]] TaskStatus  status() const noexcept { return _status.load(std::memory_order_acquire); }
    [[nodiscard]] const std::string& name() const noexcept { return _name; }
    [[nodiscard]] Priority    priority() const noexcept { return _priority; }
    [[nodiscard]] std::chrono::system_clock::time_point createdAt() const noexcept { return _createdAt; }
    /** @return Id of the worker that ran the task last (0 before the first run). */
    [[nodiscard]] std::size_t workerId() const noexcept { return _workerId.load(std::memory_order_relaxed); }

    /**
     * @brief Validates and arms the task for queueing. Called by ThreadPool::submit().
     * @return The previous status (Pending, or Failed for a retry), or nullopt if the task is
     *         already Queued/Running or ended Completed/Cancelled and therefore not submittable.
     */
    std::optional<TaskStatus> prepareForSubmit() noexcept;

    /** @brief Undoes prepareForSubmit() when the pool refused the task. */
    void abortSubmit(TaskStatus previous) noexcept;

protected:
    /** @brief The work to perform. May throw. */
    virtual void execute() = 0;

    /** @brief Optional hook invoked on the worker after execute() threw, before TaskFailedEvent. */
    virtual void onError(std::exception_ptr) {}

private:
    std::string                           _name;
    Priority                              _priority;
    std::atomic<TaskStatus>               _status{ TaskStatus::Pending };
    std::chrono::system_clock::time_point _createdAt;
    std::atomic<std::size_t>              _workerId{ 0 };
    std::stop_source                      _stop;
};

/** @brief Shared ownership handle used throughout the module. */
using TaskPtr = std::shared_ptr<ITask>;

/** @brief Task wrapping any callable (move-only callables are supported). */
class TK4CPP_TP_API FunctionTask : public ITask {
public:
    using Func = std::move_only_function<void()>;

    explicit FunctionTask(Func fn, std::string name = "fn_task", Priority priority = Priority::Normal)
        : ITask(std::move(name), priority), _fn(std::move(fn)) {}

protected:
    void execute() override { _fn(); }

private:
    Func _fn;
};

} // namespace TK4CPP::TP
