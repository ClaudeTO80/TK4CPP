#pragma once

/**
 * @file LogObserver.h
 * @brief RAII helper to subscribe callbacks to the log stream.
 */

#include <string>
#include <vector>

#include "TK4CPP/Logging/LogLevel.h"
#include "TK4CPP/Logging/LogNotification.h"

namespace TK4CPP::Logging {

class LogManager;

/**
 * @brief Owns a set of subscriptions on a LogManager and removes them on destruction.
 *
 * Observers are implemented on top of an internal spdlog sink: the callbacks run
 * on the thread that emitted the record (or on the spdlog worker thread when the
 * asynchronous mode is enabled), *after* the logger-level filter has been applied.
 *
 * @par Callback rules
 *  - Callbacks must not throw; exceptions are swallowed so that one faulty
 *    observer cannot affect the others.
 *  - Logging from inside a callback is allowed, but records produced on the
 *    callback's own thread are not delivered to observers again (this prevents
 *    unbounded recursion).
 *  - A callback may call unsubscribeAll() on its own observer.
 *
 * @warning unsubscribeAll() / the destructor do not wait for a callback that is
 *          already running on another thread. State captured by a callback must
 *          therefore outlive any in-flight invocation.
 *
 * @note The LogObserver object itself is not thread-safe: configure it from one thread.
 */
class LogObserver {
public:
    /** @brief Alias of Logging::LogHandler. */
    using LogHandler = Logging::LogHandler;
    /** @brief Alias of Logging::SystemHandler. */
    using SystemHandler = Logging::SystemHandler;

    /**
     * @brief Creates an observer bound to @p manager.
     * @param manager Manager whose log stream is observed; must outlive this object.
     */
    explicit LogObserver(LogManager& manager);

    /** @brief Removes every subscription made through this object. */
    ~LogObserver();

    LogObserver(const LogObserver&) = delete;
    LogObserver& operator=(const LogObserver&) = delete;

    /**
     * @brief Subscribes @p handler to log records.
     * @param handler      Callback to invoke.
     * @param minLevel     Records below this level are not delivered.
     * @param loggerFilter If not empty, only records of the logger with exactly this name are delivered.
     * @return `*this`, for chaining.
     */
    LogObserver& onLog(LogHandler handler,
                       Level minLevel = Level::Trace,
                       std::string loggerFilter = {});

    /**
     * @brief Subscribes @p handler to lifecycle events of the logging system.
     * @param handler Callback to invoke.
     * @return `*this`, for chaining.
     */
    LogObserver& onSystem(SystemHandler handler);

    /** @brief Removes every subscription made through this object. */
    void unsubscribeAll() noexcept;

private:
    LogManager& _manager;
    std::vector<SubscriptionId> _ids;
};

} // namespace TK4CPP::Logging
