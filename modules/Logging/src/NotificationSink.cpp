#include "NotificationSink.h"

#include <optional>
#include <string_view>

#include "spdlog/details/log_msg.h"

namespace TK4CPP::Logging::detail {

namespace {

/// True while the current thread is running an observer callback.
thread_local bool t_dispatching = false;

/// Sets #t_dispatching for the lifetime of the object.
struct DispatchGuard {
    DispatchGuard() noexcept { t_dispatching = true; }
    ~DispatchGuard() { t_dispatching = false; }
};

/// Invokes a callback, swallowing anything it throws.
template <class Handler, class Arg>
void safeInvoke(const Handler& handler, const Arg& arg) noexcept {
    try {
        if (handler) handler(arg);
    } catch (...) {
    }
}

} // namespace

std::shared_ptr<const NotificationSink::Snapshot> NotificationSink::snapshot() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _snap;
}

SubscriptionId NotificationSink::addLog(LogHandler handler, Level minLevel, std::string loggerFilter) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto next = std::make_shared<Snapshot>(*_snap);
    const SubscriptionId id = _nextId++;
    next->logs.push_back({id, std::move(handler), minLevel, std::move(loggerFilter)});
    _snap = std::move(next);
    _count.fetch_add(1, std::memory_order_relaxed);
    return id;
}

SubscriptionId NotificationSink::addSystem(SystemHandler handler) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto next = std::make_shared<Snapshot>(*_snap);
    const SubscriptionId id = _nextId++;
    next->systems.push_back({id, std::move(handler)});
    _snap = std::move(next);
    _count.fetch_add(1, std::memory_order_relaxed);
    return id;
}

bool NotificationSink::remove(SubscriptionId id) noexcept {
    std::lock_guard<std::mutex> lock(_mutex);
    auto next = std::make_shared<Snapshot>(*_snap);
    const auto logs = std::erase_if(next->logs, [id](const LogEntry& e) { return e.id == id; });
    const auto sys  = std::erase_if(next->systems, [id](const SystemEntry& e) { return e.id == id; });
    if (logs + sys == 0) return false;
    _snap = std::move(next);
    _count.fetch_sub(logs + sys, std::memory_order_relaxed);
    return true;
}

void NotificationSink::dispatch(const LogNotification& n) {
    if (!hasSubscribers() || t_dispatching) return;
    const auto snap = snapshot();
    DispatchGuard guard;
    for (const auto& e : snap->logs) {
        if (n.level() < e.minLevel) continue;
        if (!e.filter.empty() && e.filter != n.loggerName()) continue;
        safeInvoke(e.handler, n);
    }
}

void NotificationSink::dispatch(const LogSystemNotification& n) {
    if (!hasSubscribers() || t_dispatching) return;
    const auto snap = snapshot();
    DispatchGuard guard;
    for (const auto& e : snap->systems) safeInvoke(e.handler, n);
}

void NotificationSink::log(const spdlog::details::log_msg& msg) {
    // Fast path: no observers (or called from inside a callback) -> nothing to do.
    if (!hasSubscribers() || t_dispatching) return;

    const auto snap = snapshot();
    const Level level = fromSpdLevel(msg.level);
    const std::string_view name{msg.logger_name.data(), msg.logger_name.size()};

    DispatchGuard guard;
    std::optional<LogNotification> note; // built lazily, once, for the first matching callback
    for (const auto& e : snap->logs) {
        if (level < e.minLevel) continue;
        if (!e.filter.empty() && e.filter != name) continue;
        if (!note) {
            note.emplace(level,
                         std::string(name),
                         std::string(msg.payload.data(), msg.payload.size()),
                         msg.source.filename ? msg.source.filename : "",
                         msg.source.line,
                         msg.source.funcname ? msg.source.funcname : "",
                         msg.time);
        }
        safeInvoke(e.handler, *note);
    }
}

} // namespace TK4CPP::Logging::detail
