#pragma once

/**
 * @file LogNotification.h
 * @brief Value types delivered to log observers.
 *
 * Observers are plain callbacks registered on the LogManager (see LogObserver).
 * They receive the types declared here by const reference; the objects are only
 * valid for the duration of the call, so copy what you need to keep.
 */

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "TK4CPP/Logging/LogLevel.h"

namespace TK4CPP::Logging {

/** @brief Identifies a registered observer callback; pass it to LogManager::unsubscribe(). */
using SubscriptionId = std::uint64_t;

/**
 * @brief Immutable copy of a single log record, as seen by observers.
 */
class LogNotification {
public:
    /**
     * @brief Builds a notification.
     * @param level     Severity of the record.
     * @param logger    Name of the logger that produced it.
     * @param message   Fully formatted message payload.
     * @param file      Source file of the call site (may be empty).
     * @param line      Source line of the call site (0 if unknown).
     * @param func      Function name of the call site (may be empty).
     * @param timestamp Time the record was created (defaults to now).
     */
    LogNotification(Level level,
                    std::string logger,
                    std::string message,
                    std::string file = {},
                    int line = 0,
                    std::string func = {},
                    std::chrono::system_clock::time_point timestamp =
                        std::chrono::system_clock::now());

    /** @brief Severity of the record. */
    Level level() const noexcept;
    /** @brief Name of the originating logger. */
    const std::string& loggerName() const noexcept;
    /** @brief Formatted message payload (without timestamp, level, etc.). */
    const std::string& message() const noexcept;
    /** @brief Source file of the call site; empty if not available. */
    const std::string& file() const noexcept;
    /** @brief Source line of the call site; 0 if not available. */
    int line() const noexcept;
    /** @brief Function name of the call site; empty if not available. */
    const std::string& func() const noexcept;
    /** @brief Time at which the record was produced. */
    std::chrono::system_clock::time_point timestamp() const noexcept;

private:
    Level _level;
    std::string _loggerName;
    std::string _message;
    std::string _file;
    int _line;
    std::string _func;
    std::chrono::system_clock::time_point _timestamp;
};

/**
 * @brief Notification about a lifecycle event of the logging system itself.
 */
class LogSystemNotification {
public:
    /** @brief Kind of lifecycle event. */
    enum class Event {
        LoggerCreated,   ///< A named logger was created; detail = logger name.
        LoggerDestroyed, ///< A logger was released by shutdown(); detail = logger name.
        SinkAdded,       ///< A sink was created during init(); detail = sink name.
        FlushAll,        ///< LogManager::flushAll() completed.
        LevelChanged,    ///< A level was changed; detail = logger name, or "*" for the global level.
        Initialized,     ///< LogManager::init() (or implicit initialisation) completed.
        Shutdown         ///< LogManager::shutdown() completed.
    };

    /**
     * @brief Builds a system notification.
     * @param event  What happened.
     * @param detail Event-specific detail (see Event).
     */
    explicit LogSystemNotification(Event event, std::string detail = {});

    /** @brief The event kind. */
    Event event() const noexcept;
    /** @brief Event-specific detail string. */
    const std::string& detail() const noexcept;

private:
    Event _event;
    std::string _detail;
};

/** @brief Callback invoked for every log record accepted by an observer's filters. */
using LogHandler = std::function<void(const LogNotification&)>;

/** @brief Callback invoked for every LogSystemNotification. */
using SystemHandler = std::function<void(const LogSystemNotification&)>;

} // namespace TK4CPP::Logging
