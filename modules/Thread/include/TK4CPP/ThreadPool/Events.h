/**
 * @file Events.h
 * @brief Event payloads and the per-pool EventBus.
 *
 * Every ThreadPool owns one EventBus (see ThreadPool::events()). Subscribe with
 * EventBus::on(); keep the returned Connection alive for as long as the handler
 * must stay registered.
 *
 * @code
 * auto c = pool.events().on<TaskCompletedEvent>([](const TaskCompletedEvent& e) {
 *     std::cout << e.task->name() << " took " << e.elapsed.count() << " us\n";
 * });
 * @endcode
 *
 * Events are delivered synchronously on the thread that produced them (a worker
 * thread for task/worker events, the caller for submit/start/shutdown), so
 * handlers must be fast and thread-safe.
 */
#pragma once

#include "Signal.h"
#include "Types.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace TK4CPP::TP {

class ITask;

// ───────────────────────────── Task events ─────────────────────────────

/** @brief Base of all task related events. */
struct TaskEvent {
    TaskEvent(std::shared_ptr<ITask> t, std::size_t w) : task(std::move(t)), workerId(w) {}

    std::shared_ptr<ITask> task;      ///< The task the event refers to.
    std::size_t            workerId;  ///< Worker involved (0 when none, e.g. on submit).
};

/** @brief A submit() call passed validation; the task is about to be queued. */
struct TaskSubmittedEvent : TaskEvent { using TaskEvent::TaskEvent; };

/** @brief A worker is about to run the task. */
struct TaskStartedEvent : TaskEvent { using TaskEvent::TaskEvent; };

/** @brief The task finished without throwing. */
struct TaskCompletedEvent : TaskEvent {
    TaskCompletedEvent(std::shared_ptr<ITask> t, std::size_t w, std::chrono::microseconds e)
        : TaskEvent(std::move(t), w), elapsed(e) {}

    std::chrono::microseconds elapsed;  ///< Wall-clock time spent in execute().
};

/** @brief The task threw. The task is in TaskStatus::Failed and may be resubmitted. */
struct TaskFailedEvent : TaskEvent {
    TaskFailedEvent(std::shared_ptr<ITask> t, std::size_t w, std::exception_ptr ex,
                    std::string msg, std::chrono::microseconds e)
        : TaskEvent(std::move(t), w), exception(std::move(ex)),
          errorMessage(std::move(msg)), elapsed(e) {}

    std::exception_ptr        exception;
    std::string               errorMessage;  ///< what() of the exception, or "Unknown exception".
    std::chrono::microseconds elapsed;
};

/** @brief The task was cancelled before it started (by cancel() or shutdownNow()). */
struct TaskCancelledEvent : TaskEvent { using TaskEvent::TaskEvent; };

/** @brief submit() failed after TaskSubmittedEvent had been emitted. */
struct TaskRejectedEvent : TaskEvent {
    TaskRejectedEvent(std::shared_ptr<ITask> t, SubmitError r)
        : TaskEvent(std::move(t), 0), reason(r) {}

    SubmitError reason;
};

// ───────────────────────────── Worker events ─────────────────────────────

/** @brief Base of all worker related events. */
struct WorkerEvent {
    explicit WorkerEvent(std::size_t id) : workerId(id) {}
    std::size_t workerId;
};

/** @brief A worker thread was created. */
struct WorkerStartedEvent : WorkerEvent { using WorkerEvent::WorkerEvent; };

/** @brief A worker finished a task and is waiting for the next one. */
struct WorkerIdleEvent : WorkerEvent {
    WorkerIdleEvent(std::size_t id, std::size_t completed) : WorkerEvent(id), tasksCompleted(completed) {}
    std::size_t tasksCompleted;  ///< Tasks run so far by this worker.
};

/** @brief A worker picked up a task. */
struct WorkerBusyEvent : WorkerEvent {
    WorkerBusyEvent(std::size_t id, std::chrono::milliseconds idle) : WorkerEvent(id), idleDuration(idle) {}
    std::chrono::milliseconds idleDuration;  ///< How long the worker had been waiting.
};

/** @brief A worker thread is exiting (idle timeout or pool shutdown). */
struct WorkerStoppedEvent : WorkerEvent {
    WorkerStoppedEvent(std::size_t id, std::size_t completed) : WorkerEvent(id), tasksCompleted(completed) {}
    std::size_t tasksCompleted;
};

// ───────────────────────────── Pool events ─────────────────────────────

/** @brief Base of pool events: carries a snapshot of the pool load. */
struct PoolEvent {
    PoolEvent(std::size_t active, std::size_t idle, std::size_t queued)
        : activeWorkers(active), idleWorkers(idle), queuedTasks(queued) {}

