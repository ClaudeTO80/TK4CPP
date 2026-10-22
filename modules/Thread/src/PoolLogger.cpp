#include "TK4CPP/ThreadPool/PoolLogger.h"

#include <chrono>
#include <ctime>
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace TK4CPP::TP {

namespace {

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms;
    return os.str();
}

std::string millis(std::chrono::microseconds us) {
    return std::format("{:.3f}ms", static_cast<double>(us.count()) / 1000.0);
}

const std::string& nameOf(const std::shared_ptr<ITask>& t) {
    static const std::string unknown = "?";
    return t ? t->name() : unknown;
}

} // namespace

PoolLogger::PoolLogger(ThreadPool& pool) : PoolLogger(pool, Options{}) {}

PoolLogger::PoolLogger(ThreadPool& pool, Options options) : _opt(std::move(options)) {
    if (!_opt.out) _opt.out = &std::cout;
    if (!_opt.err) _opt.err = &std::cerr;
    subscribe(pool);
}

PoolLogger::~PoolLogger() {
    for (auto& c : _connections) c.disconnect();
}

void PoolLogger::write(LogLevel level, const std::string& line) {
    if (_opt.sink) {
        _opt.sink(level, line);
        return;
    }
    std::string full = _opt.timestamps ? timestamp() + ' ' + line : line;
    full += '\n';
    std::lock_guard lk(_mutex);
    (level == LogLevel::Error ? *_opt.err : *_opt.out) << full << std::flush;
}

void PoolLogger::subscribe(ThreadPool& pool) {
    EventBus& bus = pool.events();

    if (hasCategory(_opt.categories, LogCategory::Tasks)) {
        _connections.push_back(bus.on<TaskSubmittedEvent>([this](const TaskSubmittedEvent& e) {
            write(LogLevel::Debug, std::format("[QUEUE] Task '{}' queued", nameOf(e.task)));
        }));
        _connections.push_back(bus.on<TaskStartedEvent>([this](const TaskStartedEvent& e) {
            write(LogLevel::Debug, std::format("[START] Worker-{} executing '{}'", e.workerId, nameOf(e.task)));
        }));
        _connections.push_back(bus.on<TaskCompletedEvent>([this](const TaskCompletedEvent& e) {
            write(LogLevel::Info, std::format("[DONE] Task '{}' completed in {}", nameOf(e.task), millis(e.elapsed)));
        }));
        _connections.push_back(bus.on<TaskFailedEvent>([this](const TaskFailedEvent& e) {
            write(LogLevel::Error, std::format("[ERROR] Task '{}' failed: {}", nameOf(e.task), e.errorMessage));
        }));
        _connections.push_back(bus.on<TaskCancelledEvent>([this](const TaskCancelledEvent& e) {
            write(LogLevel::Info, std::format("[CANCEL] Task '{}' cancelled", nameOf(e.task)));
        }));
        _connections.push_back(bus.on<TaskRejectedEvent>([this](const TaskRejectedEvent& e) {
            write(LogLevel::Error, std::format("[REJECT] Task '{}' rejected: {}", nameOf(e.task), toString(e.reason)));
        }));
    }

    if (hasCategory(_opt.categories, LogCategory::Workers)) {
        _connections.push_back(bus.on<WorkerStartedEvent>([this](const WorkerStartedEvent& e) {
            write(LogLevel::Debug, std::format("[WORKER] Worker-{} started", e.workerId));
        }));
        _connections.push_back(bus.on<WorkerStoppedEvent>([this](const WorkerStoppedEvent& e) {
            write(LogLevel::Debug, std::format("[WORKER] Worker-{} stopped ({} tasks run)", e.workerId, e.tasksCompleted));
        }));
    }

    if (hasCategory(_opt.categories, LogCategory::Pool)) {
        _connections.push_back(bus.on<PoolStartedEvent>([this](const PoolStartedEvent& e) {
            write(LogLevel::Info, std::format("[POOL] Started with {} workers", e.idleWorkers + e.activeWorkers));
        }));
        _connections.push_back(bus.on<PoolScaledUpEvent>([this](const PoolScaledUpEvent& e) {
            write(LogLevel::Info, std::format("[SCALE] Scaled up to {} workers", e.newWorkerCount));
        }));
        _connections.push_back(bus.on<PoolScaledDownEvent>([this](const PoolScaledDownEvent& e) {
            write(LogLevel::Info, std::format("[SCALE] Scaled down ({} worker(s) retired)", e.removedWorkers));
        }));
        _connections.push_back(bus.on<PoolShutdownEvent>([this](const PoolShutdownEvent& e) {
            write(LogLevel::Info, std::format("[SHUTDOWN] {} shutdown (pending at call: {})",
                                              e.graceful ? "Graceful" : "Immediate", e.pendingTasks));
        }));
    }
}

} // namespace TK4CPP::TP
