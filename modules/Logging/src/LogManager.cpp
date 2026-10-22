#include "TK4CPP/Logging/LogManager.h"

#include <algorithm>
#include <stdexcept>

#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#if !defined(_WIN32)
    #include "spdlog/sinks/syslog_sink.h"
#endif

#include "NotificationSink.h"

namespace TK4CPP::Logging {

namespace {

using Event = LogSystemNotification::Event;

/// Throws std::invalid_argument if a file based sink has no path.
void requirePath(const SinkConfig& sc) {
    if (sc.filePath.empty())
        throw std::invalid_argument("sink '" + sc.name + "' requires a file path");
}

/// Creates the spdlog sink described by @p sc and applies its level and pattern.
spdlog::sink_ptr makeSink(const SinkConfig& sc, const LogConfig& cfg) {
    spdlog::sink_ptr sink;

    switch (sc.type) {
    case SinkConfig::Type::Console:
        sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        break;
    case SinkConfig::Type::File:
        requirePath(sc);
        sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(sc.filePath, false);
        break;
    case SinkConfig::Type::RotatingFile:
        requirePath(sc);
        sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            sc.filePath, sc.maxFileSize, sc.maxFiles);
        break;
    case SinkConfig::Type::DailyFile:
        requirePath(sc);
        sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
            sc.filePath, sc.rotationHour, sc.rotationMinute);
        break;
    case SinkConfig::Type::Syslog:
#if !defined(_WIN32)
        sink = std::make_shared<spdlog::sinks::syslog_sink_mt>(
            sc.name.empty() ? "tk4cpp" : sc.name, 0, LOG_USER, true);
        break;
#else
        throw std::invalid_argument("syslog sinks are not available on Windows");
#endif
    case SinkConfig::Type::Custom:
        if (!sc.customSink)
            throw std::invalid_argument("sink '" + sc.name + "' is Custom but has no customSink");
        sink = sc.customSink;
        break;
    }

    sink->set_level(detail::toSpdLevel(sc.level));
    if (!sc.pattern.empty())
        sink->set_pattern(sc.pattern);
    else if (sc.type != SinkConfig::Type::Custom && !cfg.globalPattern.empty())
        sink->set_pattern(cfg.globalPattern);
    return sink;
}

} // namespace

LogManager::LogManager() : _hub(std::make_shared<detail::NotificationSink>()) {}

LogManager::~LogManager() {
    try {
        shutdown();
    } catch (...) {
    }
}

LogManager& LogManager::instance() {
    static LogManager inst;
    return inst;
}

// ── Initialisation ───────────────────────────────────────────────────────────

void LogManager::initLocked(const LogConfig& config, bool implicit, Events& events, PoolPtr& retiredPool) {
    // Build everything first (this may throw) and commit only when it all worked.
    std::vector<spdlog::sink_ptr> sinks;
    sinks.reserve(config.sinks.size());
    for (const auto& sc : config.sinks) {
        sinks.push_back(makeSink(sc, config));
        events.emplace_back(Event::SinkAdded, sc.name);
    }

    PoolPtr pool;
    if (config.asyncQueue)
        pool = std::make_shared<spdlog::details::thread_pool>(std::max<std::size_t>(config.asyncQueueSize, 1), 1);

    _config = config;
    _sinks = std::move(sinks);
    _loggers.clear();
    retiredPool = std::move(_pool);
    _pool = std::move(pool);
    _initialized = true;
    _implicit = implicit;
    events.emplace_back(Event::Initialized, "LogManager");
}

bool LogManager::doInit(const LogConfig& config, LogError& error) {
    Events events;
    PoolPtr retiredPool; // destroyed after the lock is released (its destructor joins a thread)
    {
        std::unique_lock lock(_mutex);
        if (_initialized && !_implicit) {
            error = {LogError::Code::AlreadyInitialized, "LogManager::init() called more than once"};
            return false;
        }
        try {
            initLocked(config, false, events, retiredPool);
        } catch (const std::invalid_argument& e) {
            error = {LogError::Code::InvalidConfig, e.what()};
            return false;
        } catch (const std::exception& e) {
            error = {LogError::Code::SinkCreationFailed, e.what()};
            return false;
        }
    }
    retiredPool.reset();
    emit(events, config.notifyEnabled);
    return true;
}

void LogManager::init(const LogConfig& config) {
    LogError error{};
    if (!doInit(config, error))
        throw std::runtime_error(error.message);
}

#if defined(__cpp_lib_expected)
std::expected<void, LogError> LogManager::tryInit(const LogConfig& config) {
    LogError error{};
    if (!doInit(config, error))
        return std::unexpected(std::move(error));
    return {};
}
#endif

bool LogManager::isInitialized() const {
    std::shared_lock lock(_mutex);
    return _initialized;
}

// ── Loggers ──────────────────────────────────────────────────────────────────

