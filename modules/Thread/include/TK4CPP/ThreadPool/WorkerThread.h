/**
 * @file WorkerThread.h
 * @brief Pool worker. Internal: instances are created and owned by ThreadPool.
 */
#pragma once

#include "Export.h"
#include "Task.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <thread>

namespace TK4CPP::TP {

class ThreadPool;

/** @brief A pool thread: pops tasks, runs them, retires after idleTimeout when above minThreads. */
class TK4CPP_TP_API WorkerThread {
public:
    /** @brief Joins the thread. The owning pool must have closed the queue first. */
    ~WorkerThread();

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    [[nodiscard]] std::size_t id() const noexcept { return _id; }
    [[nodiscard]] bool        isIdle() const noexcept { return _idle.load(); }
    [[nodiscard]] bool        isFinished() const noexcept { return _finished.load(); }
    [[nodiscard]] std::size_t tasksCompleted() const noexcept { return _tasksCompleted.load(); }

    /** @brief Signals cooperative cancellation to the task being executed, if any. */
    void requestStopCurrentTask() noexcept;

    /** @return The pool whose worker the calling thread is, or nullptr. */
    [[nodiscard]] static const ThreadPool* currentPool() noexcept;

private:
    friend class ThreadPool;
    WorkerThread(ThreadPool& pool, std::size_t id);

    void start();
    void loop();
    bool tryRetire();
    void runTask(TaskPtr task);

    ThreadPool&              _pool;
    std::size_t              _id;
    std::atomic<bool>        _idle{ true };
    std::atomic<bool>        _finished{ false };
    std::atomic<std::size_t> _tasksCompleted{ 0 };
    std::mutex               _currentMutex;
    TaskPtr                  _current;
    std::jthread             _thread;
};

} // namespace TK4CPP::TP
