#include "TK4CPP/ThreadPool/Task.h"

namespace TK4CPP::TP {

namespace {

std::string describe(const std::exception_ptr& ex) {
    try {
        std::rethrow_exception(ex);
    }
    catch (const std::exception& e) {
        return e.what();
    }
    catch (...) {
        return "Unknown exception";
    }
}

} // namespace

ITask::ITask(std::string name, Priority priority)
    : _name(std::move(name))
    , _priority(priority)
    , _createdAt(std::chrono::system_clock::now())
{
}

TaskStatus ITask::run(const EventBus* bus, std::size_t workerId) noexcept {
    // Not owned by a shared_ptr (direct use): events then carry a null task.
    const TaskPtr self = weak_from_this().lock();

    TaskStatus cur = _status.load(std::memory_order_acquire);
    for (;;) {
        if (cur == TaskStatus::Pending || cur == TaskStatus::Queued) {
            if (_status.compare_exchange_weak(cur, TaskStatus::Running, std::memory_order_acq_rel))
                break;
            continue;  // cur was refreshed by the failed CAS
        }
        if (cur == TaskStatus::Cancelled && bus)
            bus->emit<TaskCancelledEvent>(self, workerId);
        return cur;
    }

    _workerId.store(workerId, std::memory_order_relaxed);
    if (bus) bus->emit<TaskStartedEvent>(self, workerId);

    std::exception_ptr error;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        execute();
    }
    catch (...) {
        error = std::current_exception();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0);

    // Events go out last: after this point a handler may resubmit the task
    // and another worker may start it while this call is still unwinding.
    if (!error) {
        _status.store(TaskStatus::Completed, std::memory_order_release);
        if (bus) bus->emit<TaskCompletedEvent>(self, workerId, elapsed);
        return TaskStatus::Completed;
    }

    _status.store(TaskStatus::Failed, std::memory_order_release);
    try { onError(error); } catch (...) { /* the hook must not break the worker */ }
    if (bus) {
        try { bus->emit<TaskFailedEvent>(self, workerId, error, describe(error), elapsed); }
        catch (...) {}
    }
    return TaskStatus::Failed;
}

bool ITask::cancel() noexcept {
    TaskStatus cur = _status.load(std::memory_order_acquire);
    while (cur == TaskStatus::Pending || cur == TaskStatus::Queued) {
        if (_status.compare_exchange_weak(cur, TaskStatus::Cancelled, std::memory_order_acq_rel)) {
            _stop.request_stop();
            return true;
        }
    }
    if (cur == TaskStatus::Running) _stop.request_stop();
    return false;
}

std::optional<TaskStatus> ITask::prepareForSubmit() noexcept {
    TaskStatus cur = TaskStatus::Pending;
    if (_status.compare_exchange_strong(cur, TaskStatus::Queued, std::memory_order_acq_rel))
        return TaskStatus::Pending;

    if (cur == TaskStatus::Failed &&
        _status.compare_exchange_strong(cur, TaskStatus::Queued, std::memory_order_acq_rel)) {
        _stop = std::stop_source{};  // fresh token for the new attempt
        return TaskStatus::Failed;
    }
    return std::nullopt;
}

void ITask::abortSubmit(TaskStatus previous) noexcept {
    TaskStatus expected = TaskStatus::Queued;
    _status.compare_exchange_strong(expected, previous, std::memory_order_acq_rel);
}

} // namespace TK4CPP::TP
