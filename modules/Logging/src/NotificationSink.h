#pragma once

/**
 * @file NotificationSink.h
 * @brief Private: spdlog sink that fans log records out to observer callbacks.
 *
 * This is the only "observable" machinery of the module. It is deliberately
 * tiny and private; it is not installed.
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "spdlog/sinks/sink.h"

#include "TK4CPP/Logging/LogLevel.h"
#include "TK4CPP/Logging/LogNotification.h"

namespace TK4CPP::Logging::detail {

/** @brief Converts a Level to the spdlog equivalent. */
constexpr spdlog::level::level_enum toSpdLevel(Level l) noexcept {
    switch (l) {
    case Level::Trace:    return spdlog::level::trace;
    case Level::Debug:    return spdlog::level::debug;
    case Level::Info:     return spdlog::level::info;
    case Level::Warn:     return spdlog::level::warn;
    case Level::Error:    return spdlog::level::err;
    case Level::Critical: return spdlog::level::critical;
    case Level::Off:      return spdlog::level::off;
    }
    return spdlog::level::info;
}

/** @brief Converts a spdlog level to the Level equivalent. */
constexpr Level fromSpdLevel(spdlog::level::level_enum l) noexcept {
    switch (l) {
    case spdlog::level::trace:    return Level::Trace;
    case spdlog::level::debug:    return Level::Debug;
    case spdlog::level::info:     return Level::Info;
    case spdlog::level::warn:     return Level::Warn;
    case spdlog::level::err:      return Level::Error;
    case spdlog::level::critical: return Level::Critical;
    default:                      return Level::Off;
    }
}

/**
 * @brief Sink delivering records to registered callbacks.
 *
 * Subscriptions are stored copy-on-write: dispatching works on an immutable
 * snapshot, so callbacks run without any lock held and may subscribe,
 * unsubscribe or log without deadlocking.
 */
class NotificationSink final : public spdlog::sinks::sink {
public:
    /** @brief Adds a log callback. @return Its subscription id. */
    SubscriptionId addLog(LogHandler handler, Level minLevel, std::string loggerFilter);
    /** @brief Adds a lifecycle callback. @return Its subscription id. */
    SubscriptionId addSystem(SystemHandler handler);
    /** @brief Removes a callback. @return true if it existed. */
    bool remove(SubscriptionId id) noexcept;
    /** @brief True if at least one callback is registered. */
    bool hasSubscribers() const noexcept { return _count.load(std::memory_order_relaxed) != 0; }

    /** @brief Delivers a hand-made log notification to the log callbacks. */
    void dispatch(const LogNotification& n);
    /** @brief Delivers a lifecycle notification to the lifecycle callbacks. */
    void dispatch(const LogSystemNotification& n);

    // spdlog::sinks::sink
    void log(const spdlog::details::log_msg& msg) override;
    void flush() override {}
    void set_pattern(const std::string&) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter>) override {}

private:
    struct LogEntry {
        SubscriptionId id;
        LogHandler     handler;
        Level          minLevel;
        std::string    filter;
    };
    struct SystemEntry {
        SubscriptionId id;
        SystemHandler  handler;
    };
    struct Snapshot {
        std::vector<LogEntry>    logs;
        std::vector<SystemEntry> systems;
    };

    std::shared_ptr<const Snapshot> snapshot() const;

    mutable std::mutex              _mutex;
    std::shared_ptr<const Snapshot> _snap = std::make_shared<const Snapshot>();
    SubscriptionId                  _nextId = 1;
    std::atomic<std::size_t>        _count{0};
};

} // namespace TK4CPP::Logging::detail
