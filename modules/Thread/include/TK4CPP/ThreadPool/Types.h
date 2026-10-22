/**
 * @file Types.h
 * @brief Small value types shared by the whole ThreadPool module.
 */
#pragma once

#include <cstdint>

namespace TK4CPP::TP {

/** @brief Scheduling priority of a task. Higher values are dequeued first. */
enum class Priority : int {
    Low    = 0,
    Normal = 5,
    High   = 10
};

/** @brief Lifecycle state of a task. */
enum class TaskStatus : std::uint8_t {
    Pending,    ///< Created, not yet submitted.
    Queued,     ///< Accepted by a pool (or re-armed for a retry), waiting for a worker.
    Running,    ///< Currently executing on a worker.
    Completed,  ///< execute() returned normally.
    Cancelled,  ///< Cancelled before it started.
    Failed      ///< execute() threw.
};

/** @brief Reason why ThreadPool::submit() refused a task. */
enum class SubmitError : std::uint8_t {
    NotRunning,  ///< The pool is not started, or is shutting down.
    QueueFull,   ///< The bounded queue is full (OverflowPolicy::Reject).
    InvalidTask  ///< Null task, or a task that is already queued/running/finished.
};

/** @brief What submit() does when the bounded queue is full. */
enum class OverflowPolicy : std::uint8_t {
    Reject,  ///< Fail immediately with SubmitError::QueueFull.
    Block    ///< Wait for room. Falls back to Reject when called from a pool worker.
};

/** @brief Human readable name of a TaskStatus. */
[[nodiscard]] constexpr const char* toString(TaskStatus s) noexcept {
    switch (s) {
    case TaskStatus::Pending:   return "Pending";
    case TaskStatus::Queued:    return "Queued";
    case TaskStatus::Running:   return "Running";
    case TaskStatus::Completed: return "Completed";
    case TaskStatus::Cancelled: return "Cancelled";
    case TaskStatus::Failed:    return "Failed";
    }
    return "Unknown";
}

/** @brief Human readable name of a SubmitError. */
[[nodiscard]] constexpr const char* toString(SubmitError e) noexcept {
    switch (e) {
    case SubmitError::NotRunning:  return "pool not running";
    case SubmitError::QueueFull:   return "queue full";
    case SubmitError::InvalidTask: return "invalid task";
    }
    return "unknown";
}

} // namespace TK4CPP::TP
