#pragma once

/**
 * @file LogConfig.h
 * @brief Declarative configuration of the logging system.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "spdlog/sinks/sink.h"

#include "TK4CPP/Logging/LogLevel.h"

namespace TK4CPP::Logging {

/**
 * @brief Configuration of a single output sink.
 *
 * Sinks are created once by LogManager::init() and shared by all loggers, so
 * several loggers writing to the same file never open it twice.
 */
struct SinkConfig {
    /** @brief Kind of sink. */
    enum class Type {
        Console,      ///< Coloured stdout.
        File,         ///< Plain file, never rotated.
        RotatingFile, ///< File rotated by size (see maxFileSize / maxFiles).
        DailyFile,    ///< File rotated every day at rotationHour:rotationMinute.
        Syslog,       ///< POSIX syslog (not available on Windows); #name is used as ident.
        Custom        ///< User supplied sink (see #customSink).
    };

    /** @brief Creates a sink description of kind @p t with default settings. */
    explicit SinkConfig(Type t = Type::Console) : type(t) {}

    Type        type = Type::Console;  ///< Kind of sink.
    std::string name;                  ///< Identifier, reported in LogSystemNotification::Event::SinkAdded.
    std::string filePath;              ///< Target path for file based sinks.
    Level       level = Level::Trace;  ///< Per-sink severity threshold.
    std::string pattern;               ///< spdlog pattern; empty = LogConfig::globalPattern.

    std::size_t maxFileSize = 10 * 1024 * 1024; ///< RotatingFile: size that triggers rotation, in bytes.
    std::size_t maxFiles    = 5;                ///< RotatingFile: number of rotated files kept.

    int rotationHour   = 0; ///< DailyFile: hour of the rotation (0-23).
    int rotationMinute = 0; ///< DailyFile: minute of the rotation (0-59).

    /** @brief Sink used when #type is Type::Custom. Its own formatter is kept unless #pattern is set. */
    std::shared_ptr<spdlog::sinks::sink> customSink;

    /** @brief Sets #name. */
    SinkConfig& withName(std::string n)      { name = std::move(n);      return *this; }
    /** @brief Sets #level. */
    SinkConfig& withLevel(Level l)           { level = l;                return *this; }
    /** @brief Sets #pattern. */
    SinkConfig& withPattern(std::string p)   { pattern = std::move(p);   return *this; }
    /** @brief Sets #filePath. */
    SinkConfig& withFile(std::string path)   { filePath = std::move(path); return *this; }
    /** @brief Sets #maxFileSize. */
    SinkConfig& withMaxSize(std::size_t b)   { maxFileSize = b;          return *this; }
    /** @brief Sets #maxFiles. */
    SinkConfig& withMaxFiles(std::size_t n)  { maxFiles = n;             return *this; }
    /** @brief Sets #customSink and switches #type to Type::Custom. */
    SinkConfig& withCustomSink(std::shared_ptr<spdlog::sinks::sink> s) {
        customSink = std::move(s);
        type = Type::Custom;
        return *this;
    }
};

/**
 * @brief Global configuration passed to LogManager::init().
 */
struct LogConfig {
    /** @brief Pattern applied to sinks that do not define their own. */
    std::string globalPattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v";
    /** @brief Initial level of every logger. */
    Level globalLevel = Level::Info;
    /** @brief Messages at or above this level are flushed to the sinks immediately. */
    Level flushLevel = Level::Error;
    /** @brief Output sinks. */
    std::vector<SinkConfig> sinks;
    /** @brief Enables observers and system notifications. */
    bool notifyEnabled = true;
    /** @brief Log through a background thread instead of the calling thread. */
    bool asyncQueue = false;
    /** @brief Capacity of the asynchronous queue, in messages. */
    std::size_t asyncQueueSize = 8192;

    /** @brief Sets #globalPattern. */
    LogConfig& withPattern(std::string p) { globalPattern = std::move(p); return *this; }
    /** @brief Sets #globalLevel. */
    LogConfig& withLevel(Level l)         { globalLevel = l;              return *this; }
    /** @brief Sets #flushLevel. */
    LogConfig& withFlushLevel(Level l)    { flushLevel = l;               return *this; }
    /**
     * @brief Enables asynchronous logging.
     * @param queueSize Capacity of the message queue; producers block when it is full.
     */
    LogConfig& withAsync(std::size_t queueSize = 8192) {
        asyncQueue = true;
        asyncQueueSize = queueSize;
        return *this;
    }
    /** @brief Sets #notifyEnabled. */
    LogConfig& withNotifications(bool on = true) { notifyEnabled = on; return *this; }
    /** @brief Appends a sink. */
    LogConfig& addSink(SinkConfig s) { sinks.push_back(std::move(s)); return *this; }

    /** @brief Console output only. @param level Threshold of both logger and sink. */
    static LogConfig consoleOnly(Level level = Level::Info);
    /** @brief Console (Debug+) plus a rotating file (20 MB x 7). */
    static LogConfig fileAndConsole(const std::string& filePath, Level level = Level::Info);
    /**
     * @brief Production profile: async, console for warnings, daily log and error log.
     * @param logDir  Directory of the log files.
     * @param appName Base name of the log files.
     */
    static LogConfig production(const std::string& logDir, const std::string& appName);
    /** @brief Development profile: everything to the console, with function names. */
    static LogConfig development();
};

} // namespace TK4CPP::Logging