    std::size_t activeWorkers;
    std::size_t idleWorkers;
    std::size_t queuedTasks;
};

/** @brief start() completed. */
struct PoolStartedEvent : PoolEvent { using PoolEvent::PoolEvent; };

/** @brief A worker was added because the load exceeded the idle capacity. */
struct PoolScaledUpEvent : PoolEvent {
    PoolScaledUpEvent(std::size_t a, std::size_t i, std::size_t q, std::size_t n)
        : PoolEvent(a, i, q), newWorkerCount(n) {}
    std::size_t newWorkerCount;  ///< Number of workers after scaling.
};

/** @brief Idle workers above minThreads were retired. */
struct PoolScaledDownEvent : PoolEvent {
    PoolScaledDownEvent(std::size_t a, std::size_t i, std::size_t q, std::size_t r)
        : PoolEvent(a, i, q), removedWorkers(r) {}
    std::size_t removedWorkers;
};

/** @brief A task was rejected because the bounded queue was full. */
struct PoolQueueFullEvent : PoolEvent {
    PoolQueueFullEvent(std::size_t a, std::size_t i, std::size_t q, std::shared_ptr<ITask> t)
        : PoolEvent(a, i, q), rejectedTask(std::move(t)) {}
    std::shared_ptr<ITask> rejectedTask;
};

/** @brief shutdown() completed (all workers joined). */
struct PoolShutdownEvent : PoolEvent {
    PoolShutdownEvent(std::size_t a, std::size_t i, std::size_t q, bool g, std::size_t p)
        : PoolEvent(a, i, q), graceful(g), pendingTasks(p) {}
    bool        graceful;      ///< true: the queue was drained; false: pending tasks were cancelled.
    std::size_t pendingTasks;  ///< Tasks still queued when shutdown started.
};

// ───────────────────────────── Event bus ─────────────────────────────

/** @brief True for every event type the EventBus can carry. */
template<class E>
concept PoolEventType =
    std::is_base_of_v<TaskEvent, E> || std::is_base_of_v<WorkerEvent, E> || std::is_base_of_v<PoolEvent, E>;

/**
 * @brief Typed publish/subscribe hub, one per ThreadPool.
 *
 * Unlike a global singleton, two pools never see each other's events.
 */
class EventBus {
public:
    /**
     * @brief Subscribes a callable to events of type @p E.
     * @tparam E Event type.
     * @param handler Callable invocable as `void(const E&)`.
     */
    template<PoolEventType E, class F>
    [[nodiscard]] Connection on(F&& handler) {
        return signal<E>().connect(std::forward<F>(handler));
    }

    /** @brief Subscribes a member function (the object must outlive the Connection). */
    template<class C, PoolEventType E>
    [[nodiscard]] Connection on(C& object, void (C::*method)(const E&)) {
        return signal<E>().connect([&object, method](const E& e) { (object.*method)(e); });
    }

    /** @overload for const member functions. */
    template<class C, PoolEventType E>
    [[nodiscard]] Connection on(C& object, void (C::*method)(const E&) const) {
        return signal<E>().connect([&object, method](const E& e) { (object.*method)(e); });
    }

    /** @return true if somebody listens to events of type @p E. */
    template<PoolEventType E>
    [[nodiscard]] bool hasListeners() const noexcept { return signal<E>().hasListeners(); }

    /**
     * @brief Builds an @p E from @p args and delivers it. Nothing is constructed
     *        when nobody is subscribed.
     */
    template<PoolEventType E, class... Args>
    void emit(Args&&... args) const {
        const auto& s = signal<E>();
        if (!s.hasListeners()) return;
        const E event(std::forward<Args>(args)...);
        s.emit(event);
    }

private:
    template<class E> Signal<E>& signal() { return std::get<Signal<E>>(_signals); }
    template<class E> const Signal<E>& signal() const { return std::get<Signal<E>>(_signals); }

    std::tuple<
        Signal<TaskSubmittedEvent>, Signal<TaskStartedEvent>, Signal<TaskCompletedEvent>,
        Signal<TaskFailedEvent>, Signal<TaskCancelledEvent>, Signal<TaskRejectedEvent>,
        Signal<WorkerStartedEvent>, Signal<WorkerIdleEvent>, Signal<WorkerBusyEvent>,
        Signal<WorkerStoppedEvent>,
        Signal<PoolStartedEvent>, Signal<PoolScaledUpEvent>, Signal<PoolScaledDownEvent>,
        Signal<PoolQueueFullEvent>, Signal<PoolShutdownEvent>
    > _signals;
};

} // namespace TK4CPP::TP