std::shared_ptr<spdlog::logger> LogManager::createLoggerLocked(std::string_view name, Events& events) {
    std::vector<spdlog::sink_ptr> sinks = _sinks;
    if (_config.notifyEnabled)
        sinks.push_back(_hub);

    std::shared_ptr<spdlog::logger> logger;
    if (_pool) {
        logger = std::make_shared<spdlog::async_logger>(
            std::string(name), sinks.begin(), sinks.end(), _pool, spdlog::async_overflow_policy::block);
    } else {
        logger = std::make_shared<spdlog::logger>(std::string(name), sinks.begin(), sinks.end());
    }
    logger->set_level(detail::toSpdLevel(_config.globalLevel));
    logger->flush_on(detail::toSpdLevel(_config.flushLevel));

    // Not registered in spdlog's global registry: registering would overwrite the
    // per-sink patterns and would make spdlog::shutdown() affect unrelated loggers.
    _loggers.emplace(std::string(name), logger);
    events.emplace_back(Event::LoggerCreated, std::string(name));
    return logger;
}

std::shared_ptr<spdlog::logger> LogManager::getLogger(std::string_view name) {
    {
        std::shared_lock lock(_mutex);
        if (_initialized) {
            if (const auto it = _loggers.find(name); it != _loggers.end())
                return it->second;
        }
    }

    Events events;
    PoolPtr retiredPool;
    std::shared_ptr<spdlog::logger> logger;
    bool notify = false;
    {
        std::unique_lock lock(_mutex);
        if (!_initialized)
            initLocked(LogConfig::consoleOnly(), true, events, retiredPool);
        if (const auto it = _loggers.find(name); it != _loggers.end())
            logger = it->second; // created by another thread in the meantime
        else
            logger = createLoggerLocked(name, events);
        notify = _config.notifyEnabled;
    }
    retiredPool.reset();
    emit(events, notify);
    return logger;
}

bool LogManager::notifyEnabled() const {
    std::shared_lock lock(_mutex);
    return _config.notifyEnabled;
}

void LogManager::setLevel(std::string_view loggerName, Level level) {
    getLogger(loggerName)->set_level(detail::toSpdLevel(level));
    if (notifyEnabled())
        _hub->dispatch(LogSystemNotification(Event::LevelChanged, std::string(loggerName)));
}

void LogManager::setGlobalLevel(Level level) {
    bool notify = false;
    {
        std::unique_lock lock(_mutex);
        _config.globalLevel = level;
        for (auto& [name, logger] : _loggers)
            logger->set_level(detail::toSpdLevel(level));
        notify = _config.notifyEnabled;
    }
    emit({LogSystemNotification(Event::LevelChanged, "*")}, notify);
}

void LogManager::flushAll() {
    std::vector<std::shared_ptr<spdlog::logger>> loggers;
    bool notify = false;
    {
        std::shared_lock lock(_mutex);
        loggers.reserve(_loggers.size());
        for (const auto& [name, logger] : _loggers)
            loggers.push_back(logger);
        notify = _config.notifyEnabled;
    }
    for (const auto& logger : loggers)
        logger->flush();
    emit({LogSystemNotification(Event::FlushAll)}, notify);
}

void LogManager::shutdown() {
    std::vector<std::shared_ptr<spdlog::logger>> loggers;
    PoolPtr pool;
    bool notify = false;
    {
        std::unique_lock lock(_mutex);
        if (!_initialized)
            return;
        loggers.reserve(_loggers.size());
        for (const auto& [name, logger] : _loggers)
            loggers.push_back(logger);
        pool = std::move(_pool);
        notify = _config.notifyEnabled;
        _loggers.clear();
        _sinks.clear();
        _initialized = false;
        _implicit = false;
    }

    Events events;
    for (const auto& logger : loggers) {
        logger->flush();
        events.emplace_back(Event::LoggerDestroyed, logger->name());
    }
    loggers.clear();
    // Destroying the pool drains the queue, so every record has reached the sinks
    // (and the observers) before the Shutdown event is emitted.
    pool.reset();
    events.emplace_back(Event::Shutdown, "LogManager");
    emit(events, notify);
}

// ── Observers ────────────────────────────────────────────────────────────────

std::unique_ptr<LogObserver> LogManager::makeObserver() {
    return std::make_unique<LogObserver>(*this);
}

SubscriptionId LogManager::subscribeLog(LogHandler handler, Level minLevel, std::string loggerFilter) {
    return _hub->addLog(std::move(handler), minLevel, std::move(loggerFilter));
}

SubscriptionId LogManager::subscribeSystem(SystemHandler handler) {
    return _hub->addSystem(std::move(handler));
}

bool LogManager::unsubscribe(SubscriptionId id) noexcept {
    return _hub->remove(id);
}

bool LogManager::hasObservers() const noexcept {
    return _hub->hasSubscribers();
}

void LogManager::notify(const LogNotification& notification) {
    if (notifyEnabled())
        _hub->dispatch(notification);
}

void LogManager::emit(const Events& events, bool enabled) {
    if (!enabled)
        return;
    for (const auto& e : events)
        _hub->dispatch(e);
}

} // namespace TK4CPP::Logging
