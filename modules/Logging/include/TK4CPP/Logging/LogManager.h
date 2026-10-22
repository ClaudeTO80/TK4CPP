#pragma once

/**
 * @file LogManager.h
 * @brief Process-wide entry point of the logging module.
 */

#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <version>

#if defined(__cpp_lib_expected)
    #include <expected>
#endif

#include "spdlog/logger.h"
#include "spdlog/spdlog.h"

#include "TK4CPP/Logging/LogConfig.h"
#include "TK4CPP/Logging/LogLevel.h"
#include "TK4CPP/Logging/LogNotification.h"
#include "TK4CPP/Logging/LogObserver.h"

namespace spdlog::details {
class thread_pool;
}

namespace TK4CPP::Logging {

namespace detail {
class NotificationSink;
}

/**
 * @brief Describes why LogManager::tryInit() failed.
 */
struct LogError {
    /** @brief Failure category. */
    enum class Code {
        AlreadyInitialized, ///< init() was already called explicitly.
        InvalidConfig,      ///< The configuration is inconsistent (e.g. file sink without path).
        SinkCreationFailed  ///< A sink could not be created (e.g. file cannot be opened).
    };

    Code        code;    ///< Failure category.
    std::string message; ///< Human readable description.
};

/**
 * @brief Owns the sinks and loggers of the process and dispatches observer notifications.
 *
 * Typical use:
 * @code
 * auto& log = TK4CPP::Logging::LogManager::instance();
 * log.init(LogConfig::fileAndConsole("app.log"));
 * LL_INFO("net", "listening on port {}", 8080);
 * log.shutdown();
 * @endcode
 *
 * @par Observers
 * There is no separate Observable library: every logger also writes to one
 * internal spdlog sink that forwards the records to the registered callbacks
 * (see LogObserver). Level filtering, source location, asynchronous delivery
 * and thread safety therefore come from spdlog itself.
 *
 * @par Thread safety
 * All members are thread safe.
 *
 * @par Implicit initialisation
 * If getLogger() is called before init(), the manager initialises itself with
 * LogConfig::consoleOnly(). A later explicit init() replaces that configuration;
 * logger handles obtained earlier keep writing to the console.
 */
class LogManager {
public:
    /** @brief Returns the process-wide instance. */
    static LogManager& instance();

    /**
     * @brief Initialises the logging system. Call once, at start-up.
     * @param config Sinks, levels and options.
     * @throws std::runtime_error if already initialised explicitly, or if a sink cannot be created.
     */
    void init(const LogConfig& config);

#if defined(__cpp_lib_expected)
    /**
     * @brief Non-throwing variant of init().
     * @param config Sinks, levels and options.
     * @return Nothing on success, a LogError otherwise. The manager is left unchanged on failure.
     */
    std::expected<void, LogError> tryInit(const LogConfig& config);
#endif

    /**
     * @brief Flushes and releases every logger and sink.
     *
     * In asynchronous mode this blocks until all queued messages have been
     * written. Observer subscriptions are kept, so init() can be called again.
     * Logger handles still held by the caller stay usable but, if asynchronous,
     * silently drop messages.
     */
    void shutdown();

    /** @brief True after init() (explicit or implicit) and before shutdown(). */
    bool isInitialized() const;

    /**
     * @brief Returns the logger called @p name, creating it on first use.
     *
     * All loggers share the same sinks. Lookups take a shared lock only.
     * @param name Logger name, reported by the `%n` pattern flag.
     */
    std::shared_ptr<spdlog::logger> getLogger(std::string_view name);

    /**
     * @brief Changes the level of one logger.
     * @param loggerName Logger to change (created if missing).
     * @param level      New threshold.
     */
    void setLevel(std::string_view loggerName, Level level);

    /** @brief Changes the level of every existing logger and of loggers created later. */
    void setGlobalLevel(Level level);

    /** @brief Flushes every logger. In asynchronous mode the flush is queued, not awaited. */
    void flushAll();

    /** @brief Creates a LogObserver bound to this manager. */
    std::unique_ptr<LogObserver> makeObserver();

    /**
     * @brief Registers a log callback; prefer LogObserver for automatic cleanup.
     * @param handler      Callback to invoke.
     * @param minLevel     Records below this level are skipped.
     * @param loggerFilter If not empty, only records of this logger are delivered.
     * @return Id to pass to unsubscribe().
     */
    SubscriptionId subscribeLog(LogHandler handler,
                                Level minLevel = Level::Trace,
                                std::string loggerFilter = {});

    /** @brief Registers a callback for lifecycle events; prefer LogObserver. */
    SubscriptionId subscribeSystem(SystemHandler handler);

    /**
     * @brief Removes a callback.
     * @return true if @p id was registered.
     */
    bool unsubscribe(SubscriptionId id) noexcept;

    /** @brief True if at least one callback is registered. */
    bool hasObservers() const noexcept;

    /**
     * @brief Delivers @p notification to the log callbacks, bypassing the loggers.
     *
     * Does nothing when notifications are disabled in the configuration.
     */
    void notify(const LogNotification& notification);

private:
    LogManager();
    ~LogManager();
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    struct NameHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };
    using Events = std::vector<LogSystemNotification>;
    using PoolPtr = std::shared_ptr<spdlog::details::thread_pool>;

    bool doInit(const LogConfig& config, LogError& error);
    void initLocked(const LogConfig& config, bool implicit, Events& events, PoolPtr& retiredPool);
    std::shared_ptr<spdlog::logger> createLoggerLocked(std::string_view name, Events& events);
    void emit(const Events& events, bool enabled);
    bool notifyEnabled() const;

    LogConfig _config;
    std::unordered_map<std::string, std::shared_ptr<spdlog::logger>, NameHash, std::equal_to<>> _loggers;
    std::vector<spdlog::sink_ptr> _sinks;
    PoolPtr _pool;
    std::shared_ptr<detail::NotificationSink> _hub;
    mutable std::shared_mutex _mutex;
    bool _initialized = false;
    bool _implicit = false;
};

} // namespace TK4CPP::Logging
