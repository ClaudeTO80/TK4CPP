/**
 * @file TaskQueue.h
 * @brief Thread-safe priority queue feeding the workers.
 */
#pragma once

#include "Export.h"
#include "Task.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace TK4CPP::TP {

/**
 * @brief Bounded or unbounded priority queue.
 *
 * Ordering: higher Priority first; FIFO among equal priorities (strict, based
 * on an insertion counter, not on timestamps).
 */
class TK4CPP_TP_API TaskQueue {
public:
    enum class PushResult { Ok, Full, Closed };
    enum class PopStatus  { Task, Timeout, Closed };

    /** @brief Outcome of pop()/popFor(). `task` is set only when status == Task. */
    struct PopResult {
        PopStatus status;
        TaskPtr   task;
    };

    /** @param maxSize Capacity, 0 = unbounded. */
    explicit TaskQueue(std::size_t maxSize = 0);

    /**
     * @brief Inserts a task.
     * @param task Task to enqueue (its priority is read now).
     * @param block If true and the queue is full, wait for room instead of failing.
     */
    PushResult push(TaskPtr task, bool block = false);

    /** @brief Blocks until a task is available, or the queue is closed *and* empty. */
    PopResult pop();

    /** @brief Like pop() but gives up after @p timeout (status Timeout). */
    PopResult popFor(std::chrono::milliseconds timeout);

    /** @brief Non-blocking pop; returns a null task if the queue is empty. */
    TaskPtr tryPop();

    /** @brief Removes and returns every queued task (in no particular order). */
    std::vector<TaskPtr> drain();

    /** @brief Atomically closes the queue and removes every queued task (used by immediate shutdown). */
    std::vector<TaskPtr> closeAndDrain();

    /** @brief Rejects further pushes and wakes every waiter. Queued tasks can still be popped. */
    void close();

    /** @brief Re-enables pushes after close() (used when a pool is restarted). */
    void reopen();

    [[nodiscard]] bool        isClosed() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool        empty() const;
    [[nodiscard]] std::size_t capacity() const noexcept { return _maxSize; }

private:
    struct Entry {
        TaskPtr       task;
        int           priority;
        std::uint64_t seq;
    };
    struct EntryLess {
        bool operator()(const Entry& a, const Entry& b) const noexcept {
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.seq > b.seq;
        }
    };

    TaskPtr takeLocked();

    mutable std::mutex      _mutex;
    std::condition_variable _notEmpty;
    std::condition_variable _notFull;
    std::vector<Entry>      _heap;
    std::size_t             _maxSize;
    std::uint64_t           _nextSeq = 0;
    bool                    _closed  = false;
};

} // namespace TK4CPP::TP
