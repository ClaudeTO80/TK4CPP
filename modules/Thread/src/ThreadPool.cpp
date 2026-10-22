#include "TK4CPP/ThreadPool/ThreadPool.h"

#include <algorithm>
#include <stdexcept>

namespace TK4CPP::TP {

namespace {

ThreadPoolConfig sanitize(ThreadPoolConfig c) {
    c.maxThreads = std::max({ c.maxThreads, c.minThreads, std::size_t{ 1 } });
    return c;
}

} // namespace

ThreadPool::ThreadPool(ThreadPoolConfig config)
    : _config(sanitize(std::move(config)))
    , _queue(_config.maxQueueSize)
{}

ThreadPool::~ThreadPool() {
    try {
        shutdown(true);
    }
    catch (...) { /* never throw from a destructor */ }
}

// ───────────────────────────── lifecycle ─────────────────────────────

void ThreadPool::start() {
    std::lock_guard lifecycle(_lifecycleMutex);
    const State s = _state.load();
    if (s == State::Running) return;
    if (s == State::Stopped) _queue.reopen();

    _state.store(State::Running);
    for (std::size_t i = 0; i < _config.minThreads; ++i)
        trySpawnWorker();

    const PoolStats st = stats();
    _events.emit<PoolStartedEvent>(st.activeWorkers, st.idleWorkers, st.queuedTasks);
}

void ThreadPool::shutdown(bool waitForPending) {
    if (onWorkerThread())
        throw std::logic_error("ThreadPool::shutdown() called from one of the pool's own workers");

    std::lock_guard lifecycle(_lifecycleMutex);
    State expected = State::Running;
    if (!_state.compare_exchange_strong(expected, State::Stopping)) return;

    const std::size_t pendingAtCall = _queue.size();
    if (!waitForPending) discardPendingAndClose();

    // Workers keep popping until the queue is closed *and* empty, which gives the graceful drain.
    _queue.close();

    std::vector<std::unique_ptr<WorkerThread>> workers;
    {
        std::lock_guard lk(_workersMutex);
        workers.swap(_workers);
    }
    workers.clear();  // joins every thread (outside any lock: workers may call back into the pool)

    _state.store(State::Stopped);
    const PoolStats st = stats();
    _events.emit<PoolShutdownEvent>(st.activeWorkers, st.idleWorkers, st.queuedTasks,
                                    waitForPending, pendingAtCall);
}

void ThreadPool::shutdownNow() { shutdown(false); }

std::size_t ThreadPool::discardPendingAndClose() {
    // Running tasks: ask them to stop cooperatively.
    {
        std::lock_guard lk(_workersMutex);
        for (auto& w : _workers) w->requestStopCurrentTask();
    }
    // Queued tasks: cancel and account for them.
    auto discarded = _queue.closeAndDrain();
    for (auto& t : discarded) {
        t->cancel();
        _cancelled.fetch_add(1);
        _events.emit<TaskCancelledEvent>(t, 0);
        t.reset();
        taskFinished();
    }
    return discarded.size();
}

void ThreadPool::taskFinished() noexcept {
    if (_pending.fetch_sub(1) == 1) {
        std::lock_guard lk(_doneMutex);  // pairs with the predicate check in waitForAll()
        _doneCv.notify_all();
    }
}

void ThreadPool::waitForAll() {
    std::unique_lock lk(_doneMutex);
    _doneCv.wait(lk, [this] { return _pending.load() == 0; });
}

bool ThreadPool::waitForAll(std::chrono::milliseconds timeout) {
    std::unique_lock lk(_doneMutex);
    return _doneCv.wait_for(lk, timeout, [this] { return _pending.load() == 0; });
}

// ───────────────────────────── submission ─────────────────────────────

SubmitResult ThreadPool::submit(TaskPtr task) {
    if (!task) return std::unexpected(SubmitError::InvalidTask);
    if (_state.load() != State::Running) return std::unexpected(SubmitError::NotRunning);

    const auto previous = task->prepareForSubmit();
    if (!previous) return std::unexpected(SubmitError::InvalidTask);

    _pending.fetch_add(1);
    // Emitted before the push so that "submitted" always precedes "started" in the event stream.
    _events.emit<TaskSubmittedEvent>(task, std::size_t{ 0 });

    const bool block = _config.overflowPolicy == OverflowPolicy::Block && !onWorkerThread();
    const auto pushed = _queue.push(task, block);
    if (pushed != TaskQueue::PushResult::Ok) {
        task->abortSubmit(*previous);
        const SubmitError why = pushed == TaskQueue::PushResult::Full ? SubmitError::QueueFull
                                                                      : SubmitError::NotRunning;
        if (why == SubmitError::QueueFull) {
            _rejected.fetch_add(1);
            const PoolStats st = stats();
            _events.emit<PoolQueueFullEvent>(st.activeWorkers, st.idleWorkers, st.queuedTasks, task);
        }
        _events.emit<TaskRejectedEvent>(task, why);
        taskFinished();
        return std::unexpected(why);
    }

    ensureCapacity();
    return {};
}

SubmitResult ThreadPool::submit(FunctionTask::Func fn, std::string name, Priority priority) {
    return submit(std::make_shared<FunctionTask>(std::move(fn), std::move(name), priority));
}

// ───────────────────────────── workers ─────────────────────────────

bool ThreadPool::trySpawnWorker() {
    std::vector<std::unique_ptr<WorkerThread>> graveyard;  // destroyed (joined) after the lock is released
    {
        std::lock_guard lk(_workersMutex);
        if (_state.load() != State::Running) return false;
        if (_live.load() >= _config.maxThreads) return false;

        reapFinishedLocked(graveyard);

        const std::size_t id = _nextWorkerId.fetch_add(1);
        _live.fetch_add(1);
        _idle.fetch_add(1);  // a new worker is born idle
        try {
            _workers.emplace_back(new WorkerThread(*this, id));
            _workers.back()->start();
        }
        catch (...) {  // e.g. std::system_error when the OS refuses a thread
            _workers.pop_back();
            _live.fetch_sub(1);
            _idle.fetch_sub(1);
            return false;
        }
    }
    return true;
}

void ThreadPool::reapFinishedLocked(std::vector<std::unique_ptr<WorkerThread>>& graveyard) {
    auto it = std::stable_partition(_workers.begin(), _workers.end(),
                                    [](const auto& w) { return !w->isFinished(); });
    for (auto f = it; f != _workers.end(); ++f) graveyard.push_back(std::move(*f));
    _workers.erase(it, _workers.end());
}

void ThreadPool::ensureCapacity() {
    // Dekker-style handshake with retiring workers: they re-run this check after decrementing the
    // counters, so a task queued while a worker retires is never left without a consumer.
    while (_state.load() == State::Running &&
           _queue.size() > _idle.load() &&
           _live.load() < _config.maxThreads) {
        if (!trySpawnWorker()) break;
        const PoolStats st = stats();
        _events.emit<PoolScaledUpEvent>(st.activeWorkers, st.idleWorkers, st.queuedTasks, _live.load());
    }
}

// ───────────────────────────── observation ─────────────────────────────

PoolStats ThreadPool::stats() const noexcept {
    const std::size_t live = _live.load();
    const std::size_t idle = std::min(_idle.load(), live);
    return { live - idle, idle, _queue.size(),
             _completed.load(), _failed.load(), _cancelled.load(), _rejected.load() };
}

bool ThreadPool::onWorkerThread() const noexcept { return WorkerThread::currentPool() == this; }


} // namespace TK4CPP::TP
