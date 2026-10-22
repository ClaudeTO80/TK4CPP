#include "TK4CPP/ThreadPool/TaskQueue.h"

#include <algorithm>

namespace TK4CPP::TP {

TaskQueue::TaskQueue(std::size_t maxSize) : _maxSize(maxSize) {}

TaskQueue::PushResult TaskQueue::push(TaskPtr task, bool block) {
    {
        std::unique_lock lk(_mutex);
        if (block && _maxSize > 0)
            _notFull.wait(lk, [&] { return _closed || _heap.size() < _maxSize; });

        if (_closed) return PushResult::Closed;
        if (_maxSize > 0 && _heap.size() >= _maxSize) return PushResult::Full;

        const int prio = static_cast<int>(task->priority());
        _heap.push_back(Entry{ std::move(task), prio, _nextSeq++ });
        std::push_heap(_heap.begin(), _heap.end(), EntryLess{});
    }
    _notEmpty.notify_one();
    return PushResult::Ok;
}

TaskPtr TaskQueue::takeLocked() {
    std::pop_heap(_heap.begin(), _heap.end(), EntryLess{});
    TaskPtr t = std::move(_heap.back().task);
    _heap.pop_back();
    return t;
}

TaskQueue::PopResult TaskQueue::pop() {
    TaskPtr t;
    {
        std::unique_lock lk(_mutex);
        _notEmpty.wait(lk, [&] { return !_heap.empty() || _closed; });
        if (_heap.empty()) return { PopStatus::Closed, nullptr };  // closed and drained
        t = takeLocked();
    }
    _notFull.notify_one();
    return { PopStatus::Task, std::move(t) };
}

TaskQueue::PopResult TaskQueue::popFor(std::chrono::milliseconds timeout) {
    TaskPtr t;
    {
        std::unique_lock lk(_mutex);
        if (!_notEmpty.wait_for(lk, timeout, [&] { return !_heap.empty() || _closed; }))
            return { PopStatus::Timeout, nullptr };
        if (_heap.empty()) return { PopStatus::Closed, nullptr };
        t = takeLocked();
    }
    _notFull.notify_one();
    return { PopStatus::Task, std::move(t) };
}

TaskPtr TaskQueue::tryPop() {
    TaskPtr t;
    {
        std::lock_guard lk(_mutex);
        if (_heap.empty()) return nullptr;
        t = takeLocked();
    }
    _notFull.notify_one();
    return t;
}

std::vector<TaskPtr> TaskQueue::drain() {
    std::vector<TaskPtr> out;
    {
        std::lock_guard lk(_mutex);
        out.reserve(_heap.size());
        for (auto& e : _heap) out.push_back(std::move(e.task));
        _heap.clear();
    }
    _notFull.notify_all();
    return out;
}

std::vector<TaskPtr> TaskQueue::closeAndDrain() {
    std::vector<TaskPtr> out;
    {
        std::lock_guard lk(_mutex);
        _closed = true;  // set under the same lock: a blocked producer cannot slip in after the drain
        out.reserve(_heap.size());
        for (auto& e : _heap) out.push_back(std::move(e.task));
        _heap.clear();
    }
    _notEmpty.notify_all();
    _notFull.notify_all();
    return out;
}

void TaskQueue::close() {
    {
        std::lock_guard lk(_mutex);
        _closed = true;
    }
    _notEmpty.notify_all();
    _notFull.notify_all();
}

void TaskQueue::reopen() {
    std::lock_guard lk(_mutex);
    _closed = false;
}

bool TaskQueue::isClosed() const {
    std::lock_guard lk(_mutex);
    return _closed;
}

std::size_t TaskQueue::size() const {
    std::lock_guard lk(_mutex);
    return _heap.size();
}

bool TaskQueue::empty() const {
    std::lock_guard lk(_mutex);
    return _heap.empty();
}

} // namespace TK4CPP::TP
