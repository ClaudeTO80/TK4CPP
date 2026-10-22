#include "TK4CPP/ThreadPool/WorkerThread.h"
#include "TK4CPP/ThreadPool/ThreadPool.h"

#include <string>

#if defined(__linux__) || defined(__APPLE__)
#  include <pthread.h>
#endif

namespace TK4CPP::TP {

namespace {

thread_local const ThreadPool* t_currentPool = nullptr;

/** Best effort: names the OS thread (visible in debuggers/top). No-op on Windows. */
void setCurrentThreadName(const std::string& name) {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());  // Linux limit: 15 chars
#elif defined(__APPLE__)
    pthread_setname_np(name.substr(0, 63).c_str());
#else
    (void)name;
#endif
}

} // namespace

WorkerThread::WorkerThread(ThreadPool& pool, std::size_t id) : _pool(pool), _id(id) {}

WorkerThread::~WorkerThread() = default;  // std::jthread joins

void WorkerThread::start() {
    _thread = std::jthread([this] { loop(); });
}

const ThreadPool* WorkerThread::currentPool() noexcept { return t_currentPool; }

void WorkerThread::requestStopCurrentTask() noexcept {
    std::lock_guard lk(_currentMutex);
    if (_current) _current->requestStop();
}

bool WorkerThread::tryRetire() {
    // Only retire while we stay above minThreads; the CAS makes concurrent retirements safe.
    std::size_t live = _pool._live.load();
    while (live > _pool._config.minThreads) {
        if (_pool._live.compare_exchange_weak(live, live - 1)) {
            _pool._idle.fetch_sub(1);
            return true;
        }
    }
    return false;
}

void WorkerThread::runTask(TaskPtr task) {
    {
        std::lock_guard lk(_currentMutex);
        _current = task;
    }
    const TaskStatus result = task->run(&_pool._events, _id);
    {
        std::lock_guard lk(_currentMutex);
        _current.reset();
    }

    switch (result) {
    case TaskStatus::Completed: _pool._completed.fetch_add(1); break;
    case TaskStatus::Failed:    _pool._failed.fetch_add(1);    break;
    case TaskStatus::Cancelled: _pool._cancelled.fetch_add(1); break;
    default: break;
    }
    if (result != TaskStatus::Cancelled) _tasksCompleted.fetch_add(1);

    task.reset();            // drop captured state before waiters are released
    _pool.taskFinished();
}

void WorkerThread::loop() {
    t_currentPool = &_pool;
    setCurrentThreadName(_pool._config.name + "-" + std::to_string(_id));
    _pool._events.emit<WorkerStartedEvent>(_id);

    const auto idleTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(_pool._config.idleTimeout);
    bool retired = false;

    for (;;) {
        const auto idleSince = std::chrono::steady_clock::now();
        auto r = idleTimeout.count() > 0 ? _pool._queue.popFor(idleTimeout) : _pool._queue.pop();

        if (r.status == TaskQueue::PopStatus::Task) {
            _idle.store(false);
            _pool._idle.fetch_sub(1);
            _pool._events.emit<WorkerBusyEvent>(_id,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - idleSince));

            runTask(std::move(r.task));

            _pool._idle.fetch_add(1);
            _idle.store(true);
            _pool._events.emit<WorkerIdleEvent>(_id, _tasksCompleted.load());
            continue;
        }

        if (r.status == TaskQueue::PopStatus::Timeout) {
            if (tryRetire()) { retired = true; break; }
            continue;
        }

        // Closed and drained: pool shutdown.
        _pool._live.fetch_sub(1);
        _pool._idle.fetch_sub(1);
        break;
    }

    _pool._events.emit<WorkerStoppedEvent>(_id, _tasksCompleted.load());
    if (retired) {
        const PoolStats s = _pool.stats();
        _pool._events.emit<PoolScaledDownEvent>(s.activeWorkers, s.idleWorkers, s.queuedTasks, 1);
        // A task may have been queued while we were deciding to retire: make sure somebody serves it.
        _pool.ensureCapacity();
    }
    _finished.store(true);
}

} // namespace TK4CPP::TP
