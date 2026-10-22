/**
 * @file PoolLogger.h
 * @brief Observer that prints pool events to streams or to a user supplied sink.
 */
#pragma once

#include "Export.h"
#include "ThreadPool.h"

#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace TK4CPP::TP {

/** @brief Severity passed to a PoolLogger sink. */
enum class LogLevel { Debug, Info, Error };

/** @brief Event groups a PoolLogger can report (bit mask). */
enum class LogCategory : unsigned {
    Tasks   = 1u << 0,  ///< queued / started / completed / failed / cancelled / rejected
    Workers = 1u << 1,  ///< worker started / stopped
    Pool    = 1u << 2,  ///< pool started / scaling / shutdown
    All     = Tasks | Workers | Pool
};

[[nodiscard]] constexpr LogCategory operator|(LogCategory a, LogCategory b) noexcept {
    return static_cast<LogCategory>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
[[nodiscard]] constexpr bool hasCategory(LogCategory mask, LogCategory c) noexcept {
    return (static_cast<unsigned>(mask) & static_cast<unsigned>(c)) != 0;
}

/**
 * @brief Logs the events of one ThreadPool.
 *
 * Each line is written atomically (no interleaving between threads). The
 * logger unsubscribes in its destructor. To forward to another logging
 * framework set Options::sink.
 */
class TK4CPP_TP_API PoolLogger {
public:
    /** @brief Receives one formatted line (without trailing newline). */
    using Sink = std::function<void(LogLevel, std::string_view)>;

    struct Options {
        std::ostream* out        = nullptr;  ///< Info lines; nullptr = std::cout.
        std::ostream* err        = nullptr;  ///< Error lines; nullptr = std::cerr.
        bool          timestamps = false;    ///< Prefix streamed lines with local time.
        LogCategory   categories = LogCategory::Tasks | LogCategory::Pool;
        Sink          sink;                  ///< When set, replaces the streams.
    };

    explicit PoolLogger(ThreadPool& pool);
    PoolLogger(ThreadPool& pool, Options options);
    ~PoolLogger();

    PoolLogger(const PoolLogger&) = delete;
    PoolLogger& operator=(const PoolLogger&) = delete;

private:
    void subscribe(ThreadPool& pool);
    void write(LogLevel level, const std::string& line);

    Options                 _opt;
    std::mutex              _mutex;
    std::vector<Connection> _connections;
};

} // namespace TK4CPP::TP
