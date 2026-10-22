#include "TK4CPP/ThreadPool/Observers.h"

#include <algorithm>
#include <stdexcept>

namespace TK4CPP::TP {

// ───────────────────────────── MetricsCollector ─────────────────────────────

MetricsCollector::MetricsCollector(ThreadPool& pool) {
    EventBus& bus = pool.events();

    // Attempted = every TaskSubmitted; rejected ones are subtracted by TaskRejected.
    _connections.push_back(bus.on<TaskSubmittedEvent>([this](const TaskSubmittedEvent&) {
        std::lock_guard lk(_mutex);
        ++_m.totalSubmitted;
    }));
    _connections.push_back(bus.on<TaskRejectedEvent>([this](const TaskRejectedEvent&) {
        std::lock_guard lk(_mutex);
        --_m.totalSubmitted;
        ++_m.totalRejected;
    }));
    _connections.push_back(bus.on<TaskCompletedEvent>([this](const TaskCompletedEvent& e) {
        std::lock_guard lk(_mutex);
        ++_m.totalCompleted;
        _m.totalExecutionTime += e.elapsed;
        _m.maxExecutionTime = std::max(_m.maxExecutionTime, e.elapsed);
        _m.avgExecutionTime = _m.totalExecutionTime / static_cast<long long>(_m.totalCompleted);
    }));
    _connections.push_back(bus.on<TaskFailedEvent>([this](const TaskFailedEvent&) {
        std::lock_guard lk(_mutex);
        ++_m.totalFailed;
    }));
    _connections.push_back(bus.on<TaskCancelledEvent>([this](const TaskCancelledEvent&) {
        std::lock_guard lk(_mutex);
        ++_m.totalCancelled;
    }));
    _connections.push_back(bus.on<WorkerStartedEvent>([this](const WorkerStartedEvent&) {
        std::lock_guard lk(_mutex);
        _m.peakWorkers = std::max(_m.peakWorkers, ++_aliveWorkers);
    }));
    _connections.push_back(bus.on<WorkerStoppedEvent>([this](const WorkerStoppedEvent&) {
        std::lock_guard lk(_mutex);
        if (_aliveWorkers > 0) --_aliveWorkers;
    }));
}

MetricsCollector::Metrics MetricsCollector::getMetrics() const {
    std::lock_guard lk(_mutex);
    return _m;
}

void MetricsCollector::reset() {
    std::lock_guard lk(_mutex);
    _m = Metrics{};
    _m.peakWorkers = _aliveWorkers;
}

// ───────────────────────────── RetryHandler ─────────────────────────────

RetryHandler::RetryHandler(ThreadPool& pool, std::size_t maxRetries)
    : _pool(pool), _maxRetries(maxRetries)
{
    EventBus& bus = pool.events();
    _connections.push_back(bus.on<TaskFailedEvent>([this](const TaskFailedEvent& e) { onFailed(e); }));
    _connections.push_back(bus.on<TaskCompletedEvent>([this](const TaskCompletedEvent& e) { onSettled(e); }));
    _connections.push_back(bus.on<TaskCancelledEvent>([this](const TaskCancelledEvent& e) { onSettled(e); }));
}

void RetryHandler::onFailed(const TaskFailedEvent& e) {
    if (!e.task) return;
    {
        std::lock_guard lk(_mutex);
        std::size_t& attempts = _attempts[e.task.get()];
        if (attempts >= _maxRetries) {
            _attempts.erase(e.task.get());
            ++_exhausted;
            return;
        }
        ++attempts;
    }
    // Outside the lock: submit() emits events and may take other locks.
    if (_pool.submit(e.task)) {
        std::lock_guard lk(_mutex);
        ++_performed;
    } else {
        std::lock_guard lk(_mutex);
        _attempts.erase(e.task.get());
    }
}

void RetryHandler::onSettled(const TaskEvent& e) {
    if (!e.task) return;
    std::lock_guard lk(_mutex);
    _attempts.erase(e.task.get());
}

std::size_t RetryHandler::retriesPerformed() const {
    std::lock_guard lk(_mutex);
    return _performed;
}

std::size_t RetryHandler::retriesExhausted() const {
    std::lock_guard lk(_mutex);
    return _exhausted;
}

// ───────────────────────────── FileLogger ─────────────────────────────

namespace {

std::ofstream openOrThrow(const std::string& filename) {
    std::ofstream f(filename, std::ios::out | std::ios::app);
    if (!f.is_open()) throw std::runtime_error("Cannot open log file: " + filename);
    return f;
}

PoolLogger::Options fileOptions(std::ofstream& f, LogCategory categories) {
    PoolLogger::Options o;
    o.out = &f;
    o.err = &f;
    o.timestamps = true;
    o.categories = categories;
    return o;
}

} // namespace

FileLogger::FileLogger(ThreadPool& pool, const std::string& filename, LogCategory categories)
    : _file(openOrThrow(filename))
    , _logger(pool, fileOptions(_file, categories))
{}

FileLogger::~FileLogger() = default;

} // namespace TK4CPP::TP
